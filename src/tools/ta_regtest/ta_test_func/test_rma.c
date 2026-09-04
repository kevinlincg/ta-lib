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
 *  090426 KL   First version (#348).
 */

/* Description:
 *
 *   Test TA_RMA (Wilder's Smoothed Moving Average).
 *
 *   RMA post-dates the frozen ta_ref_serve, so the --codegen sweep marks it
 *   skipped and compares nothing. Coverage is this file plus server_verify,
 *   --xlang-hash and the boundary/param sweeps.
 *
 *   Legs:
 *     1. THE DIFFERENTIAL, memcmp-exact: TA_RMA(TA_TRANGE(h,l,c), n) is
 *        TA_ATR(n). Both unstable periods pinned to 0. This is the only gate
 *        that sees the coefficient spelling, the fusion, and the fused operand
 *        ORDER, so it is the leg the others exist around.
 *     2. EXACT DYADIC VECTORS, bitwise. Periods 2 and 4, whose coefficients
 *        (1/2, 1/2) and (1/4, 3/4) are exactly representable, over integer
 *        inputs -- so every seed sum, every product and every sum is computed
 *        with no rounding at all and the whole vector is hand-derivable from
 *        the published formula. Deliberately spelling-BLIND (at these two
 *        periods both spellings give the identical pair), which is why leg 1
 *        is the spelling gate and this one is the formula gate.
 *     3. REFERENCE-CORPUS values at periods 14 and 30. Two comparisons per
 *        sample: the shipped double, pinned bitwise, and an independent
 *        exact-rational evaluation of the same recursion, at a relative
 *        tolerance. See the table comment for what each one can and cannot
 *        see. Also driven through server_verify (every active language
 *        server, bitwise).
 *     4. PUBLISHED BOOK / TULIP VECTORS, absolute tolerance at half a unit of
 *        the last printed decimal. Achelis p.366 is the independently derived
 *        one; Tulip's untest.txt vector is a cross-implementation check.
 *     5. PERIOD-1 IDENTITY. Bitwise on a strictly positive series, by value on
 *        a sign-crossing one, plus an explicit pin of the ONE input where the
 *        two differ.
 *     6. FLAT INPUT never returns a NaN or an Inf, and returns that constant
 *        exactly wherever its mean is exact -- which is not everywhere; see
 *        the leg for the magnitude where it is not.
 *     7. ALIASING: outReal over inReal.
 *     8. RANGE INDEPENDENCE via doRangeTestEx in the CONVERGING class.
 *
 *   Legs 1 to 5 each have a mutation that reddens them and nothing earlier
 *   (recorded in the PR that added this file). Legs 6 and 7 do not: every
 *   input-tier mutation tried on them either reddens leg 1 first or leaves them
 *   green, so they are property PINS -- "no NaN/Inf path exists", "aliasing is
 *   supported" -- rather than gates with a demonstrated failure mode. For leg 7
 *   that is structural: the aliased and non-aliased calls run the same indices,
 *   so a reordering that would corrupt the aliased case changes the
 *   non-aliased output too and leg 1 catches it there.
 *
 *   NOT DONE, so that nobody reads this file as covering it: the three live
 *   external oracle arms issue #348 asks for (pandas-ta `ta.rma`, ta4j
 *   `MMAIndicator`, trading-signals `WSMA`/`RMA`) are not wired here -- none of
 *   those runtimes is available in the environment this was written in. Leg 3's
 *   exact-rational arm is a substitute for their VALUE role and is strictly
 *   tighter (worst measured 3.5e-16 against a 1e-12-class external arm), but it
 *   is not an independent reading of the indicator's definition the way another
 *   library is: it shares the coefficient spelling with the implementation. The
 *   independent readings here are legs 1, 2 and 4.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ta_test_priv.h"
#include "ta_test_func.h"
#include "ta_utility.h"
#include "server_verify.h"

/**** External functions declarations. ****/
/* None */

/**** Global variables definitions.    ****/
/* None */

/**** Local declarations.              ****/

#define RMA_BUF_CAP 2000

/* Leg 1: every period from 1 to 60, plus 100 and 200.
 *
 * A SWEEP rather than a handful of periods, and the reason is measured. Four
 * hand-picked periods {3, 14, 30, 100} miss a seed written as
 * `tempReal * (1.0/period)` instead of `tempReal / period`: the two spellings
 * disagree on ~35% of arbitrary operands, but they happen to agree on all
 * eight seeds that {3,14,30,100} produce over this corpus and its true-range
 * series, so that mutation reads GREEN on the narrow list. It is one seed value
 * per period, so the only way to make the arm live is more periods.
 *
 * 1 and 2 carry no spelling information at all -- (wAlpha, wBeta) is exactly
 * (1, 0) and (0.5, 0.5) under either spelling -- and are here for the shape
 * check, not the values. Never narrow this leg to them.
 */
static const int rmaDiffExtraPeriods[] = { 100, 200 };
#define RMA_DIFF_SWEEP_MAX 60
#define NB_RMA_DIFF_EXTRA ((int)(sizeof(rmaDiffExtraPeriods)/sizeof(rmaDiffExtraPeriods[0])))
#define NB_RMA_DIFF_PERIODS (RMA_DIFF_SWEEP_MAX + NB_RMA_DIFF_EXTRA)

/* Leg 2: exact dyadic vectors. Derived by hand from
 *    seed = mean of the first n; v = alpha*x + (1-alpha)*v, alpha = 1/n.
 *
 * period 2, in {1,3,5,2,10,0}: seed (1+3)/2 = 2; then .5*5+.5*2 = 3.5;
 *   .5*2+.5*3.5 = 2.75; .5*10+.5*2.75 = 6.375; .5*0+.5*6.375 = 3.1875.
 * period 4, in {4,8,12,16,0,32}: seed (4+8+12+16)/4 = 10; then
 *   .25*0+.75*10 = 7.5; .25*32+.75*7.5 = 13.625.
 */
static const TA_Real rmaDyad2In[]  = { 1.0, 3.0, 5.0, 2.0, 10.0, 0.0 };
static const TA_Real rmaDyad2Exp[] = { 2.0, 3.5, 2.75, 6.375, 3.1875 };
static const TA_Real rmaDyad4In[]  = { 4.0, 8.0, 12.0, 16.0, 0.0, 32.0 };
static const TA_Real rmaDyad4Exp[] = { 10.0, 7.5, 13.625 };

/* Leg 3: samples over the 252-bar reference close series (test_data.c).
 *
 *   `shipped` is what TA_RMA returns here, pinned BITWISE. It is a pin, not an
 *   oracle: it cannot tell a right answer from a wrong one, only a changed one
 *   from an unchanged one. That is what makes it worth having next to a
 *   tolerance -- a tolerance wide enough to survive a rounding difference is
 *   also wide enough to hide a small real change.
 *
 *   `exact` is the SAME recursion evaluated in exact rational arithmetic (the
 *   two coefficients as exact rationals, no rounding thereafter, one rounding
 *   at the end). It cannot see the coefficient spelling -- it uses the same one
 *   -- but it does see the seed window, the anchor, the sum order and the alpha,
 *   which is what an external oracle arm is for. Worst deviation across the
 *   FULL output, not just these samples: 2.9e-16 at period 14, 3.5e-16 at
 *   period 30.
 */
typedef struct { int idx; double shipped; double exact; } RmaPin;

/* period 14: outBegIdx 13, 239 elements. */
static const RmaPin rmaPin14[] = {
   {   0, 93.857499999999987, 93.857500000000002 },
   {   1, 93.653392857142848, 93.653392857142862 },
   {   2, 93.593507653061209, 93.593507653061224 },
   { 119, 126.79039513948659, 126.79039513948659 },
   { 237, 108.0260369788281,  108.0260369788281  },
   { 238, 108.01489148034038, 108.01489148034038 }
};
#define NB_RMA_PIN14 ((int)(sizeof(rmaPin14)/sizeof(rmaPin14[0])))

/* period 30: outBegIdx 29, 223 elements. */
static const RmaPin rmaPin30[] = {
   {   0, 90.426333333333332, 90.426333333333332 },
   {   1, 90.253788888888892, 90.253788888888892 },
   {   2, 90.149495925925933, 90.149495925925919 },
   { 111, 120.54812802239402, 120.54812802239402 },
   { 221, 108.27699673018506, 108.27699673018508 },
   { 222, 108.26343017251223, 108.26343017251224 }
};
#define NB_RMA_PIN30 ((int)(sizeof(rmaPin30)/sizeof(rmaPin30[0])))

/* 1e-15 is ~3x the worst measured deviation from the exact recursion. Not
 * bitwise: the fl() recursion and the exact one legitimately differ, and
 * demanding equality would be asserting that rounding does not happen.
 */
#define RMA_EXACT_TOL 1e-15

/* Absolute floor for the same comparison. The corpus sits at 90..130, so the
 * relative term decides every sample here; the floor exists only so a future
 * sample taken near zero cannot turn the check into a division blowup.
 */
#define RMA_EXACT_ABS 1e-20

/* Leg 4, arm 1 -- INDEPENDENT. Achelis, Technical Analysis from A to Z, 2nd
 * ed., p.366, transcribed in Tulip Indicators 0.9.2 tests/atoz.txt:296
 * ("wilders 5", the `#page 366` case). 12 bars in, 8 published values.
 *
 * The 4 decimals it prints are the whole precision this arm carries, so the
 * bound is half of the last one. It is NOT slack: two of the eight samples land
 * within 4% of the bound (out[3] is 4.976e-05 off 5e-05, out[7] 4.33e-05), for
 * the ordinary reason that the book rounded them. Do not widen it on the theory
 * that it is nearly failing, and do not tighten it either.
 */
static const TA_Real rmaBookIn[]  = { 62.125, 61.125, 62.3438, 65.3125, 63.9688, 63.4375,
                                      63.0, 63.7812, 63.4062, 63.4062, 62.4375, 61.8438 };
static const TA_Real rmaBookExp[] = { 62.975, 63.0675, 63.054, 63.1995,
                                      63.2408, 63.2739, 63.1066, 62.8540 };
#define RMA_BOOK_TOL 5e-5

/* Leg 4, arm 2 -- CROSS-IMPLEMENTATION, not independent. Tulip Indicators
 * 0.9.2 tests/untest.txt:483 ("wilders 5"), the same 15-bar close series its
 * other vectors use; its header says the file is regenerated from Tulip's own
 * output. 3 decimals, so half a unit is 5e-4. Worth having anyway: Tulip's
 * wilders.c uses the (x - v)*alpha + v form with alpha = 1/n, so this arm
 * agrees with a DIFFERENT arithmetic than ours reaching the same values.
 */
static const TA_Real rmaTulipIn[]  = { 81.59, 81.06, 82.87, 83.00, 83.61, 83.15, 82.84, 83.99,
                                       84.55, 84.36, 85.53, 86.54, 86.89, 87.77, 87.29 };
static const TA_Real rmaTulipExp[] = { 82.426, 82.571, 82.625, 82.898, 83.228, 83.455,
                                       83.870, 84.404, 84.901, 85.475, 85.838 };
#define RMA_TULIP_TOL 5e-4

static ErrorNumber test_rma_atr_differential( const TA_History *history );
static ErrorNumber test_rma_dyadic          ( void );
static ErrorNumber test_rma_reference       ( const TA_History *history );
static ErrorNumber test_rma_published        ( void );
static ErrorNumber test_rma_period_one      ( void );
static ErrorNumber test_rma_flat            ( void );
static ErrorNumber test_rma_aliasing        ( const TA_History *history );
static ErrorNumber test_rma_range           ( const TA_History *history );

/**** Global functions definitions.   ****/

ErrorNumber test_func_rma( TA_History *history )
{
   ErrorNumber retValue;

   retValue = test_rma_atr_differential( history );
   if( retValue != TA_TEST_PASS )
      return retValue;

   retValue = test_rma_dyadic();
   if( retValue != TA_TEST_PASS )
      return retValue;

   retValue = test_rma_reference( history );
   if( retValue != TA_TEST_PASS )
      return retValue;

   retValue = test_rma_published();
   if( retValue != TA_TEST_PASS )
      return retValue;

   retValue = test_rma_period_one();
   if( retValue != TA_TEST_PASS )
      return retValue;

   retValue = test_rma_flat();
   if( retValue != TA_TEST_PASS )
      return retValue;

   retValue = test_rma_aliasing( history );
   if( retValue != TA_TEST_PASS )
      return retValue;

   retValue = test_rma_range( history );
   if( retValue != TA_TEST_PASS )
      return retValue;

   return TA_TEST_PASS;
}

/**** Local functions definitions.    ****/

/* Compare a run of doubles bit for bit. Returns the first differing index, or
 * -1. memcmp over the whole run would answer the question but not say where.
 */
static int firstBitDiff( const TA_Real *a, const TA_Real *b, int nb )
{
   int i;
   for( i = 0; i < nb; i++ )
   {
      if( memcmp( &a[i], &b[i], sizeof(TA_Real) ) != 0 )
         return i;
   }
   return -1;
}

/* (1) THE DIFFERENTIAL. TA_RMA over TA_TRANGE is TA_ATR, bit for bit.
 *
 * Why it holds: TRANGE and ATR compute the same true range with the same three
 * statements in the same order, ATR's lookback is one bar more than RMA's over
 * the TRANGE series (TRANGE consumes a previous close), and ATR's seed is the
 * same sequential sum of the same `period` values divided by the same period.
 * So the two run the identical operation sequence over the identical values,
 * and "bit for bit" is a claim about the spelling of three things -- the
 * coefficient derivation order, the seed, and the fused operand order -- rather
 * than about floating point being reproducible.
 *
 * Both unstable periods are pinned to 0 because each function owns its own
 * knob, and the identity only holds while they agree. Restored afterwards.
 */
static ErrorNumber test_rma_atr_differential( const TA_History *history )
{
   TA_RetCode rc;
   TA_Integer trBeg, trNb, atrBeg, atrNb, rmaBeg, rmaNb;
   static TA_Real tr[RMA_BUF_CAP];
   static TA_Real atr[RMA_BUF_CAP];
   static TA_Real rma[RMA_BUF_CAP];
   int n = (int)history->nbBars;
   int k, bad, compared = 0;
   int savedAtr, savedRma;
   ErrorNumber result = TA_TEST_PASS;

   savedAtr = (int)TA_GetUnstablePeriod( TA_FUNC_UNST_ATR );
   savedRma = (int)TA_GetUnstablePeriod( TA_FUNC_UNST_RMA );
   TA_SetUnstablePeriod( TA_FUNC_UNST_ATR, 0 );
   TA_SetUnstablePeriod( TA_FUNC_UNST_RMA, 0 );

   rc = TA_TRANGE( 0, n - 1, history->high, history->low, history->close,
                   &trBeg, &trNb, tr );
   if( rc != TA_SUCCESS || trBeg != 1 || trNb != n - 1 )
   {
      printf( "Fail: TA_TRANGE retCode %d beg %d nb %d\n",
              rc, (int)trBeg, (int)trNb );
      result = TA_TESTUTIL_TFRR_BAD_PARAM;
      goto done;
   }

   for( k = 0; k < NB_RMA_DIFF_PERIODS; k++ )
   {
      int period = k < RMA_DIFF_SWEEP_MAX
                     ? k + 1
                     : rmaDiffExtraPeriods[k - RMA_DIFF_SWEEP_MAX];

      rc = TA_ATR( 0, n - 1, history->high, history->low, history->close,
                   period, &atrBeg, &atrNb, atr );
      if( rc != TA_SUCCESS )
      {
         printf( "Fail: TA_ATR(%d) retCode %d\n", period, rc );
         result = TA_TESTUTIL_TFRR_BAD_PARAM;
         goto done;
      }

      rc = TA_RMA( 0, trNb - 1, tr, period, &rmaBeg, &rmaNb, rma );
      if( rc != TA_SUCCESS )
      {
         printf( "Fail: TA_RMA(%d) retCode %d\n", period, rc );
         result = TA_TESTUTIL_TFRR_BAD_PARAM;
         goto done;
      }

      /* ATR's outBegIdx counts price bars, RMA's counts TRANGE elements, and
       * TRANGE starts at price bar 1 -- so the two must differ by exactly 1.
       * Asserting that is what keeps a shifted comparison from reading as
       * agreement on shorter runs.
       */
      if( rmaNb != atrNb || atrBeg != rmaBeg + 1 )
      {
         printf( "Fail: RMA/ATR period %d shape: rma beg=%d nb=%d, atr beg=%d nb=%d\n",
                 period, (int)rmaBeg, (int)rmaNb, (int)atrBeg, (int)atrNb );
         result = TA_TESTUTIL_TFRR_BAD_BEGIDX;
         goto done;
      }

      bad = firstBitDiff( rma, atr, (int)atrNb );
      if( bad >= 0 )
      {
         printf( "Fail: RMA(TRANGE,%d) != ATR(%d) at %d: %.17g vs %.17g\n",
                 period, period, bad, rma[bad], atr[bad] );
         result = TA_TESTUTIL_TFRR_BAD_CALCULATION;
         goto done;
      }
      compared += (int)atrNb;
   }

   /* The leg compared something, and not a handful of values. The floor is a
    * literal rather than derived from the period list, so shrinking that list
    * fails here instead of quietly lowering the bar with it.
    */
   if( compared < 12000 )
   {
      printf( "Fail: RMA/ATR differential compared only %d values\n", compared );
      result = TA_TESTUTIL_TFRR_BAD_CALCULATION;
      goto done;
   }

done:
   TA_SetUnstablePeriod( TA_FUNC_UNST_ATR, (unsigned int)savedAtr );
   TA_SetUnstablePeriod( TA_FUNC_UNST_RMA, (unsigned int)savedRma );
   return result;
}

/* Run one vector and compare it bitwise. */
static ErrorNumber rmaCheckExactVector( const char *tag,
                                        const TA_Real *in, int nbIn, int period,
                                        const TA_Real *exp, int nbExp )
{
   TA_RetCode rc;
   TA_Integer beg, nb;
   TA_Real out[RMA_BUF_CAP];
   int bad;

   rc = TA_RMA( 0, nbIn - 1, in, period, &beg, &nb, out );
   if( rc != TA_SUCCESS )
   {
      printf( "Fail: TA_RMA %s retCode %d\n", tag, rc );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   if( beg != period - 1 || nb != nbExp )
   {
      printf( "Fail: TA_RMA %s beg=%d nb=%d, expected beg=%d nb=%d\n",
              tag, (int)beg, (int)nb, period - 1, nbExp );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }
   bad = firstBitDiff( out, exp, nbExp );
   if( bad >= 0 )
   {
      printf( "Fail: TA_RMA %s at %d: %.17g, expected exactly %.17g\n",
              tag, bad, out[bad], exp[bad] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }
   return TA_TEST_PASS;
}

/* (2) EXACT DYADIC VECTORS, bitwise, hand-derived from the published formula.
 *
 * At periods 2 and 4 every coefficient, every seed sum and every product is a
 * dyadic rational that fits a double, so the recursion is carried out with no
 * rounding and the expected values are exactly what the formula says. Nothing
 * about the arithmetic is being tolerated here, which is why the comparison is
 * memcmp and not a tolerance.
 */
static ErrorNumber test_rma_dyadic( void )
{
   ErrorNumber r;

   r = rmaCheckExactVector( "dyadic period 2",
                            rmaDyad2In, (int)(sizeof(rmaDyad2In)/sizeof(TA_Real)), 2,
                            rmaDyad2Exp, (int)(sizeof(rmaDyad2Exp)/sizeof(TA_Real)) );
   if( r != TA_TEST_PASS ) return r;

   r = rmaCheckExactVector( "dyadic period 4",
                            rmaDyad4In, (int)(sizeof(rmaDyad4In)/sizeof(TA_Real)), 4,
                            rmaDyad4Exp, (int)(sizeof(rmaDyad4Exp)/sizeof(TA_Real)) );
   if( r != TA_TEST_PASS ) return r;

   return TA_TEST_PASS;
}

/* Drive one reference-corpus period: shape, both value comparisons, and the
 * cross-language replay.
 */
static ErrorNumber rmaCheckReferencePeriod( const TA_History *history, int period,
                                            const RmaPin *pin, int nbPin,
                                            int expBeg, int expNb )
{
   TA_RetCode rc;
   TA_Integer beg, nb;
   static TA_Real out[RMA_BUF_CAP];
   int k;

   rc = TA_RMA( 0, (int)history->nbBars - 1, history->close, period, &beg, &nb, out );
   if( rc != TA_SUCCESS )
   {
      printf( "Fail: TA_RMA(%d) reference retCode %d\n", period, rc );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   if( beg != expBeg || nb != expNb )
   {
      printf( "Fail: TA_RMA(%d) reference beg=%d nb=%d, expected %d/%d\n",
              period, (int)beg, (int)nb, expBeg, expNb );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }

   for( k = 0; k < nbPin; k++ )
   {
      int    idx = pin[k].idx;
      double got = out[idx];
      double err;
      const char *mode;

      if( memcmp( &got, &pin[k].shipped, sizeof(double) ) != 0 )
      {
         printf( "Fail: TA_RMA(%d) pin idx=%d got=%.17g, pinned=%.17g (bitwise)\n",
                 period, idx, got, pin[k].shipped );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
      if( !checkOracleValue( got, pin[k].exact,
                             RMA_EXACT_TOL, RMA_EXACT_ABS, &err, &mode ) )
      {
         printf( "Fail: TA_RMA(%d) exact-recursion idx=%d got=%.17g want=%.17g "
                 "(%s err %.3e)\n", period, idx, got, pin[k].exact, mode, err );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   if( server_verify_active() )
   {
      ErrorNumber e = server_verify( "RMA", 0, (int)history->nbBars - 1, history->nbBars,
                                     rc, beg, nb,
                                     (const TA_Real*[]){ history->close, NULL },
                                     (double[]){ (double)period }, 1,
                                     (const TA_Real*[]){ out, NULL }, NULL );
      if( e != TA_TEST_PASS )
         return e;
   }

   return TA_TEST_PASS;
}

/* (3) REFERENCE CORPUS at the two periods. Unstable period pinned to 0: the
 * pinned constants and the exact recursion both assume it.
 */
static ErrorNumber test_rma_reference( const TA_History *history )
{
   ErrorNumber r;
   int saved = (int)TA_GetUnstablePeriod( TA_FUNC_UNST_RMA );

   TA_SetUnstablePeriod( TA_FUNC_UNST_RMA, 0 );

   r = rmaCheckReferencePeriod( history, 14, rmaPin14, NB_RMA_PIN14, 13, 239 );
   if( r == TA_TEST_PASS )
      r = rmaCheckReferencePeriod( history, 30, rmaPin30, NB_RMA_PIN30, 29, 223 );

   TA_SetUnstablePeriod( TA_FUNC_UNST_RMA, (unsigned int)saved );
   return r;
}

/* Run one published vector at an absolute tolerance. */
static ErrorNumber rmaCheckPublished( const char *tag,
                                      const TA_Real *in, int nbIn, int period,
                                      const TA_Real *exp, int nbExp, double absTol )
{
   TA_RetCode rc;
   TA_Integer beg, nb;
   TA_Real out[RMA_BUF_CAP];
   int i;
   double err;
   const char *mode;

   rc = TA_RMA( 0, nbIn - 1, in, period, &beg, &nb, out );
   if( rc != TA_SUCCESS )
   {
      printf( "Fail: TA_RMA %s retCode %d\n", tag, rc );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   if( beg != period - 1 || nb != nbExp )
   {
      printf( "Fail: TA_RMA %s beg=%d nb=%d, expected beg=%d nb=%d\n",
              tag, (int)beg, (int)nb, period - 1, nbExp );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }
   for( i = 0; i < nbExp; i++ )
   {
      if( !checkOracleValue( out[i], exp[i], 0.0, absTol, &err, &mode ) )
      {
         printf( "Fail: TA_RMA %s at %d: got %.17g expected %.17g (%s=%.3e > abs %.3e)\n",
                 tag, i, out[i], exp[i], mode, err, absTol );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }
   return TA_TEST_PASS;
}

/* (4) THE PUBLISHED VECTORS. Unstable period pinned to 0 -- an external source
 * has no such concept, so a non-zero one here would shift the anchor and the
 * comparison would be against the wrong bars.
 */
static ErrorNumber test_rma_published( void )
{
   ErrorNumber r;
   int saved = (int)TA_GetUnstablePeriod( TA_FUNC_UNST_RMA );

   TA_SetUnstablePeriod( TA_FUNC_UNST_RMA, 0 );

   r = rmaCheckPublished( "Achelis p.366 (atoz.txt:296 wilders 5)",
                          rmaBookIn, (int)(sizeof(rmaBookIn)/sizeof(TA_Real)), 5,
                          rmaBookExp, (int)(sizeof(rmaBookExp)/sizeof(TA_Real)),
                          RMA_BOOK_TOL );
   if( r == TA_TEST_PASS )
      r = rmaCheckPublished( "Tulip untest.txt:483 (wilders 5)",
                             rmaTulipIn, (int)(sizeof(rmaTulipIn)/sizeof(TA_Real)), 5,
                             rmaTulipExp, (int)(sizeof(rmaTulipExp)/sizeof(TA_Real)),
                             RMA_TULIP_TOL );

   TA_SetUnstablePeriod( TA_FUNC_UNST_RMA, (unsigned int)saved );
   return r;
}

/* (5) PERIOD-1 IDENTITY, and the one place it is not bitwise.
 *
 * At period 1 the coefficients are exactly (1, 0), so the step reduces to
 * fma(0, prev, 1*x) -- x plus a signed zero. That is x for every input except
 * a NEGATIVE ZERO, which comes back +0.0 because (+0.0) + (-0.0) is +0.0. The
 * flag RMA declares is `period1_identity`, a claim about VALUES, and it holds:
 * -0.0 == +0.0. This leg pins both halves separately so the difference is
 * recorded rather than discovered.
 *
 * It is a pre-existing property shared with SMA and TRIMA, and it is
 * unreachable from `test_period_boundary.c`, whose three sweep series are
 * strictly positive. RMA is the first flagged member documented over an
 * arbitrary oscillator input, which is what makes writing it down worthwhile.
 */
static ErrorNumber test_rma_period_one( void )
{
   TA_RetCode rc;
   TA_Integer beg, nb;
   /* Strictly positive: the identity is bitwise here. */
   static const TA_Real posIn[] = { 12.5, 3.0, 99.25, 1e-9, 4e8, 7.75 };
   /* Sign-crossing, with both zeros present. Identity by value only. */
   static const TA_Real mixIn[] = { 1.0, -2.5, 0.0, -0.0, 3.0, -1e-9, 7.5 };
   TA_Real out[RMA_BUF_CAP];
   const TA_Real posZero = 0.0;
   int i, nbIn, bad;

   nbIn = (int)(sizeof(posIn)/sizeof(TA_Real));
   rc = TA_RMA( 0, nbIn - 1, posIn, 1, &beg, &nb, out );
   if( rc != TA_SUCCESS || beg != 0 || nb != nbIn )
   {
      printf( "Fail: TA_RMA period 1 (positive) retCode %d beg %d nb %d\n",
              rc, (int)beg, (int)nb );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }
   bad = firstBitDiff( out, posIn, nbIn );
   if( bad >= 0 )
   {
      printf( "Fail: TA_RMA period 1 not bitwise at %d: %.17g vs %.17g\n",
              bad, out[bad], posIn[bad] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   nbIn = (int)(sizeof(mixIn)/sizeof(TA_Real));
   rc = TA_RMA( 0, nbIn - 1, mixIn, 1, &beg, &nb, out );
   if( rc != TA_SUCCESS || beg != 0 || nb != nbIn )
   {
      printf( "Fail: TA_RMA period 1 (mixed) retCode %d beg %d nb %d\n",
              rc, (int)beg, (int)nb );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }
   for( i = 0; i < nbIn; i++ )
   {
      if( out[i] != mixIn[i] )
      {
         printf( "Fail: TA_RMA period 1 value identity at %d: %.17g vs %.17g\n",
                 i, out[i], mixIn[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }
   /* mixIn[3] is -0.0. Pinned as exactly +0.0 -- the one input where the
    * identity is by value and not by bits. A `!= 0.0` check would pass either
    * zero and record nothing.
    */
   if( memcmp( &out[3], &posZero, sizeof(TA_Real) ) != 0 )
   {
      printf( "Fail: TA_RMA period 1 over -0.0 gave %.17g, expected exactly +0.0\n",
              out[3] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   return TA_TEST_PASS;
}

/* (6) FLAT INPUT. The only divisions are by the period, which is >= 1, so no
 * NaN or Inf path exists at all; this pins that, and pins how closely a
 * constant series comes back as that constant.
 *
 * Two arms, because "exactly the constant" is NOT true at every magnitude and
 * issue #348 states it as though it were:
 *
 *   A. BITWISE, on POWER-OF-TWO levels. There the seed is exact -- the partial
 *      sums are m * 2^k for m = 1..n, each representable, so the sum is exactly
 *      n * 2^k and dividing by n is exact -- and the step then holds it,
 *      because alpha * 2^k and beta * 2^k are both exact and their sum is
 *      within half an ULP of 2^k.
 *   B. NEAR, on levels whose mean is not exact. 1e-300 at period 30 is the
 *      counter-example: 30 * 1e-300 needs 58 mantissa bits, so the seed rounds
 *      and comes back 1.0000000000000007e-300. That is one ULP of the seed
 *      accumulator, not drift in the recursion, and it is shared with TA_SMA.
 *
 * What this leg does NOT see, stated so it is not credited with it: the
 * coefficient spelling. Measured -- the alpha-first spelling and a reciprocal
 * multiply in the seed both leave arm A bitwise green, because at a
 * power-of-two level every candidate coefficient pair sums to within half an
 * ULP of 1 and rounds back. Legs 1 and 2 are what see the spelling.
 */
static ErrorNumber test_rma_flat( void )
{
   /* Exactly representable, and their n-fold sums are too. */
   static const double dyadic[] = { 0.0, 1.0, 0.5, 1024.0, 0x1p-1000, 0x1p1000 };
   /* Decimal levels whose mean over some of the periods below is inexact. */
   static const double inexact[] = { 0.1, 1234.5, 1e-300, 1e300, -3.7 };
   const int periods[] = { 1, 2, 7, 30 };
   int nbPeriods = (int)(sizeof(periods)/sizeof(periods[0]));
   TA_Real in[64];
   TA_Real out[RMA_BUF_CAP];
   TA_RetCode rc;
   TA_Integer beg, nb;
   int arm, L, P, i, nbLevels;
   double err;
   const char *mode;

   for( arm = 0; arm < 2; arm++ )
   {
      const double *levels = arm == 0 ? dyadic : inexact;
      nbLevels = arm == 0 ? (int)(sizeof(dyadic)/sizeof(dyadic[0]))
                          : (int)(sizeof(inexact)/sizeof(inexact[0]));

      for( L = 0; L < nbLevels; L++ )
      {
         for( i = 0; i < 64; i++ )
            in[i] = levels[L];

         for( P = 0; P < nbPeriods; P++ )
         {
            rc = TA_RMA( 0, 63, in, periods[P], &beg, &nb, out );
            if( rc != TA_SUCCESS || beg != periods[P] - 1 || nb != 64 - periods[P] + 1 )
            {
               printf( "Fail: TA_RMA flat %g period %d retCode %d beg %d nb %d\n",
                       levels[L], periods[P], rc, (int)beg, (int)nb );
               return TA_TESTUTIL_TFRR_BAD_PARAM;
            }
            for( i = 0; i < nb; i++ )
            {
               if( !TA_IS_FINITE( out[i] ) )
               {
                  printf( "Fail: TA_RMA flat %g period %d at %d: not finite (%.17g)\n",
                          levels[L], periods[P], i, out[i] );
                  return TA_TESTUTIL_TFRR_BAD_CALCULATION;
               }
               if( arm == 0 )
               {
                  if( memcmp( &out[i], &levels[L], sizeof(double) ) != 0 )
                  {
                     printf( "Fail: TA_RMA flat %a period %d at %d: %a, expected exactly %a\n",
                             levels[L], periods[P], i, out[i], levels[L] );
                     return TA_TESTUTIL_TFRR_BAD_CALCULATION;
                  }
               }
               else if( !checkOracleValue( out[i], levels[L], 1e-15, 1e-300,
                                           &err, &mode ) )
               {
                  printf( "Fail: TA_RMA flat %g period %d at %d: %.17g (%s err %.3e)\n",
                          levels[L], periods[P], i, out[i], mode, err );
                  return TA_TESTUTIL_TFRR_BAD_CALCULATION;
               }
            }
         }
      }
   }

   return TA_TEST_PASS;
}

/* (7) ALIASING: outReal over inReal. Each bar's input is read before that
 * bar's output is written, and the output index trails the bar index, so this
 * is supported -- but only because of the statement order in the body, which a
 * rewrite that hoisted the store would break without changing any value on a
 * non-aliased call. Period 1 is in the list because that is where the write
 * index catches up with the read index.
 */
static ErrorNumber test_rma_aliasing( const TA_History *history )
{
   const int periods[] = { 1, 2, 14, 30 };
   int nbPeriods = (int)(sizeof(periods)/sizeof(periods[0]));
   static TA_Real ref[RMA_BUF_CAP];
   static TA_Real buf[RMA_BUF_CAP];
   TA_RetCode rc;
   TA_Integer refBeg, refNb, beg, nb;
   int n = (int)history->nbBars;
   int P, bad;

   for( P = 0; P < nbPeriods; P++ )
   {
      rc = TA_RMA( 0, n - 1, history->close, periods[P], &refBeg, &refNb, ref );
      if( rc != TA_SUCCESS )
      {
         printf( "Fail: TA_RMA aliasing reference period %d retCode %d\n",
                 periods[P], rc );
         return TA_TESTUTIL_TFRR_BAD_PARAM;
      }

      memcpy( buf, history->close, (size_t)n * sizeof(TA_Real) );
      rc = TA_RMA( 0, n - 1, buf, periods[P], &beg, &nb, buf );
      if( rc != TA_SUCCESS || beg != refBeg || nb != refNb )
      {
         printf( "Fail: TA_RMA aliasing period %d retCode %d beg %d nb %d\n",
                 periods[P], rc, (int)beg, (int)nb );
         return TA_TESTUTIL_TFRR_BAD_PARAM;
      }
      bad = firstBitDiff( buf, ref, (int)refNb );
      if( bad >= 0 )
      {
         printf( "Fail: TA_RMA aliasing period %d at %d: %.17g vs %.17g\n",
                 periods[P], bad, buf[bad], ref[bad] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   return TA_TEST_PASS;
}

/* (8) RANGE INDEPENDENCE. RMA is recursive, so a sub-range starts its own
 * warm-up and the class is CONVERGING with RMA's own unstable id -- the sweep
 * warms that knob to bound the legitimate difference. Measured decay at period
 * 14 on the reference corpus: the seed error is 0.168 at the first output,
 * 4.1e-3 after 50 bars and 6.2e-8 after 200, which is what CONVERGING models
 * and what EPSILON would wrongly reject.
 */
typedef struct
{
   const TA_Real *close;
} RmaRangeParam;

static TA_RetCode rmaRangeTestFunction( TA_Integer startIdx, TA_Integer endIdx,
                                        TA_Real *outputBuffer, TA_Integer *outputBufferInt,
                                        TA_Integer *outBegIdx, TA_Integer *outNbElement,
                                        TA_Integer *lookback, void *opaqueData,
                                        unsigned int outputNb, unsigned int *isOutputInteger )
{
   RmaRangeParam *p = (RmaRangeParam *)opaqueData;

   (void)outputNb;
   (void)outputBufferInt;
   *isOutputInteger = 0;

   *lookback = TA_RMA_Lookback( 14 );
   return TA_RMA( startIdx, endIdx, p->close, 14,
                  outBegIdx, outNbElement, outputBuffer );
}

static ErrorNumber test_rma_range( const TA_History *history )
{
   RmaRangeParam param;

   param.close = history->close;

   return doRangeTestEx( rmaRangeTestFunction,
                         TA_STABLE_CONVERGING, TA_FUNC_UNST_RMA,
                         (void *)&param, 1, 0 );
}
