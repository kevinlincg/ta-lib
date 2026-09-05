/* TA-LIB Copyright (c) 1999-2026, Mario Fortier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 * - Neither name of author nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  KL       Kevin Lin
 *
 * Change history:
 *
 *  MMDDYY BY     Description
 *  -------------------------------------------------------------------
 *  090526 KL     First version (issue #371).
 */

/* Description:
 *
 *   Test TA_FRACTAL (Williams Fractal / bounded swing-pivot detector).
 *
 *   Every comparison here is on INTEGER flags, so every leg is exact: there
 *   is no arithmetic in the function at all, only comparisons of unmodified
 *   input values. A nonzero diff anywhere is a real bug.
 *
 *   Legs:
 *     1. EXTERNAL ORACLE, bitwise: the confirmation-bar flags frozen in the
 *        spec issue (TA-Lib/ta-lib#371), captured from ta4j 0.22.6/0.24.1
 *        (FractalHighIndicator / FractalLowIndicator) and trading-signals
 *        8.3.0 (SwingHigh / SwingLow) over the opening bars of this same
 *        252-bar corpus. It is what pins the two conventions no in-tree
 *        differential can pin on its own: WHERE the verdict is reported (at
 *        candidate+optInRightBars, not at the candidate) and that the arms
 *        are strict.
 *     2. DIFFERENTIAL over the whole corpus, bitwise, against shipped
 *        MAX/MIN: the candidate is a swing high exactly when its high beats
 *        the MAX of the optInLeftBars bars before it AND the MAX of the
 *        optInRightBars bars after it. Same decomposition on MIN for the
 *        low. Run over the eight (left,right) pairs the spec issue measured,
 *        including the asymmetric ones and both period-1 arms.
 *     3. RANGE COHERENCY: no accumulator and no unstable period, so a call
 *        starting late must agree bar-for-bar with the full-range call.
 *     4. Hand-built discriminators for the strict rule and the deterministic
 *        edges: a right-arm tie (the case that separates the strict rule from
 *        the >-left / >=-right variant some charting docs describe), an
 *        outside bar that is both a swing high and a swing low, all-flat, a
 *        monotone ramp, and the exact-fit / oversize windows.
 *     5. The output-distinctness rejection (issue #108). There is no in-place
 *        aliasing leg: both outputs are TA_Integer and both inputs are
 *        TA_Real, so no (input, output) pair can share a buffer.
 *
 *   Cross-language value coverage comes from server_verify below plus the
 *   --xlang-hash sweep; the frozen ta_ref_serve predates this function, so
 *   the --codegen value comparison cannot run for it (same situation as KC
 *   and DONCHIAN).
 */

/**** Headers ****/
#include <stdio.h>
#include <string.h>

#include "ta_test_priv.h"
#include "ta_test_func.h"
#include "ta_utility.h"
#include "server_verify.h"

/**** Local declarations. ****/
#define OUT_CAP 1024

/* The opening bars of the corpus, as the spec issue lists them. Leg 1's pins
 * are only meaningful against this data, so the leg verifies the history it
 * was handed IS this data before comparing anything. */
static const TA_Real fractalRefHigh[] =
{
   93.25, 94.94, 96.375, 96.19, 96.0, 94.72, 95.0, 93.72,
   92.47, 92.75, 96.25, 99.625, 99.125, 92.75, 91.315
};
static const TA_Real fractalRefLow[] =
{
   90.75, 91.405, 94.25, 93.5, 92.815, 93.5, 92.0, 89.75,
   89.44, 90.625, 92.75, 96.315, 96.03, 88.815, 86.75
};
#define NB_REF_BARS ((int)(sizeof(fractalRefHigh)/sizeof(fractalRefHigh[0])))

/* Golden flags at optInLeftBars = optInRightBars = 2, bars 4..14 inclusive,
 * from TA-Lib/ta-lib#371: ta4j 0.22.6/0.24.1 and trading-signals 8.3.0 agree
 * bar for bar on the confirmation-bar lists. Three rows carry a 100, and each
 * one names the candidate it confirms:
 *   bar  4 -> high[ 2] = 96.375 is a strict swing high
 *   bar 13 -> high[11] = 99.625 is a strict swing high
 *   bar 10 -> low [ 8] = 89.44  is a strict swing low
 * A verdict reported at the candidate instead of at candidate+right, or an
 * arm off by one bar, moves at least one of these rows. */
static const TA_Integer fractalGoldHigh[] = { 100, 0, 0, 0, 0, 0, 0, 0, 0, 100, 0 };
static const TA_Integer fractalGoldLow[]  = {   0, 0, 0, 0, 0, 0, 100, 0, 0,  0, 0 };
#define NB_GOLD ((int)(sizeof(fractalGoldHigh)/sizeof(fractalGoldHigh[0])))

/* The eight (left,right) pairs the spec issue measured against both oracle
 * libraries. (1,*) and (*,1) exercise the single-bar arm, which the
 * differential has to read straight out of the input because TA_MAX/TA_MIN
 * do not accept a period of 1. */
typedef struct { int left; int right; } FractalPair;
static const FractalPair fractalPairs[] =
{
   { 1, 1 }, { 2, 2 }, { 3, 3 }, { 5, 5 },
   { 2, 5 }, { 5, 2 }, { 1, 10 }, { 10, 1 }
};
#define NB_PAIRS ((int)(sizeof(fractalPairs)/sizeof(fractalPairs[0])))

/* Coverage counters: these legs report nothing on success, so a count that
 * reached zero is the only remaining way one could run without comparing
 * anything. g_fractalFlagsSeen additionally guards against the differential
 * agreeing on an all-zero output, which would prove nothing about the
 * detector. */
static int g_fractalGoldCmp;
static int g_fractalDiffCmp;
static int g_fractalFlagsSeen;
static int g_fractalRangeCmp;

/* (1) External oracle, bitwise. */
static ErrorNumber test_fractal_oracle( const TA_History *history )
{
   TA_RetCode retCode;
   TA_Integer begIdx, nbElement;
   static TA_Integer sh[OUT_CAP], sl[OUT_CAP];
   int nbBars = (int)history->nbBars;
   int i;

   g_fractalGoldCmp = 0;

   if( nbBars < NB_REF_BARS )
   {
      printf( "FRACTAL oracle skip: need at least %d bars, got %d\n",
              NB_REF_BARS, nbBars );
      return TA_TEST_PASS;
   }
   for( i = 0; i < NB_REF_BARS; i++ )
   {
      if( history->high[i] != fractalRefHigh[i] || history->low[i] != fractalRefLow[i] )
      {
         printf( "FRACTAL oracle skip: history bar %d is (%.17g,%.17g), "
                 "the goldens were captured on (%.17g,%.17g)\n",
                 i, history->high[i], history->low[i],
                 fractalRefHigh[i], fractalRefLow[i] );
         return TA_TEST_PASS;
      }
   }

   retCode = TA_FRACTAL( 0, nbBars - 1, history->high, history->low, 2, 2,
                         &begIdx, &nbElement, sh, sl );
   if( retCode != TA_SUCCESS || begIdx != 4 || nbElement != nbBars - 4 )
   {
      printf( "FRACTAL oracle Fail: rc=%d (%d,%d) expected (4,%d)\n",
              (int)retCode, begIdx, nbElement, nbBars - 4 );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }

   /* Cross-language: bit-identical on every language server. */
   if( server_verify_active() )
   {
      double optIn[2];
      ErrorNumber e;

      optIn[0] = 2.0;
      optIn[1] = 2.0;
      e = server_verify( "FRACTAL", 0, nbBars - 1, nbBars,
                         retCode, begIdx, nbElement,
                         (const TA_Real*[]){ history->high, history->low, NULL },
                         optIn, 2,
                         NULL, (const TA_Integer*[]){ sh, sl, NULL } );
      if( e != TA_TEST_PASS )
         return e;
   }

   /* Output index i is absolute bar begIdx+i, and a verdict depends only on
    * bars [bar-4, bar], so the pins hold on the full-corpus run exactly as
    * they do on the 15-bar prefix they were captured over. */
   for( i = 0; i < NB_GOLD; i++ )
   {
      g_fractalGoldCmp += 2;
      if( sh[i] != fractalGoldHigh[i] || sl[i] != fractalGoldLow[i] )
      {
         printf( "FRACTAL oracle Fail [issue #371 / ta4j 0.22.6 / trading-signals 8.3.0] "
                 "at bar %d: got (%d,%d) expected (%d,%d)\n",
                 begIdx + i, sh[i], sl[i], fractalGoldHigh[i], fractalGoldLow[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   if( g_fractalGoldCmp < 2 * NB_GOLD )
   {
      printf( "FRACTAL oracle Fail: %d comparisons, expected %d\n",
              g_fractalGoldCmp, 2 * NB_GOLD );
      return TA_FRACTAL_ORACLE_VACUOUS;
   }

   return TA_TEST_PASS;
}

/* (2) DIFFERENTIAL, bitwise, against shipped MAX/MIN.
 *
 *   candidate c = bar - optInRightBars
 *   swing high iff  high[c] > MAX(high, left ) evaluated at bar c-1
 *               and high[c] > MAX(high, right) evaluated at bar c+right = bar
 *   swing low  iff  low [c] < MIN(low , left ) at c-1  and  < MIN(low, right) at bar
 *
 * MAX(x,N) reports at bar N-1 first, so the value for absolute bar b sits at
 * output index b-(N-1). A period of 1 is outside TA_MAX's accepted range and
 * is read straight out of the input instead -- the max of one bar is that bar,
 * which is not a re-implementation of anything this leg is testing.
 */
static ErrorNumber test_fractal_differential( const TA_History *history )
{
   TA_RetCode retCode;
   TA_Integer begIdx, nbElement, refBeg, refNb;
   static TA_Integer sh[OUT_CAP], sl[OUT_CAP];
   static TA_Real maxLeft[OUT_CAP], maxRight[OUT_CAP];
   static TA_Real minLeft[OUT_CAP], minRight[OUT_CAP];
   int nbBars = (int)history->nbBars;
   int pi, i, L, R, bar, c, expHigh, expLow;
   double armHighL, armHighR, armLowL, armLowR;

   g_fractalDiffCmp = 0;
   g_fractalFlagsSeen = 0;

   if( nbBars > OUT_CAP )
      nbBars = OUT_CAP;

   for( pi = 0; pi < NB_PAIRS; pi++ )
   {
      L = fractalPairs[pi].left;
      R = fractalPairs[pi].right;
      if( L + R >= nbBars )
         continue;

      if( L > 1 )
      {
         retCode = TA_MAX( 0, nbBars - 1, history->high, L, &refBeg, &refNb, maxLeft );
         if( retCode != TA_SUCCESS )
         {
            printf( "FRACTAL differential Fail: MAX(high,%d) rc=%d\n", L, (int)retCode );
            return TA_TESTUTIL_TFRR_BAD_RETCODE;
         }
         retCode = TA_MIN( 0, nbBars - 1, history->low, L, &refBeg, &refNb, minLeft );
         if( retCode != TA_SUCCESS )
         {
            printf( "FRACTAL differential Fail: MIN(low,%d) rc=%d\n", L, (int)retCode );
            return TA_TESTUTIL_TFRR_BAD_RETCODE;
         }
      }
      if( R > 1 )
      {
         retCode = TA_MAX( 0, nbBars - 1, history->high, R, &refBeg, &refNb, maxRight );
         if( retCode != TA_SUCCESS )
         {
            printf( "FRACTAL differential Fail: MAX(high,%d) rc=%d\n", R, (int)retCode );
            return TA_TESTUTIL_TFRR_BAD_RETCODE;
         }
         retCode = TA_MIN( 0, nbBars - 1, history->low, R, &refBeg, &refNb, minRight );
         if( retCode != TA_SUCCESS )
         {
            printf( "FRACTAL differential Fail: MIN(low,%d) rc=%d\n", R, (int)retCode );
            return TA_TESTUTIL_TFRR_BAD_RETCODE;
         }
      }

      retCode = TA_FRACTAL( 0, nbBars - 1, history->high, history->low, L, R,
                            &begIdx, &nbElement, sh, sl );
      if( retCode != TA_SUCCESS || begIdx != L + R || nbElement != nbBars - (L + R) )
      {
         printf( "FRACTAL differential Fail [L=%d R=%d]: rc=%d (%d,%d) expected (%d,%d)\n",
                 L, R, (int)retCode, begIdx, nbElement, L + R, nbBars - (L + R) );
         return TA_TESTUTIL_TFRR_BAD_BEGIDX;
      }

      if( server_verify_active() )
      {
         double optIn[2];
         ErrorNumber e;

         optIn[0] = (double)L;
         optIn[1] = (double)R;
         e = server_verify( "FRACTAL", 0, nbBars - 1, nbBars,
                            retCode, begIdx, nbElement,
                            (const TA_Real*[]){ history->high, history->low, NULL },
                            optIn, 2,
                            NULL, (const TA_Integer*[]){ sh, sl, NULL } );
         if( e != TA_TEST_PASS )
            return e;
      }

      for( i = 0; i < nbElement; i++ )
      {
         bar = begIdx + i;
         c   = bar - R;

         armHighL = ( L > 1 ) ? maxLeft[(c - 1) - (L - 1)]  : history->high[c - 1];
         armHighR = ( R > 1 ) ? maxRight[bar - (R - 1)]     : history->high[bar];
         armLowL  = ( L > 1 ) ? minLeft[(c - 1) - (L - 1)]  : history->low[c - 1];
         armLowR  = ( R > 1 ) ? minRight[bar - (R - 1)]     : history->low[bar];

         expHigh = ( history->high[c] > armHighL && history->high[c] > armHighR ) ? 100 : 0;
         expLow  = ( history->low[c]  < armLowL  && history->low[c]  < armLowR  ) ? 100 : 0;

         g_fractalDiffCmp += 2;
         if( sh[i] == 100 )
            g_fractalFlagsSeen++;
         if( sl[i] == 100 )
            g_fractalFlagsSeen++;

         if( sh[i] != expHigh || sl[i] != expLow )
         {
            printf( "FRACTAL differential Fail [L=%d R=%d] bar %d: got (%d,%d) "
                    "expected (%d,%d) from MAX/MIN arms\n",
                    L, R, bar, sh[i], sl[i], expHigh, expLow );
            return TA_TESTUTIL_TFRR_BAD_CALCULATION;
         }
      }
   }

   if( g_fractalDiffCmp < 2 * 100 )
   {
      printf( "FRACTAL differential Fail: %d comparisons, expected >= %d\n",
              g_fractalDiffCmp, 2 * 100 );
      return TA_FRACTAL_ORACLE_VACUOUS;
   }
   /* An all-zero output would agree with the arms trivially. */
   if( g_fractalFlagsSeen < 10 )
   {
      printf( "FRACTAL differential Fail: only %d flags raised over %d comparisons\n",
              g_fractalFlagsSeen, g_fractalDiffCmp );
      return TA_FRACTAL_ORACLE_VACUOUS;
   }

   return TA_TEST_PASS;
}

/* (3) Range coherency: a verdict reads only its own window, so a call that
 * starts late must reproduce the full-range call bar for bar. */
static ErrorNumber test_fractal_range( const TA_History *history )
{
   TA_RetCode retCode;
   TA_Integer fullBeg, fullNb, partBeg, partNb;
   static TA_Integer fullH[OUT_CAP], fullL[OUT_CAP];
   static TA_Integer partH[OUT_CAP], partL[OUT_CAP];
   int nbBars = (int)history->nbBars;
   int i, offset;

   g_fractalRangeCmp = 0;

   if( nbBars > OUT_CAP )
      nbBars = OUT_CAP;
   if( nbBars < 120 )
      return TA_TEST_PASS;

   retCode = TA_FRACTAL( 0, nbBars - 1, history->high, history->low, 3, 2,
                         &fullBeg, &fullNb, fullH, fullL );
   if( retCode != TA_SUCCESS )
   {
      printf( "FRACTAL range Fail: full-range rc=%d\n", (int)retCode );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   retCode = TA_FRACTAL( 100, nbBars - 1, history->high, history->low, 3, 2,
                         &partBeg, &partNb, partH, partL );
   if( retCode != TA_SUCCESS || partBeg != 100 || partNb != nbBars - 100 )
   {
      printf( "FRACTAL range Fail: sub-range rc=%d (%d,%d) expected (100,%d)\n",
              (int)retCode, partBeg, partNb, nbBars - 100 );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }

   offset = partBeg - fullBeg;
   for( i = 0; i < partNb; i++ )
   {
      g_fractalRangeCmp += 2;
      if( partH[i] != fullH[offset + i] || partL[i] != fullL[offset + i] )
      {
         printf( "FRACTAL range Fail at bar %d: sub-range (%d,%d) != full (%d,%d)\n",
                 partBeg + i, partH[i], partL[i], fullH[offset + i], fullL[offset + i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }
   if( g_fractalRangeCmp < 2 * 100 )
   {
      printf( "FRACTAL range Fail: %d comparisons, expected >= %d\n",
              g_fractalRangeCmp, 2 * 100 );
      return TA_FRACTAL_ORACLE_VACUOUS;
   }

   return TA_TEST_PASS;
}

/* (4) Hand-built discriminators and deterministic edges. */
static ErrorNumber test_fractal_edges( void )
{
   /* Five bars, one output at bar 4, candidate bar 2, arms of two.
    *
    * tie: the candidate ties the bar immediately to its RIGHT. Under the
    * strict rule ruled on issue #371 -- and implemented by both oracle
    * libraries -- that is not a pivot. Under the >-left / >=-right variant
    * some charting documentation describes, it would be one. This case, and
    * only this case, separates the two. */
   static const TA_Real tieHigh[5]  = { 1.0, 1.0, 5.0, 5.0, 1.0 };
   static const TA_Real tieLow[5]   = { 9.0, 9.0, 5.0, 5.0, 9.0 };
   /* outside bar: strictly the highest AND strictly the lowest of its window,
    * so both flags fire on the same bar. */
   static const TA_Real outHigh[5]  = { 1.0, 1.0, 5.0, 4.0, 1.0 };
   static const TA_Real outLow[5]   = { 9.0, 9.0, 3.0, 4.0, 9.0 };
   static TA_Real flatHigh[32], flatLow[32], rampHigh[32], rampLow[32];
   TA_RetCode retCode;
   TA_Integer begIdx, nbElement;
   static TA_Integer sh[OUT_CAP], sl[OUT_CAP];
   int i;

   retCode = TA_FRACTAL( 0, 4, tieHigh, tieLow, 2, 2, &begIdx, &nbElement, sh, sl );
   if( retCode != TA_SUCCESS || begIdx != 4 || nbElement != 1 )
   {
      printf( "FRACTAL tie Fail: rc=%d (%d,%d) expected (4,1)\n",
              (int)retCode, begIdx, nbElement );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }
   if( sh[0] != 0 || sl[0] != 0 )
   {
      printf( "FRACTAL tie Fail: got (%d,%d), expected (0,0) -- a candidate that "
              "ties its right arm is not a pivot under the strict rule\n",
              sh[0], sl[0] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   retCode = TA_FRACTAL( 0, 4, outHigh, outLow, 2, 2, &begIdx, &nbElement, sh, sl );
   if( retCode != TA_SUCCESS || nbElement != 1 || sh[0] != 100 || sl[0] != 100 )
   {
      printf( "FRACTAL outside-bar Fail: rc=%d (%d,%d) flags (%d,%d), "
              "expected (4,1) and (100,100)\n",
              (int)retCode, begIdx, nbElement, sh[0], sl[0] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   /* All-flat: every bar ties every other, so nothing is ever a pivot. */
   for( i = 0; i < 32; i++ )
   {
      flatHigh[i] = 100.0;
      flatLow[i]  = 100.0;
      rampHigh[i] = 10.0 + i;
      rampLow[i]  = 9.0 + i;
   }
   retCode = TA_FRACTAL( 0, 31, flatHigh, flatLow, 2, 2, &begIdx, &nbElement, sh, sl );
   if( retCode != TA_SUCCESS || begIdx != 4 || nbElement != 28 )
   {
      printf( "FRACTAL flat Fail: rc=%d (%d,%d) expected (4,28)\n",
              (int)retCode, begIdx, nbElement );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }
   for( i = 0; i < nbElement; i++ )
   {
      if( sh[i] != 0 || sl[i] != 0 )
      {
         printf( "FRACTAL flat Fail at out %d: (%d,%d) != (0,0)\n", i, sh[i], sl[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* A monotone ramp: every candidate loses its right arm on the high and its
    * left arm on the low, so again nothing fires. */
   retCode = TA_FRACTAL( 0, 31, rampHigh, rampLow, 2, 3, &begIdx, &nbElement, sh, sl );
   if( retCode != TA_SUCCESS || begIdx != 5 || nbElement != 27 )
   {
      printf( "FRACTAL ramp Fail: rc=%d (%d,%d) expected (5,27)\n",
              (int)retCode, begIdx, nbElement );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }
   for( i = 0; i < nbElement; i++ )
   {
      if( sh[i] != 0 || sl[i] != 0 )
      {
         printf( "FRACTAL ramp Fail at out %d: (%d,%d) != (0,0)\n", i, sh[i], sl[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* Exactly enough history for one verdict, and one bar short of it. */
   retCode = TA_FRACTAL( 0, 4, flatHigh, flatLow, 2, 2, &begIdx, &nbElement, sh, sl );
   if( retCode != TA_SUCCESS || begIdx != 4 || nbElement != 1 )
   {
      printf( "FRACTAL exact-fit Fail: rc=%d (%d,%d) expected (4,1)\n",
              (int)retCode, begIdx, nbElement );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   retCode = TA_FRACTAL( 0, 3, flatHigh, flatLow, 2, 2, &begIdx, &nbElement, sh, sl );
   if( retCode != TA_SUCCESS || nbElement != 0 || begIdx != 0 )
   {
      printf( "FRACTAL short-history Fail: rc=%d (%d,%d) expected (0,0)\n",
              (int)retCode, begIdx, nbElement );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }

   return TA_TEST_PASS;
}

/* (5) Two outputs on one buffer must be rejected (issue #108). No in-place
 * aliasing leg exists: the outputs are TA_Integer and the inputs TA_Real. */
static ErrorNumber test_fractal_distinct_outputs( const TA_History *history )
{
   TA_RetCode retCode;
   TA_Integer begIdx, nbElement;
   static TA_Integer sh[OUT_CAP];
   int nbBars = (int)history->nbBars;

   if( nbBars > OUT_CAP )
      nbBars = OUT_CAP;

   retCode = TA_FRACTAL( 0, nbBars - 1, history->high, history->low, 2, 2,
                         &begIdx, &nbElement, sh, sh );
   if( retCode != TA_BAD_PARAM )
   {
      printf( "FRACTAL distinct-output Fail: shared output buffer accepted (rc=%d)\n",
              (int)retCode );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }

   return TA_TEST_PASS;
}

/**** Global functions definitions.   ****/
ErrorNumber test_func_fractal( TA_History *history )
{
   ErrorNumber err;

   err = test_fractal_oracle( history );
   if( err != TA_TEST_PASS )
      return err;

   err = test_fractal_differential( history );
   if( err != TA_TEST_PASS )
      return err;

   err = test_fractal_range( history );
   if( err != TA_TEST_PASS )
      return err;

   err = test_fractal_edges();
   if( err != TA_TEST_PASS )
      return err;

   err = test_fractal_distinct_outputs( history );
   if( err != TA_TEST_PASS )
      return err;

   return TA_TEST_PASS;
}
