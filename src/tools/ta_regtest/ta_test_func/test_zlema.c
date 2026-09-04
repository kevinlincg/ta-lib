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
 *  KL       Kevin Lin (@kevinlincg)
 *
 * Change history:
 *
 *  MMDDYY BY   Description
 *  -------------------------------------------------------------------
 *  090426 KL   First version (issue #347).
 */

/* Description:
 *
 *   Test TA_ZLEMA (Zero-Lag Exponential Moving Average).
 *
 *   ZLEMA is post-0.6.4, so the --codegen sweep marks it skipped against the
 *   frozen ta_ref_serve and --fuzz-064 auto-skips it. Coverage is this file
 *   plus server_verify / --xlang-hash.
 *
 *   Legs:
 *     1. EXTERNAL GOLDEN VALUES -- the only legs that can catch a wrong
 *        FORMULA (lag, de-lag, alpha, seed window).
 *     2. DIFFERENTIAL vs the shipped TA_EMA over a materialised de-lagged
 *        series, BITWISE. Catches a reordered de-lag or a reordered step; it
 *        cannot catch a wrong lag, which it shares with the code under test.
 *     3. THE DEGENERATE SHAPES -- period 1, a flat series, in-place aliasing,
 *        an empty range.
 *     4. THE MATYPE ARM -- TA_MAType_ZLEMA dispatches to the same code, and
 *        reaches BBANDS.
 *     5. RANGE INDEPENDENCE via doRangeTestEx, in the converging class.
 */

/**** Headers ****/
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ta_test_priv.h"
#include "ta_test_func.h"
#include "ta_utility.h"
#include "server_verify.h"

/**** External functions declarations. ****/
/* None */

/**** External variables declarations. ****/
/* None */

/**** Global variables definitions.    ****/
/* None */

/**** Local declarations.              ****/

#define OUT_CAP 512

/* (1a) Spot values on the standard 252-bar close series
 * (TA_SREF_close_daily_ref_0_PRIV), period 10, EMA unstable period pinned to 0.
 * Recorded in issue #347 from pandas-ta-classic 0.6.52 `ta.zlma(close, 10,
 * mamode="ema")`, which agrees with this implementation to 5.7e-16 over the
 * whole series -- the residue of a different summation order in the seed, not
 * a different formula.
 *
 * Frozen at 1e-12 relative, ~1700x the measured agreement, per the PVO
 * precedent. NOT bitwise: no external arm can be, and leg 2 is where bitwise
 * belongs.
 *
 * What this leg is FOR is the formula, which the in-tree differential cannot
 * see: leg 2 shares the lag and the de-lag with the code under test. Both
 * halves were watched red. Rounding the even-period lag up (`period/2`) is
 * caught by the begIdx/nbElement check, not by a value: the lag is part of the
 * lookback, so the whole output shifts -- nbElement 238 where 239 is asserted.
 * A de-lag coefficient of 1.9 instead of 2.0 keeps the shape and is caught by
 * the values, at 9.9e-2 relative on the first bar, eleven orders of magnitude
 * above this gate.
 */
typedef struct { int idx; TA_Real value; } ZlemaSpot;

static const ZlemaSpot zlemaSrefP10[] = {
   {  13,  94.4185 },
   {  20,  89.1838734789416 },
   {  50,  89.9765328317566 },
   { 100, 114.5840310968303 },
   { 175, 134.35563908057495 },
   { 251, 108.64427272744325 }
};
#define NB_ZLEMA_SREF_SPOT ((int)(sizeof(zlemaSrefP10)/sizeof(zlemaSrefP10[0])))

#define ZLEMA_SREF_PERIOD  10
#define ZLEMA_SREF_BEGIDX  13
#define ZLEMA_SREF_NB      239
#define ZLEMA_SREF_TOL     1e-12

/* (1b) The 15-bar close series Tulip Indicators ships as its own test input
 * (tests/untest.txt), at period 5, with the values recorded in issue #347 and
 * independently reproduced there by pandas-ta-classic to 1.7e-16.
 *
 * These are NOT Tulip's own goldens: `ti_zlema` seeds from a single raw price
 * and emits from bar lag-1, so its warm-up is a different curve and its output
 * range is not comparable to ours at any tolerance. Only the input series is
 * borrowed.
 *
 * Tolerance is half a unit in the last printed place of the widest-printed row
 * (three decimals), which is what a transcribed vector can carry.
 */
static const TA_Real zlemaSmallIn[] = {
   81.59, 81.06, 82.87, 83.00, 83.61, 83.15, 82.84, 83.99,
   84.55, 84.36, 85.53, 86.54, 86.89, 87.77, 87.29
};
static const TA_Real zlemaSmallOut[] = {
   83.762, 84.118, 84.832, 84.798, 85.368667,
   86.485778, 87.073852, 87.715901, 87.707267
};
#define NB_ZLEMA_SMALL_IN  ((int)(sizeof(zlemaSmallIn)/sizeof(zlemaSmallIn[0])))
#define NB_ZLEMA_SMALL_OUT ((int)(sizeof(zlemaSmallOut)/sizeof(zlemaSmallOut[0])))
#define ZLEMA_SMALL_PERIOD 5
#define ZLEMA_SMALL_BEGIDX 6
#define ZLEMA_SMALL_TOL    5e-4

/* Differential sweep bounds. Period 2 is the first period with a recursion
 * (period 1 is the identity copy) and has lag 0; the odd/even alternation of
 * the truncated lag is what makes a dense low range worth walking. */
#define ZLEMA_DIFF_MIN_PERIOD 2
#define ZLEMA_DIFF_MAX_PERIOD 40
#define ZLEMA_DIFF_START_STEP 7

static const int zlemaMaGrid[] = { 2, 3, 5, 10, 14, 30, 100 };
#define NB_ZLEMA_MA_GRID ((int)(sizeof(zlemaMaGrid)/sizeof(zlemaMaGrid[0])))

static ErrorNumber test_zlema_golden   ( const TA_History *history );
static ErrorNumber test_zlema_vs_ema   ( const TA_History *history );
static ErrorNumber test_zlema_edges    ( const TA_History *history );
static ErrorNumber test_zlema_matype   ( const TA_History *history );
static ErrorNumber test_zlema_subrange ( const TA_History *history );

/**** Global functions definitions.   ****/

ErrorNumber test_func_zlema( TA_History *history )
{
   ErrorNumber retValue;
   unsigned int savedUnst = TA_GetUnstablePeriod( TA_FUNC_UNST_EMA );

   /* Every leg below states an output bar, and ZLEMA reads the EMA knob in its
    * lookback. Pin it, and put it back whatever happens. */
   TA_SetUnstablePeriod( TA_FUNC_UNST_EMA, 0 );

   retValue = test_zlema_golden( history );
   if( retValue == TA_TEST_PASS )
      retValue = test_zlema_vs_ema( history );
   if( retValue == TA_TEST_PASS )
      retValue = test_zlema_edges( history );
   if( retValue == TA_TEST_PASS )
      retValue = test_zlema_matype( history );

   TA_SetUnstablePeriod( TA_FUNC_UNST_EMA, savedUnst );

   if( retValue != TA_TEST_PASS )
      return retValue;

   /* The range sweep drives the unstable period itself. */
   return test_zlema_subrange( history );
}

/**** Local functions definitions.    ****/

/* (1) EXTERNAL GOLDEN VALUES. */
static ErrorNumber test_zlema_golden( const TA_History *history )
{
   TA_RetCode retCode;
   TA_Integer begIdx, nbElement;
   static TA_Real out[OUT_CAP];
   int i, nbBars;

   nbBars = (int)history->nbBars;

   /* (1a) the 252-bar reference series at period 10. */
   retCode = TA_ZLEMA( 0, nbBars - 1, history->close, ZLEMA_SREF_PERIOD,
                       &begIdx, &nbElement, out );
   if( retCode != TA_SUCCESS )
   {
      printf( "Fail: TA_ZLEMA sref retCode %d\n", retCode );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }

   if( begIdx != ZLEMA_SREF_BEGIDX || nbElement != ZLEMA_SREF_NB )
   {
      printf( "Fail: TA_ZLEMA sref range: begIdx=%d nbElement=%d (want %d/%d)\n",
              (int)begIdx, (int)nbElement, ZLEMA_SREF_BEGIDX, ZLEMA_SREF_NB );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }

   for( i = 0; i < NB_ZLEMA_SREF_SPOT; i++ )
   {
      int at = zlemaSrefP10[i].idx - (int)begIdx;
      TA_Real want = zlemaSrefP10[i].value;
      TA_Real err  = fabs( out[at] - want ) / fabs( want );
      if( !( err <= ZLEMA_SREF_TOL ) )
      {
         printf( "Fail: TA_ZLEMA sref bar %d: got=%.17g want=%.17g "
                 "(rel err %.3e > %.1e)\n",
                 zlemaSrefP10[i].idx, out[at], want, err, ZLEMA_SREF_TOL );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   if( server_verify_active() )
   {
      const double optParams[1] = { (double)ZLEMA_SREF_PERIOD };
      ErrorNumber e = server_verify( "ZLEMA", 0, nbBars - 1, nbBars,
                        retCode, begIdx, nbElement,
                        (const TA_Real*[]){ history->close, NULL },
                        optParams, 1,
                        (const TA_Real*[]){ out, NULL }, NULL );
      if( e != TA_TEST_PASS )
         return e;
   }

   /* (1b) the 15-bar series at period 5. */
   retCode = TA_ZLEMA( 0, NB_ZLEMA_SMALL_IN - 1, zlemaSmallIn,
                       ZLEMA_SMALL_PERIOD, &begIdx, &nbElement, out );
   if( retCode != TA_SUCCESS )
   {
      printf( "Fail: TA_ZLEMA small retCode %d\n", retCode );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }

   if( begIdx != ZLEMA_SMALL_BEGIDX || nbElement != NB_ZLEMA_SMALL_OUT )
   {
      printf( "Fail: TA_ZLEMA small range: begIdx=%d nbElement=%d (want %d/%d)\n",
              (int)begIdx, (int)nbElement, ZLEMA_SMALL_BEGIDX,
              NB_ZLEMA_SMALL_OUT );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }

   for( i = 0; i < NB_ZLEMA_SMALL_OUT; i++ )
   {
      TA_Real err = fabs( out[i] - zlemaSmallOut[i] );
      if( !( err <= ZLEMA_SMALL_TOL ) )
      {
         printf( "Fail: TA_ZLEMA small idx=%d got=%.17g want=%.17g "
                 "(abs err %.3e > %.1e)\n",
                 i, out[i], zlemaSmallOut[i], err, ZLEMA_SMALL_TOL );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   if( server_verify_active() )
   {
      const double optParams[1] = { (double)ZLEMA_SMALL_PERIOD };
      ErrorNumber e = server_verify( "ZLEMA", 0, NB_ZLEMA_SMALL_IN - 1,
                        NB_ZLEMA_SMALL_IN,
                        retCode, begIdx, nbElement,
                        (const TA_Real*[]){ zlemaSmallIn, NULL },
                        optParams, 1,
                        (const TA_Real*[]){ out, NULL }, NULL );
      if( e != TA_TEST_PASS )
         return e;
   }

   return TA_TEST_PASS;
}

/* (2) DIFFERENTIAL vs the shipped TA_EMA, BITWISE.
 *
 * ZLEMA is exactly EMA(2*P[t] - P[t-lag], n), and it inlines that composition
 * rather than materialising the de-lagged series and calling TA_EMA. This leg
 * builds the series the definition describes and calls the shipped TA_EMA on
 * it, then asserts memcmp equality -- which holds only while the de-lag stays
 * `2.0*x - trailing` (one rounding) and the step stays TA_EMA's own
 * ((v-prev)*k)+prev. Reordering either degrades this to ~1e-15 and turns the
 * strongest available gate into a tolerance, so it is a memcmp on purpose.
 *
 * Alignment: the de-lagged series starts at input bar `lag`, so an index j in
 * it is input bar j+lag, and TA_EMA called over it from its own start emits
 * from de-lagged index n-1, i.e. input bar lag+n-1 -- ZLEMA's lookback with
 * the EMA unstable period pinned to 0, which the caller has done.
 *
 * Period 1 is excluded: ZLEMA short-circuits it to a copy (as TA_EMA does),
 * so there is no recursion to compare.
 */
static ErrorNumber test_zlema_vs_ema( const TA_History *history )
{
   static TA_Real delagged[2048], zlema[2048], reference[2048];
   TA_RetCode retCode;
   TA_Integer zBeg, zNb, eBeg, eNb;
   int period, startIdx, lag, i;
   int nbBars = (int)history->nbBars;
   int comparisons = 0;

   for( period = ZLEMA_DIFF_MIN_PERIOD; period <= ZLEMA_DIFF_MAX_PERIOD; period++ )
   {
      lag = (period - 1) / 2;

      /* The de-lagged series, materialised over the whole history. */
      for( i = lag; i < nbBars; i++ )
         delagged[i - lag] = 2.0 * history->close[i] - history->close[i - lag];

      for( startIdx = 0; startIdx < nbBars - 60; startIdx += ZLEMA_DIFF_START_STEP )
      {
         retCode = TA_ZLEMA( startIdx, nbBars - 1, history->close, period,
                             &zBeg, &zNb, zlema );
         if( retCode != TA_SUCCESS )
         {
            printf( "Fail: TA_ZLEMA period=%d startIdx=%d retCode %d\n",
                    period, startIdx, retCode );
            return TA_TESTUTIL_TFRR_BAD_PARAM;
         }
         if( zNb == 0 )
            continue;

         retCode = TA_EMA( zBeg - lag, nbBars - 1 - lag, delagged, period,
                           &eBeg, &eNb, reference );
         if( retCode != TA_SUCCESS || eBeg != zBeg - lag || eNb != zNb )
         {
            printf( "Fail: TA_EMA alignment period=%d startIdx=%d retCode %d "
                    "eBeg %d (want %d) eNb %d (want %d)\n",
                    period, startIdx, retCode, (int)eBeg, (int)zBeg - lag,
                    (int)eNb, (int)zNb );
            return TA_TESTUTIL_TFRR_BAD_PARAM;
         }

         if( memcmp( reference, zlema, (size_t)zNb * sizeof(TA_Real) ) != 0 )
         {
            for( i = 0; i < zNb; i++ )
               if( reference[i] != zlema[i] )
                  break;
            printf( "Fail: TA_ZLEMA vs TA_EMA period=%d startIdx=%d at %d: "
                    "%.17g vs %.17g\n", period, startIdx, i,
                    zlema[i], reference[i] );
            return TA_TESTUTIL_TFRR_BAD_CALCULATION;
         }
         comparisons += zNb;
      }
   }

   if( comparisons == 0 )
   {
      printf( "Fail: TA_ZLEMA vs TA_EMA compared nothing\n" );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }

   return TA_TEST_PASS;
}

/* (3) THE DEGENERATE SHAPES.
 *
 * (a) period 1 is the identity, BITWISE. The de-lag is already the identity
 *     there (lag 0, and 2.0*x - x is exact), but the recursion is not: at k
 *     exactly 1.0 the step is (x-prev)+prev, which returns x only while
 *     consecutive values stay within a factor of two. The series below spans
 *     six orders of magnitude precisely so that a dropped short-circuit fails
 *     here -- a two-decimal price series would not see it.
 * (b) a flat input returns that constant exactly, at every period. Also the
 *     #112 no-NaN pin: the only divisions are by n and n+1.
 * (c) outReal aliasing inReal, bitwise against the unaliased answer.
 * (d) an empty output range answers Success with a zeroed range.
 */
static ErrorNumber test_zlema_edges( const TA_History *history )
{
   TA_RetCode retCode;
   TA_Integer begIdx, nbElement;
   static TA_Real out[OUT_CAP], alias[OUT_CAP];
   TA_Real wide[64], flat[64];
   int i, g, nbBars;

   nbBars = (int)history->nbBars;

   /* (a) period-1 identity over a wide-magnitude series. */
   for( i = 0; i < 64; i++ )
      wide[i] = ( i % 2 ) ? 1.0e-3 * (TA_Real)(i + 1) : 1.0e3 * (TA_Real)(i + 1);

   retCode = TA_ZLEMA( 0, 63, wide, 1, &begIdx, &nbElement, out );
   if( retCode != TA_SUCCESS || begIdx != 0 || nbElement != 64 )
   {
      printf( "Fail: TA_ZLEMA period-1 retCode %d begIdx %d nb %d\n",
              retCode, (int)begIdx, (int)nbElement );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }
   if( memcmp( out, wide, 64 * sizeof(TA_Real) ) != 0 )
   {
      for( i = 0; i < 64; i++ )
         if( out[i] != wide[i] )
            break;
      printf( "Fail: TA_ZLEMA period-1 at %d: %.17g vs input %.17g\n",
              i, out[i], wide[i] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   /* (b) a flat series is returned exactly. */
   for( i = 0; i < 64; i++ )
      flat[i] = 17.25;

   for( g = 0; g < NB_ZLEMA_MA_GRID; g++ )
   {
      int period = zlemaMaGrid[g];
      if( TA_ZLEMA_Lookback( period ) > 63 )
         continue;

      retCode = TA_ZLEMA( 0, 63, flat, period, &begIdx, &nbElement, out );
      if( retCode != TA_SUCCESS )
      {
         printf( "Fail: TA_ZLEMA flat period %d retCode %d\n", period, retCode );
         return TA_TESTUTIL_TFRR_BAD_PARAM;
      }
      for( i = 0; i < nbElement; i++ )
      {
         if( out[i] != 17.25 )
         {
            printf( "Fail: TA_ZLEMA flat period %d at %d: %.17g (want 17.25)\n",
                    period, i, out[i] );
            return TA_TESTUTIL_TFRR_BAD_CALCULATION;
         }
      }
   }

   /* (c) in-place aliasing. The write index trails both reads by at least the
    * lookback, but the loop must still read before it stores. */
   for( g = 0; g < NB_ZLEMA_MA_GRID; g++ )
   {
      int period = zlemaMaGrid[g];

      retCode = TA_ZLEMA( 0, nbBars - 1, history->close, period,
                          &begIdx, &nbElement, out );
      if( retCode != TA_SUCCESS )
      {
         printf( "Fail: TA_ZLEMA alias reference period %d retCode %d\n",
                 period, retCode );
         return TA_TESTUTIL_TFRR_BAD_PARAM;
      }

      memcpy( alias, history->close, (size_t)nbBars * sizeof(TA_Real) );
      retCode = TA_ZLEMA( 0, nbBars - 1, alias, period, &begIdx, &nbElement, alias );
      if( retCode != TA_SUCCESS )
      {
         printf( "Fail: TA_ZLEMA alias period %d retCode %d\n", period, retCode );
         return TA_TESTUTIL_TFRR_BAD_PARAM;
      }
      if( memcmp( alias, out, (size_t)nbElement * sizeof(TA_Real) ) != 0 )
      {
         for( i = 0; i < nbElement; i++ )
            if( alias[i] != out[i] )
               break;
         printf( "Fail: TA_ZLEMA in-place period %d at %d: %.17g vs %.17g\n",
                 period, i, alias[i], out[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* (d) an input shorter than the lookback. */
   retCode = TA_ZLEMA( 0, 3, history->close, 30, &begIdx, &nbElement, out );
   if( retCode != TA_SUCCESS || begIdx != 0 || nbElement != 0 )
   {
      printf( "Fail: TA_ZLEMA short input retCode %d begIdx %d nb %d "
              "(want Success/0/0)\n", retCode, (int)begIdx, (int)nbElement );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }

   return TA_TEST_PASS;
}

/* (4) THE MATYPE ARM: TA_MAType_ZLEMA == 12 dispatches to the same code.
 * (a) MA(period, ZLEMA) == TA_ZLEMA(period), bit-for-bit, across the grid;
 * (b) MA(1, ZLEMA) takes ma()'s own identity path, before the dispatch;
 * (c) BBANDS' middle band with TA_MAType_ZLEMA == TA_ZLEMA, bit-for-bit --
 *     smoke for every other MAType taker, all of which dispatch through MA.
 */
static ErrorNumber test_zlema_matype( const TA_History *history )
{
   TA_RetCode rcM, rcZ;
   TA_Integer begM, nbM, begZ, nbZ;
   static TA_Real outMA[OUT_CAP], outZLEMA[OUT_CAP];
   static TA_Real outUpper[OUT_CAP], outMiddle[OUT_CAP], outLower[OUT_CAP];
   int g, i, nbBars;

   nbBars = (int)history->nbBars;

   for( g = 0; g < NB_ZLEMA_MA_GRID; g++ )
   {
      int period = zlemaMaGrid[g];

      if( TA_MA_Lookback( period, TA_MAType_ZLEMA ) != TA_ZLEMA_Lookback( period ) )
      {
         printf( "Fail: TA_MA_Lookback(ZLEMA) %d != TA_ZLEMA_Lookback %d "
                 "[period %d]\n",
                 (int)TA_MA_Lookback( period, TA_MAType_ZLEMA ),
                 (int)TA_ZLEMA_Lookback( period ), period );
         return TA_TESTUTIL_TFRR_BAD_PARAM;
      }

      rcM = TA_MA( 0, nbBars - 1, history->close, period, TA_MAType_ZLEMA,
                   &begM, &nbM, outMA );
      rcZ = TA_ZLEMA( 0, nbBars - 1, history->close, period,
                      &begZ, &nbZ, outZLEMA );
      if( rcM != TA_SUCCESS || rcZ != TA_SUCCESS || begM != begZ || nbM != nbZ )
      {
         printf( "Fail: ZLEMA matype [period %d]: MA rc=%d beg=%d nb=%d, "
                 "ZLEMA rc=%d beg=%d nb=%d\n", period, rcM, (int)begM, (int)nbM,
                 rcZ, (int)begZ, (int)nbZ );
         return TA_TESTUTIL_TFRR_BAD_PARAM;
      }
      if( memcmp( outMA, outZLEMA, (size_t)nbZ * sizeof(TA_Real) ) != 0 )
      {
         for( i = 0; i < nbZ; i++ )
            if( outMA[i] != outZLEMA[i] )
               break;
         printf( "Fail: ZLEMA matype [period %d] at %d: %.17g vs %.17g\n",
                 period, i, outMA[i], outZLEMA[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* (b) period 1 through the dispatcher: ma() short-circuits before the
    * switch, so this is a copy of the input and never reaches zlema(). */
   rcM = TA_MA( 0, nbBars - 1, history->close, 1, TA_MAType_ZLEMA,
                &begM, &nbM, outMA );
   if( rcM != TA_SUCCESS || begM != 0 || nbM != nbBars ||
       memcmp( outMA, history->close, (size_t)nbBars * sizeof(TA_Real) ) != 0 )
   {
      printf( "Fail: TA_MA(period 1, ZLEMA) rc=%d beg=%d nb=%d (want the input)\n",
              rcM, (int)begM, (int)nbM );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   /* (c) through BBANDS' MAType parameter. */
   rcM = TA_BBANDS( 0, nbBars - 1, history->close, 20, 2.0, 2.0, TA_MAType_ZLEMA,
                    &begM, &nbM, outUpper, outMiddle, outLower );
   rcZ = TA_ZLEMA( 0, nbBars - 1, history->close, 20, &begZ, &nbZ, outZLEMA );
   if( rcM != TA_SUCCESS || rcZ != TA_SUCCESS || begM != begZ || nbM != nbZ )
   {
      printf( "Fail: BBANDS(ZLEMA) rc=%d beg=%d nb=%d vs ZLEMA rc=%d beg=%d nb=%d\n",
              rcM, (int)begM, (int)nbM, rcZ, (int)begZ, (int)nbZ );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }
   if( memcmp( outMiddle, outZLEMA, (size_t)nbZ * sizeof(TA_Real) ) != 0 )
   {
      for( i = 0; i < nbZ; i++ )
         if( outMiddle[i] != outZLEMA[i] )
            break;
      printf( "Fail: BBANDS(ZLEMA) middle band at %d: %.17g vs %.17g\n",
              i, outMiddle[i], outZLEMA[i] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   return TA_TEST_PASS;
}

/* (5) RANGE INDEPENDENCE. TA_STABLE_CONVERGING through TA_FUNC_UNST_EMA: the
 * recursion is TA_EMA's, seeded at the call's own adjusted start, so two calls
 * with different startIdx agree only as far as the unstable period bounds.
 */
typedef struct
{
   const TA_Real *close;
   TA_Integer     period;
} ZlemaRangeParam;

static TA_RetCode zlemaRangeTestFunction( TA_Integer startIdx, TA_Integer endIdx,
                                          TA_Real *outputBuffer, TA_Integer *outputBufferInt,
                                          TA_Integer *outBegIdx, TA_Integer *outNbElement,
                                          TA_Integer *lookback, void *opaqueData,
                                          unsigned int outputNb, unsigned int *isOutputInteger )
{
   ZlemaRangeParam *p = (ZlemaRangeParam *)opaqueData;

   (void)outputNb;
   (void)outputBufferInt;
   *isOutputInteger = 0;

   *lookback = TA_ZLEMA_Lookback( p->period );
   return TA_ZLEMA( startIdx, endIdx, p->close, p->period,
                    outBegIdx, outNbElement, outputBuffer );
}

static ErrorNumber test_zlema_subrange( const TA_History *history )
{
   ZlemaRangeParam param;
   ErrorNumber retValue;
   TA_Integer periods[3];
   int k;

   periods[0] = 2;   /* the shortest recursion, and lag 0 */
   periods[1] = 10;  /* the golden leg's period, and an even lag */
   periods[2] = 30;  /* the default */

   param.close = history->close;

   for( k = 0; k < 3; k++ )
   {
      param.period = periods[k];
      retValue = doRangeTestEx( zlemaRangeTestFunction,
                                TA_STABLE_CONVERGING, TA_FUNC_UNST_EMA,
                                (void *)&param, 1, 0 );
      if( retValue != TA_TEST_PASS )
      {
         printf( "Fail: TA_ZLEMA range test at period %d\n", (int)param.period );
         return retValue;
      }
   }

   return TA_TEST_PASS;
}
