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
 *  090526 KL     First version (issue #369).
 */

/* Description:
 *
 *   Test TA_PERCENTRANK (Percent Rank).
 *
 *   The function is a count of strict comparisons scaled to a percentage, so
 *   every value it can emit is one of period+1 exactly-determined doubles.
 *   That is what lets EVERY leg here compare bit-exactly, with no tolerance
 *   anywhere in the file.
 *
 *   Legs:
 *     1. GOLDEN VALUES on the frozen 252-bar TA_SREF close series, bit-exact.
 *        The period 20 and 100 rows are the ta4j 0.22.6 capture published in
 *        issue #369; the period 3 rows carry the counts this corpus produces at
 *        full precision. Together they pin, at once, the count, the window
 *        (previous `period` bars, current bar excluded), the lookback and the
 *        ORDER of the two arithmetic operations -- see leg 2.
 *     2. OPERATION ORDER. (count/period)*100.0 and 100.0*count/period are
 *        different doubles. Seven of the leg-1 rows disagree between the two
 *        spellings (all six period-3 non-trivial rows, plus period 20 bar 58,
 *        the row whose ta4j value 55.00000000000001 is itself the divide-first
 *        answer). A tolerance of any size would hide this, which is why leg 1
 *        compares with `!=` and not checkExpectedValue's 0.01 window.
 *     3. THE TIE RULE, which is the one user-visible ruling on this function
 *        (#369: strict `<`). A constant series is 0.0 on every bar under strict
 *        `<` and 100.0 under `<=`, so the flat case is the whole gate; a signed
 *        zero window is the same question asked where `-0.0 < 0.0` is false.
 *        Monotone series pin the two saturated ends.
 *     4. EMPTY / DEGENERATE ranges: fewer bars than the lookback, and exactly
 *        one output bar.
 *     5. IN-PLACE ALIASING (outReal == inReal), bitwise, over every period from
 *        2 to 60. The window is re-read from the input on every bar and the
 *        store lands exactly on the oldest slot the bar just read, so this is
 *        the leg that catches a store moved ahead of the count.
 *     6. The startIdx/endIdx range sweep, in the EXACT class: each bar is
 *        recomputed from its own window, so no range may move a value at all.
 *
 *   Cross-language coverage rides along: legs 1, 3 and 4 call server_verify, so
 *   under --codegen every backend answers the same bits. The frozen
 *   ta_ref_serve predates this function, so the generic --codegen value
 *   comparison cannot run for it (same situation as RMA and VHF).
 */

/**** Headers ****/
#include <stdio.h>
#include <string.h>

#include "ta_test_priv.h"
#include "ta_test_func.h"
#include "ta_utility.h"
#include "server_verify.h"

/**** Local declarations. ****/
#define PR_CAP        512
#define PR_SYN_NB      40
#define PR_ALIAS_MAXP  60

typedef struct { int period; int bar; double want; } PrGolden;

/* Leg 1. `bar` is the ABSOLUTE bar index; the output index is bar - period.
 *
 * Periods 20 and 100 are ta4j 0.22.6's values for this series, transcribed from
 * the capture published in issue #369 (its "Sample input/output" table, plus
 * the bar-58 value quoted in that card's operation-order measurement). They
 * were NOT re-captured in this tree -- there is no ta4j arm here -- but they
 * are what an implementation of Connors' rule that divides before scaling
 * produces, and both halves of that are load-bearing: change the tie rule, the
 * window or the operation order and at least one row moves.
 *
 * Period 3 is this file's own full-precision table. It exists because period 20
 * and 100 are mostly exact ratios: only bar 58 of the ta4j rows separates the
 * two operation orders, and one row is a thin gate for a rule that MEASURES 90
 * of 249 bars different at period 3. Its counts (0, 1, 2, 3) cover every value
 * the period can emit, at the first, middle and last bar each occurs on. */
static const PrGolden prGolden[] =
{
   /* period 3 -- every emittable value, first/middle/last occurrence */
   {    3,    4,                      0.0 },
   {    3,  132,                      0.0 },
   {    3,  251,                      0.0 },
   {    3,    7,        33.33333333333333 },   /* != 33.333333333333336 */
   {    3,  150,        33.33333333333333 },
   {    3,  249,        33.33333333333333 },
   {    3,    5,        66.66666666666666 },   /* != 66.66666666666667  */
   {    3,  120,        66.66666666666666 },
   {    3,  247,        66.66666666666666 },
   {    3,    3,                    100.0 },
   {    3,  122,                    100.0 },
   {    3,  248,                    100.0 },
   /* period 20 -- ta4j 0.22.6 (#369) */
   {   20,   20,                      0.0 },   /* first output bar */
   {   20,   58,        55.00000000000001 },   /* != 55.0 */
   {   20,   70,                     45.0 },
   {   20,  251,                     10.0 },   /* last output bar */
   /* period 100 -- ta4j 0.22.6 (#369) */
   {  100,  100,                     93.0 },   /* first output bar */
   {  100,  150,                     67.0 },
   {  100,  251,                     35.0 },   /* last output bar */
};
#define NB_PR_GOLDEN ((int)(sizeof(prGolden)/sizeof(PrGolden)))

/* The periods leg 1 drives, in the order the table lists them. */
static const int prGoldenPeriods[] = { 3, 20, 100 };
#define NB_PR_GOLDEN_PERIODS ((int)(sizeof(prGoldenPeriods)/sizeof(int)))

/* Coverage counters. Every leg is silent on success, so a count that reached
 * zero is the only remaining way one could run while comparing nothing. */
static int g_prGoldenCmp;
static int g_prTieCmp;
static int g_prEdgeCmp;
static int g_prAliasCmp;

/**** Local functions declarations. ****/
static ErrorNumber test_percentrank_goldens( const TA_History *history );
static ErrorNumber test_percentrank_tie_rule( void );
static ErrorNumber test_percentrank_edges( void );
static ErrorNumber test_percentrank_aliasing( const TA_Real *in, int nbBars );
static ErrorNumber test_percentrank_range( const TA_Real *in );
static ErrorNumber prCheckUniformSeries( const char *label, const TA_Real *in,
                                         int nbBars, int period, double expected );

/**** Global functions definitions. ****/
ErrorNumber test_func_percentrank( TA_History *history )
{
   ErrorNumber err;
   int nbBars = (int)history->nbBars;

   /* PERCENTRANK has no unstable period; a leftover global setting must not
    * reach it, and the range sweep asserts the same thing from the other side. */
   TA_SetUnstablePeriod( TA_FUNC_UNST_ALL, 0 );

   g_prGoldenCmp = g_prTieCmp = g_prEdgeCmp = g_prAliasCmp = 0;

   err = test_percentrank_goldens( history );
   if( err != TA_TEST_PASS )
      return err;

   err = test_percentrank_tie_rule();
   if( err != TA_TEST_PASS )
      return err;

   err = test_percentrank_edges();
   if( err != TA_TEST_PASS )
      return err;

   err = test_percentrank_aliasing( history->close, nbBars );
   if( err != TA_TEST_PASS )
      return err;

   err = test_percentrank_range( history->close );
   if( err != TA_TEST_PASS )
      return err;

   /* LITERAL counts rather than floors: on the shipped 252-bar corpus every leg
    * above is deterministic. */
   if( nbBars == 252
       && ( g_prGoldenCmp != NB_PR_GOLDEN || g_prTieCmp != 140
            || g_prEdgeCmp != 5 || g_prAliasCmp != 13039 ) )
   {
      printf( "PERCENTRANK Fail: coverage counters (golden %d, tie %d, edges %d, "
              "alias %d) are not what this file was written with (%d, 140, 5, 13039)\n",
              g_prGoldenCmp, g_prTieCmp, g_prEdgeCmp, g_prAliasCmp, NB_PR_GOLDEN );
      return TA_PERCENTRANK_VACUOUS;
   }

   return TA_TEST_PASS;
}

/**** Local functions definitions. ****/

/* (1) + (2) Golden values, bit-exact, and the operation order with them. */
static ErrorNumber test_percentrank_goldens( const TA_History *history )
{
   static TA_Real out[PR_CAP];
   TA_Integer begIdx, nbElement;
   TA_RetCode retCode;
   int nbBars = (int)history->nbBars;
   int p, g;

   if( nbBars > PR_CAP )
      return TA_TESTUTIL_TFRR_BAD_PARAM;

   for( p = 0; p < NB_PR_GOLDEN_PERIODS; p++ )
   {
      int period = prGoldenPeriods[p];
      double optIn[1];

      optIn[0] = (double)period;

      retCode = TA_PERCENTRANK( 0, nbBars-1, history->close, period,
                                &begIdx, &nbElement, out );
      if( retCode != TA_SUCCESS )
      {
         printf( "PERCENTRANK Fail: period %d returned retCode %d\n", period, retCode );
         return TA_TESTUTIL_TFRR_BAD_RETCODE;
      }

      /* The current bar is excluded from its own window, so the lookback is
       * `period`, not `period-1`. A one-bar shift here is exactly the error the
       * golden values below could otherwise absorb into a neighbouring bar. */
      if( begIdx != period || nbElement != nbBars - period )
      {
         printf( "PERCENTRANK Fail: period %d gave begIdx %d / nb %d, expected %d / %d\n",
                 period, (int)begIdx, (int)nbElement, period, nbBars - period );
         return TA_TESTUTIL_TFRR_BAD_BEGIDX;
      }

      for( g = 0; g < NB_PR_GOLDEN; g++ )
      {
         if( prGolden[g].period != period )
            continue;

         if( out[prGolden[g].bar - period] != prGolden[g].want )
         {
            printf( "PERCENTRANK Fail: period %d bar %d = %.17g, expected %.17g\n",
                    period, prGolden[g].bar,
                    out[prGolden[g].bar - period], prGolden[g].want );
            return TA_TESTUTIL_TFRR_BAD_CALCULATION;
         }
         g_prGoldenCmp++;
      }

      if( server_verify_active() )
      {
         ErrorNumber e = server_verify( "PERCENTRANK", 0, nbBars-1, nbBars,
                                        retCode, begIdx, nbElement,
                                        (const TA_Real*[]){ history->close, NULL },
                                        optIn, 1,
                                        (const TA_Real*[]){ out, NULL }, NULL );
         if( e != TA_TEST_PASS )
            return e;
      }
   }

   return TA_TEST_PASS;
}

/* Run PERCENTRANK over a caller-built series and require EVERY output bar to
 * equal `expected` bit for bit. Used by the tie-rule leg, where the answer is
 * the same on every bar and is what separates `<` from `<=`. */
static ErrorNumber prCheckUniformSeries( const char *label, const TA_Real *in,
                                         int nbBars, int period, double expected )
{
   static TA_Real out[PR_CAP];
   TA_Integer begIdx, nbElement, i;
   TA_RetCode retCode;
   double optIn[1];

   optIn[0] = (double)period;

   retCode = TA_PERCENTRANK( 0, nbBars-1, in, period, &begIdx, &nbElement, out );
   if( retCode != TA_SUCCESS )
   {
      printf( "PERCENTRANK %s: expected TA_SUCCESS, got retCode=%d\n", label, retCode );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   if( nbElement != nbBars - period )
   {
      printf( "PERCENTRANK %s: expected %d elements, got %d\n",
              label, nbBars - period, (int)nbElement );
      return TA_TESTUTIL_TFRR_BAD_OUTNBELEMENT;
   }

   for( i = 0; i < nbElement; i++ )
   {
      if( out[i] != expected )
      {
         printf( "PERCENTRANK %s: out[%d]=%.17g, expected %.17g\n",
                 label, (int)i, out[i], expected );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
      g_prTieCmp++;
   }

   if( server_verify_active() )
   {
      ErrorNumber e = server_verify( "PERCENTRANK", 0, nbBars-1, nbBars,
                                     retCode, begIdx, nbElement,
                                     (const TA_Real*[]){ in, NULL },
                                     optIn, 1,
                                     (const TA_Real*[]){ out, NULL }, NULL );
      if( e != TA_TEST_PASS )
         return e;
   }

   return TA_TEST_PASS;
}

/* (3) The tie rule -- the one user-visible ruling (#369: strict `<`).
 *
 * Nothing in the price corpus decides it: a run of equal closes is rare there
 * and a single tie moves one bar by one count, which reads as a value change
 * rather than as a rule change. A flat series makes it categorical -- every bar
 * is 0.0 under `<` and 100.0 under `<=`. */
static ErrorNumber test_percentrank_tie_rule( void )
{
   static TA_Real flat[PR_SYN_NB], up[PR_SYN_NB], down[PR_SYN_NB], zeros[PR_SYN_NB];
   ErrorNumber err;
   int i;

   for( i = 0; i < PR_SYN_NB; i++ )
   {
      flat[i] = 42.0;
      up[i]   = 10.0 + (double)i;
      down[i] = 100.0 - (double)i;
      /* Alternating -0.0 / +0.0. `-0.0 < 0.0` is false and so is `0.0 < -0.0`,
       * so under the strict rule the two zeros count as equal and every bar is
       * 0.0 -- the same answer as the flat series, reached through the one
       * comparison an implementation might get wrong by writing `<=` for just
       * the zero case. */
      zeros[i] = ( i & 1 ) ? -0.0 : 0.0;
   }

   err = prCheckUniformSeries( "constant series -> 0", flat, PR_SYN_NB, 5, 0.0 );
   if( err != TA_TEST_PASS ) return err;

   /* The two saturated ends, which are also the two values a broken count would
    * most likely collapse to. */
   err = prCheckUniformSeries( "strictly rising -> 100", up, PR_SYN_NB, 5, 100.0 );
   if( err != TA_TEST_PASS ) return err;

   err = prCheckUniformSeries( "strictly falling -> 0", down, PR_SYN_NB, 5, 0.0 );
   if( err != TA_TEST_PASS ) return err;

   err = prCheckUniformSeries( "signed zeros -> 0", zeros, PR_SYN_NB, 5, 0.0 );
   if( err != TA_TEST_PASS ) return err;

   return TA_TEST_PASS;
}

/* (4) Degenerate ranges. */
static ErrorNumber test_percentrank_edges( void )
{
   static TA_Real in[PR_SYN_NB];
   static TA_Real out[PR_CAP];
   TA_Integer begIdx, nbElement;
   TA_RetCode retCode;
   double optIn[1];
   int i;

   for( i = 0; i < PR_SYN_NB; i++ )
      in[i] = 10.0 + (double)i;

   /* Fewer bars than the lookback: TA_SUCCESS with nothing produced, and both
    * out-params zeroed rather than left as the caller had them. */
   begIdx = nbElement = -1;
   retCode = TA_PERCENTRANK( 0, 8, in, 9, &begIdx, &nbElement, out );
   if( retCode != TA_SUCCESS || begIdx != 0 || nbElement != 0 )
   {
      printf( "PERCENTRANK Fail: sub-lookback range gave retCode %d, begIdx %d, nb %d\n",
              retCode, (int)begIdx, (int)nbElement );
      return TA_TESTUTIL_TFRR_BAD_OUTNBELEMENT;
   }
   g_prEdgeCmp++;

   /* Exactly one bar of history past the lookback: exactly one output. */
   retCode = TA_PERCENTRANK( 0, 9, in, 9, &begIdx, &nbElement, out );
   if( retCode != TA_SUCCESS || begIdx != 9 || nbElement != 1 || out[0] != 100.0 )
   {
      printf( "PERCENTRANK Fail: one-bar range gave retCode %d, begIdx %d, nb %d, out[0] %.17g\n",
              retCode, (int)begIdx, (int)nbElement, out[0] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }
   g_prEdgeCmp++;

   optIn[0] = 9.0;
   if( server_verify_active() )
   {
      ErrorNumber e = server_verify( "PERCENTRANK", 0, 9, PR_SYN_NB,
                                     retCode, begIdx, nbElement,
                                     (const TA_Real*[]){ in, NULL },
                                     optIn, 1,
                                     (const TA_Real*[]){ out, NULL }, NULL );
      if( e != TA_TEST_PASS )
         return e;
   }

   /* The smallest period the metadata admits, and the anchored call that starts
    * past the lookback: both must keep the window behind the bar. */
   retCode = TA_PERCENTRANK( 0, PR_SYN_NB-1, in, 2, &begIdx, &nbElement, out );
   if( retCode != TA_SUCCESS || begIdx != 2 || nbElement != PR_SYN_NB - 2 )
   {
      printf( "PERCENTRANK Fail: period 2 gave retCode %d, begIdx %d, nb %d\n",
              retCode, (int)begIdx, (int)nbElement );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }
   g_prEdgeCmp++;

   retCode = TA_PERCENTRANK( 30, PR_SYN_NB-1, in, 2, &begIdx, &nbElement, out );
   if( retCode != TA_SUCCESS || begIdx != 30 || nbElement != PR_SYN_NB - 30
       || out[0] != 100.0 )
   {
      printf( "PERCENTRANK Fail: anchored call gave retCode %d, begIdx %d, nb %d\n",
              retCode, (int)begIdx, (int)nbElement );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }
   g_prEdgeCmp++;

   /* An out-of-range period is the generator's contract, not this body's, but a
    * function whose period floor moved would otherwise be caught nowhere here. */
   retCode = TA_PERCENTRANK( 0, PR_SYN_NB-1, in, 1, &begIdx, &nbElement, out );
   if( retCode != TA_BAD_PARAM )
   {
      printf( "PERCENTRANK Fail: period 1 gave retCode %d, expected TA_BAD_PARAM\n", retCode );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   g_prEdgeCmp++;

   return TA_TEST_PASS;
}

/* (5) In-place aliasing (#130): outReal == inReal must give the same bits as a
 * call into a separate buffer, at every period. */
static ErrorNumber test_percentrank_aliasing( const TA_Real *in, int nbBars )
{
   static TA_Real scratch[PR_CAP], out[PR_CAP];
   TA_Integer begIdx, nbElement, aliasBegIdx, aliasNbElement, i;
   TA_RetCode retCode;
   int period;

   if( nbBars > PR_CAP )
      return TA_TESTUTIL_TFRR_BAD_PARAM;

   for( period = 2; period <= PR_ALIAS_MAXP; period++ )
   {
      retCode = TA_PERCENTRANK( 0, nbBars-1, in, period, &begIdx, &nbElement, out );
      if( retCode != TA_SUCCESS )
         return TA_TESTUTIL_TFRR_BAD_RETCODE;

      memcpy( scratch, in, (size_t)nbBars * sizeof(TA_Real) );
      retCode = TA_PERCENTRANK( 0, nbBars-1, scratch, period,
                                &aliasBegIdx, &aliasNbElement, scratch );
      if( retCode != TA_SUCCESS )
         return TA_TESTUTIL_TFRR_BAD_RETCODE;

      if( aliasBegIdx != begIdx || aliasNbElement != nbElement )
      {
         printf( "PERCENTRANK Fail: aliased call at period %d moved the range\n", period );
         return TA_TESTUTIL_TFRR_BAD_BEGIDX;
      }

      for( i = 0; i < nbElement; i++ )
      {
         if( scratch[i] != out[i] )
         {
            printf( "PERCENTRANK Fail: aliased period %d bar %d = %.17g, expected %.17g\n",
                    period, (int)i, scratch[i], out[i] );
            return TA_TESTUTIL_TFRR_BAD_CALCULATION;
         }
         g_prAliasCmp++;
      }
   }

   return TA_TEST_PASS;
}

/* (6) The range sweep, in the EXACT class: every bar is recomputed from its own
 * window, so no startIdx/endIdx pair may move a value by a single bit. */
typedef struct { int period; const TA_Real *in; } PrRangeParam;

static TA_RetCode prRangeTestFunction( TA_Integer startIdx, TA_Integer endIdx,
                                       TA_Real *outputBuffer, TA_Integer *outputBufferInt,
                                       TA_Integer *outBegIdx, TA_Integer *outNbElement,
                                       TA_Integer *lookback, void *opaqueData,
                                       unsigned int outputNb, unsigned int *isOutputInteger )
{
   PrRangeParam *p = (PrRangeParam *)opaqueData;

   (void)outputNb;
   (void)outputBufferInt;
   *isOutputInteger = 0;

   *lookback = TA_PERCENTRANK_Lookback( p->period );
   return TA_PERCENTRANK( startIdx, endIdx, p->in, p->period,
                          outBegIdx, outNbElement, outputBuffer );
}

static ErrorNumber test_percentrank_range( const TA_Real *in )
{
   PrRangeParam param;

   param.period = 20;
   param.in     = in;

   return doRangeTestEx( prRangeTestFunction,
                         TA_STABLE_EXACT,
                         TA_TEST_UNST_NONE,
                         (void *)&param, 1, 0 );
}
