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
 *  090526 KL     First version (issue #373).
 */

/* Description:
 *
 *   Hand-written tests for TA_HA (Heikin-Ashi Candles).
 *
 *   The generic range sweep value-compares NOTHING here: path_dependent maps
 *   to TA_DO_NOT_COMPARE and from there to TA_STABLE_SKIP, because a
 *   sub-range call legitimately disagrees with the full-history one. These
 *   legs are the value coverage.
 *
 *   (1) EXTERNAL GOLDEN, bit-exact. `ha_gold_open` / `ha_gold_close` are a
 *       frozen capture of pandas-ta-classic 0.6.52's `ha()` over the 252-bar
 *       regtest history, compared with memcmp at tolerance ZERO. Every
 *       operation in the transform is one addition or one division by an
 *       exact power of two, so there is exactly one right answer in IEEE-754
 *       doubles and a tolerance could only hide a defect.
 *
 *   (2) THE CLAMP IDENTITY over all 252 bars: HA_high is max(high, HA_open,
 *       HA_close) and HA_low is min(low, HA_open, HA_close), recomputed here
 *       from the raw bar and the FROZEN open/close. Measured: the same
 *       identity reproduces the oracle's own HA_high/HA_low columns on all
 *       252 bars, both, so this is its contract rather than an assumption --
 *       which is what lets the two clamped outputs ride on spot pins instead
 *       of two more full tables.
 *
 *   (3) HA_close IS NOT AVGPRICE. Same four terms, different summation
 *       order, and floating-point addition is not associative. Asserted to
 *       DIFFER on at least one bar: without this the "keep the order" rule
 *       in ha.c is unenforced prose, and a future composed-AVGPRICE
 *       "simplification" would pass every other leg here. The margin is
 *       thin but real -- 17 of the 252 bars differ, all by one ulp.
 *
 *   (4) EARN path_dependent, in test_cumsum.c Leg 3's shape: the flag is
 *       published; a sub-range call really does differ from the full-history
 *       one; and it is bit-identical to a full-history call over the
 *       truncated input, proving a re-anchor rather than stale state.
 *
 *   (5) EARN unstable_period, which is the OTHER half and answers a
 *       different question: spending warm-up bars must actually buy back
 *       start-independence. Measured on this history, a call anchored at bar
 *       100 needs 56 warm-up bars to become bit-identical to the
 *       full-history call over its whole emitted range; 48 still leaves two
 *       bars differing. The leg asserts both ends -- exact agreement at 64,
 *       and disagreement at 0 -- so neither the knob nor the flag is
 *       decoration.
 *
 *   (6) EDGES: single-bar ranges at both ends; a knob larger than the series
 *       (empty, TA_SUCCESS); and four-way in-place aliasing, each output
 *       buffer passed as its own input, which the read-before-store order in
 *       ha.c is what makes safe.
 *
 *   (7) MALFORMED BARS, and the clamp arms only they can reach. On a bar
 *       with low <= open,close <= high, HA_close is an average of the four
 *       and therefore already inside [low, high]: its two clamp arms are
 *       unreachable, and MEASURED, deleting them leaves legs 1-6 green on
 *       the whole 252-bar history. The four bars here are built so that each
 *       of the four arms decides one output -- high from HA_open (bar 1) and
 *       from HA_close (bar 2), low from HA_close (bar 1) and from HA_open
 *       (bar 3) -- against a bar whose high sits below its own body. Every
 *       expected value is exact in binary, and the oracle reproduces all 16
 *       bit-identically. Without this leg half the clamp is untested.
 */

/**** Headers ****/
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ta_test_priv.h"
#include "ta_test_func.h"
#include "ta_utility.h"

#define HA_N 252

static const TA_Real ha_gold_open[HA_N] =
{
   92.0, 92.0, 92.58250000000001, 93.810625,
   94.2496875, 94.38671875, 94.361484375, 93.9969921875,
   92.96349609375, 91.986748046875, 91.8646240234375, 93.30731201171875,
   95.48990600585938, 96.8037030029297, 93.83185150146485, 91.93967575073242,
   92.07983787536621, 91.64866893768311, 90.64058446884155, 90.59404223442078,
   90.81264611721039, 89.9063230586052, 88.77753652930261, 87.5450182646513,
   85.77250913232565, 84.51187956616283, 83.56843978308142, 83.2067198915407,
   85.08398494577035, 86.06949247288517, 86.64037123644259, 86.0776856182213,
   86.22259280911065, 86.34629640455532, 86.87252320227766, 87.85438660113883,
   87.91594330056941, 87.0129716502847, 85.95211082514234, 85.00793041257117,
   84.75834020628558, 84.2072951031428, 85.33052255157139, 86.72463627578568,
   88.02294313789284, 89.35584656894642, 90.06917328447321, 90.8283366422366,
   90.5472933211183, 90.31302166055914, 90.57463583027958, 90.00981791513979,
   89.11865895756989, 87.75870447878495, 85.93060223939247, 84.46592611969623,
   84.38921305984812, 85.07335652992406, 85.65417826496203, 86.95271413248102,
   88.08198206624051, 88.74411603312026, 88.54018301656014, 89.61009150828008,
   90.74692075414004, 91.44783537707002, 92.184542688535, 92.5922713442675,
   92.02301067213375, 91.50775533606688, 90.98137766803345, 89.88131383401672,
   88.41003191700835, 86.33001595850418, 85.0087579792521, 84.91937898962604,
   91.40906449481302, 95.24390724740651, 99.35257862370325, 102.66066431185163,
   103.09595715592582, 102.93860357796291, 103.47742678898146, 104.16433839449073,
   105.30154419724536, 105.40514709862268, 105.39819854931133, 106.26534927465566,
   107.80892463732783, 108.95946231866392, 110.14035615933196, 115.07017807966598,
   117.550714039833, 117.8459820199165, 118.57174100995826, 118.40712050497913,
   117.79043525248957, 116.75146762624479, 115.0319838131224, 113.1841169065612,
   113.9245584532806, 114.7347792266403, 115.23488961332015, 113.95244480666008,
   113.17122240333003, 113.38186120166502, 113.94843060083251, 116.70046530041625,
   117.40273265020812, 117.58261632510406, 116.43880816255202, 115.523154081276,
   115.432827040638, 116.020163520319, 118.1263317601595, 119.11816588007974,
   119.82533294003987, 121.91141647001993, 123.20445823500997, 122.99347911750499,
   122.9567395587525, 123.39086977937626, 123.37543488968814, 123.64771744484406,
   125.62760872242202, 127.852554361211, 129.5362771806055, 130.79188859030276,
   131.38844429515137, 132.46047214757567, 134.06773607378784, 135.6988680368939,
   136.42068401844693, 136.99159200922347, 136.86329600461175, 136.62039800230588,
   136.27269900115294, 132.94134950057648, 130.61067475028824, 128.24158737514412,
   126.22954368757206, 124.92602184378603, 125.12676092189301, 126.2196304609465,
   125.96731523047325, 125.81990761523662, 124.6674538076183, 122.76247690380916,
   121.18498845190459, 121.1862442259523, 122.28062211297615, 122.56156105648807,
   121.56203052824404, 121.65476526412202, 122.03738263206101, 122.59619131603051,
   124.39059565801526, 126.21029782900763, 126.27764891450381, 124.75507445725191,
   123.82128722862595, 123.70689361431297, 123.10219680715649, 122.71359840357825,
   123.04304920178913, 123.63902460089457, 123.78826230044729, 124.11913115022364,
   125.41831557511182, 125.63040778755591, 127.18895389377795, 128.838226946889,
   130.2791134734445, 132.06205673672224, 134.1022783683611, 133.87238918418055,
   133.28744459209025, 133.2537222960451, 132.11936114802256, 130.0521805740113,
   129.21484028700564, 128.57617014350282, 127.28808507175141, 125.98779253587571,
   124.36139626793786, 124.40569813396894, 123.82034906698448, 122.52642453349225,
   121.74696226674612, 120.28723113337307, 119.47111556668654, 120.52430778334326,
   120.66840389167163, 119.05170194583582, 116.35335097291791, 115.37167548645895,
   113.79458774322947, 110.86479387161474, 108.61239693580737, 107.68744846790369,
   107.42997423395184, 107.22248711697591, 108.07124355848796, 99.78562177924398,
   96.74406088962199, 95.29203044481099, 95.2697652224055, 94.74363261120274,
   94.74681630560137, 96.18590815280069, 96.89670407640034, 96.20585203820016,
   95.56292601910008, 94.08646300955004, 92.80948150477502, 92.51474075238751,
   93.29487037619376, 94.49743518809689, 95.35746759404844, 95.31873379702422,
   95.19686689851211, 94.75343344925605, 94.39046672462803, 95.171483362314,
   98.38949168115701, 102.4759958405785, 105.08049792028925, 104.57899896014462,
   104.92199948007232, 104.64099974003616, 103.99174987001808, 103.57962493500904,
   104.08856246750452, 107.35553123375226, 110.92776561687613, 113.99513280843806,
   115.87631640421903, 116.46815820210952, 113.24907910105476, 111.26578955052739,
   110.1953947752637, 108.49644738763185, 108.32697369381592, 109.20973684690796,
   109.16111842345398, 109.180559211727, 109.1127796058635, 109.10263980293175,
   109.24631990146588, 109.52065995073295, 109.59657997536647, 109.48578998768323
};

static const TA_Real ha_gold_close[HA_N] =
{
   92.0, 93.165, 95.03875, 94.68875,
   94.52375, 94.33625, 93.6325, 91.93,
   91.01, 91.7425, 94.75, 97.6725,
   98.1175, 90.86, 90.0475, 92.22,
   91.2175, 89.6325, 90.5475, 91.03125,
   89.0, 87.64875, 86.3125, 84.0,
   83.25125, 82.625, 82.845, 86.96125,
   87.055, 87.21125, 85.515, 86.3675,
   86.47, 87.39875, 88.83625, 87.97749999999999,
   86.11, 84.89125, 84.06375, 84.50874999999999,
   83.65625, 86.45375, 88.11874999999999, 89.32125,
   90.68875, 90.7825, 91.58749999999999, 90.26625,
   90.07875, 90.83625, 89.445, 88.2275,
   86.39875, 84.10249999999999, 83.00125, 84.3125,
   85.7575, 86.235, 88.25125, 89.21125,
   89.40625, 88.33625, 90.68, 91.88374999999999,
   92.14875, 92.92125, 93.0, 91.45375000000001,
   90.9925, 90.455, 88.78125, 86.93875,
   84.25, 83.6875, 84.83, 97.89875,
   99.07875, 103.46124999999999, 105.96875, 103.53125,
   102.78125, 104.01625000000001, 104.85125, 106.43875,
   105.50875, 105.39125, 107.1325, 109.3525,
   110.11, 111.32124999999999, 120.0, 120.03125,
   118.14125, 119.2975, 118.24249999999999, 117.17375,
   115.71249999999999, 113.3125, 111.33625, 114.66499999999999,
   115.545, 115.735, 112.67, 112.39,
   113.5925, 114.515, 119.4525, 118.105,
   117.7625, 115.295, 114.6075, 115.3425,
   116.6075, 120.2325, 120.11, 120.5325,
   123.9975, 124.4975, 122.7825, 122.92,
   123.825, 123.36, 123.92, 127.6075,
   130.0775, 131.22, 132.0475, 131.985,
   133.5325, 135.675, 137.32999999999998, 137.14249999999998,
   137.5625, 136.735, 136.3775, 135.925,
   129.61, 128.28, 125.8725, 124.2175,
   123.6225, 125.3275, 127.3125, 125.715,
   125.6725, 123.515, 120.8575, 119.6075,
   121.1875, 123.375, 122.8425, 120.5625,
   121.7475, 122.42, 123.155, 126.185,
   128.03, 126.345, 123.2325, 122.8875,
   123.5925, 122.4975, 122.325, 123.3725,
   124.235, 123.9375, 124.45, 126.7175,
   125.8425, 128.7475, 130.4875, 131.72,
   133.845, 136.14249999999998, 133.64249999999998, 132.7025,
   133.22, 130.985, 127.985, 128.3775,
   127.9375, 126.0, 124.6875, 122.735,
   124.45, 123.235, 121.2325, 120.9675,
   118.8275, 118.655, 121.5775, 120.8125,
   117.435, 113.655, 114.39, 112.2175,
   107.935, 106.36, 106.7625, 107.1725,
   107.015, 108.92, 91.5, 93.7025,
   93.84, 95.2475, 94.2175, 94.75,
   97.625, 97.6075, 95.515, 94.92,
   92.61, 91.5325, 92.22, 94.075,
   95.7, 96.2175, 95.28, 95.075,
   94.31, 94.0275, 95.9525, 101.6075,
   106.5625, 107.685, 104.0775, 105.265,
   104.36, 103.3425, 103.1675, 104.5975,
   110.6225, 114.5, 117.0625, 117.7575,
   117.06, 110.03, 109.2825, 109.125,
   106.7975, 108.1575, 110.0925, 109.1125,
   109.2, 109.045, 109.0925, 109.39,
   109.795, 109.6725, 109.375, 108.295
};

/* Spot pins on the two clamped outputs, bars 0..7 and the final bar. */
static const TA_Real ha_gold_high_first8[8] =
{
   93.25, 94.94, 96.375, 96.19, 96.0, 94.72, 95.0, 93.9969921875
};
static const TA_Real ha_gold_low_first8[8] =
{
   90.75, 91.405, 92.58250000000001, 93.5, 92.815, 93.5, 92.0, 89.75
};
#define HA_GOLD_HIGH_LAST 109.5
#define HA_GOLD_LOW_LAST  106.62

/* Malformed-bar corpus: bars 1..3 each carry a high below their own body, or
 * a low above it, which is the only way a raw extreme loses to the synthetic
 * body. Exact in binary; the values are the oracle's, bit for bit. */
#define HA_MAL 4
static const TA_Real mal_open[HA_MAL]  = { 100.0, 100.0, 1000.0, 1000.0 };
static const TA_Real mal_high[HA_MAL]  = { 100.0,   1.0,    1.0, 1000.0 };
static const TA_Real mal_low[HA_MAL]   = { 100.0,  99.0,    1.0,  999.0 };
static const TA_Real mal_close[HA_MAL] = { 100.0, 100.0, 1000.0, 1000.0 };

static const TA_Real mal_exp_open[HA_MAL]  = { 100.0, 100.0,   87.5,  294.0 };
static const TA_Real mal_exp_high[HA_MAL]  = { 100.0, 100.0,  500.5, 1000.0 };
static const TA_Real mal_exp_low[HA_MAL]   = { 100.0,  75.0,    1.0,  294.0 };
static const TA_Real mal_exp_close[HA_MAL] = { 100.0,  75.0,  500.5,  999.75 };

/* Restore the knob before every exit: it is process-global state, and a
 * leaked non-zero value would silently change every later HA call. */
static ErrorNumber ha_restore( ErrorNumber e )
{
   TA_SetUnstablePeriod( TA_FUNC_UNST_HA, 0 );
   return e;
}

ErrorNumber test_func_ha( TA_History *history )
{
   static TA_Real ao[HA_N], ah[HA_N], al[HA_N], ac[HA_N];
   static TA_Real bo[HA_N], bh[HA_N], bl[HA_N], bc[HA_N];
   static TA_Real ko[HA_N], kh[HA_N], kl[HA_N], kc[HA_N];
   static TA_Real io[HA_N], ih[HA_N], il[HA_N], ic[HA_N];
   const TA_Real *rawO, *rawH, *rawL, *rawC;
   TA_RetCode rc;
   TA_Integer beg, nb, beg2, nb2;
   const TA_FuncHandle *handle;
   const TA_FuncInfo *funcInfo;
   TA_Real expHigh, expLow;
   int i, differs;

   if( history->nbBars < HA_N )
   {
      printf( "HA Fail: history carries %d bars, the golden capture needs %d\n",
              (int)history->nbBars, HA_N );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }
   rawO = history->open;
   rawH = history->high;
   rawL = history->low;
   rawC = history->close;

   /* (1) External golden, full range, tolerance zero. */
   rc = TA_HA( 0, HA_N - 1, rawO, rawH, rawL, rawC, &beg, &nb, ao, ah, al, ac );
   if( rc != TA_SUCCESS || beg != 0 || nb != HA_N )
   {
      printf( "HA Fail: retCode %d range (%d,%d), expected (0,%d)\n",
              (int)rc, (int)beg, (int)nb, HA_N );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   for( i = 0; i < HA_N; i++ )
   {
      if( memcmp( &ao[i], &ha_gold_open[i], sizeof(TA_Real) ) != 0 )
      {
         printf( "HA Fail open bar %d: %.17g != oracle %.17g\n",
                 i, ao[i], ha_gold_open[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
      if( memcmp( &ac[i], &ha_gold_close[i], sizeof(TA_Real) ) != 0 )
      {
         printf( "HA Fail close bar %d: %.17g != oracle %.17g\n",
                 i, ac[i], ha_gold_close[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }
   for( i = 0; i < 8; i++ )
   {
      if( ah[i] != ha_gold_high_first8[i] || al[i] != ha_gold_low_first8[i] )
      {
         printf( "HA Fail clamped pin bar %d: (%.17g,%.17g) != oracle (%.17g,%.17g)\n",
                 i, ah[i], al[i], ha_gold_high_first8[i], ha_gold_low_first8[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }
   if( ah[HA_N-1] != HA_GOLD_HIGH_LAST || al[HA_N-1] != HA_GOLD_LOW_LAST )
   {
      printf( "HA Fail clamped pin final bar: (%.17g,%.17g) != oracle (%.17g,%.17g)\n",
              ah[HA_N-1], al[HA_N-1], (TA_Real)HA_GOLD_HIGH_LAST, (TA_Real)HA_GOLD_LOW_LAST );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   /* (2) The clamp identity, every bar, against the FROZEN open/close. */
   for( i = 0; i < HA_N; i++ )
   {
      expHigh = rawH[i];
      if( ha_gold_open[i]  > expHigh ) expHigh = ha_gold_open[i];
      if( ha_gold_close[i] > expHigh ) expHigh = ha_gold_close[i];
      expLow = rawL[i];
      if( ha_gold_open[i]  < expLow ) expLow = ha_gold_open[i];
      if( ha_gold_close[i] < expLow ) expLow = ha_gold_close[i];
      if( ah[i] != expHigh || al[i] != expLow )
      {
         printf( "HA Fail clamp bar %d: (%.17g,%.17g) != (%.17g,%.17g)\n",
                 i, ah[i], al[i], expHigh, expLow );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* (3) HA_close is not AVGPRICE: same terms, different order. */
   rc = TA_AVGPRICE( 0, HA_N - 1, rawO, rawH, rawL, rawC, &beg2, &nb2, bo );
   if( rc != TA_SUCCESS || nb2 != HA_N )
   {
      printf( "HA Fail: TA_AVGPRICE retCode %d nb %d\n", (int)rc, (int)nb2 );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   differs = 0;
   for( i = 0; i < HA_N; i++ )
   {
      if( memcmp( &ac[i], &bo[i], sizeof(TA_Real) ) != 0 )
         differs++;
   }
   if( differs == 0 )
   {
      printf( "HA Fail: HA_close is bit-identical to TA_AVGPRICE on all %d bars.\n"
              "       The summation order in ha.c is then unenforced -- either the\n"
              "       corpus stopped discriminating or the order was changed.\n", HA_N );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   /* (4) Earn path_dependent. */
   if( TA_GetFuncHandle( "HA", &handle ) != TA_SUCCESS ||
       TA_GetFuncInfo( handle, &funcInfo ) != TA_SUCCESS )
   {
      printf( "HA Fail: cannot read its own TA_FuncInfo\n" );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   if( !(funcInfo->flags & TA_FUNC_FLG_PATH_DEP) )
   {
      printf( "HA Fail: TA_FUNC_FLG_PATH_DEP is not published -- the range sweep\n"
              "       would value-compare a re-anchoring function\n" );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }
   if( !(funcInfo->flags & TA_FUNC_FLG_UNST_PER) )
   {
      printf( "HA Fail: TA_FUNC_FLG_UNST_PER is not published -- the warm-up knob\n"
              "       that buys back start-independence would not exist\n" );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }
   rc = TA_HA( 100, HA_N - 1, rawO, rawH, rawL, rawC, &beg2, &nb2, bo, bh, bl, bc );
   if( rc != TA_SUCCESS || beg2 != 100 || nb2 != HA_N - 100 )
   {
      printf( "HA Fail sub-range: retCode %d range (%d,%d)\n", (int)rc, (int)beg2, (int)nb2 );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   if( memcmp( &bo[0], &ao[100], sizeof(TA_Real) ) == 0 )
   {
      printf( "HA Fail: the sub-range open agrees with full history (%.17g) at the\n"
              "       anchor -- the path_dependent flag disables a gate for nothing\n",
              bo[0] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }
   /* A re-anchor, not stale state: identical to a full call over the tail. */
   rc = TA_HA( 0, HA_N - 101, rawO + 100, rawH + 100, rawL + 100, rawC + 100,
               &beg, &nb, ko, kh, kl, kc );
   if( rc != TA_SUCCESS || nb != nb2 ||
       memcmp( ko, bo, (size_t)nb * sizeof(TA_Real) ) != 0 ||
       memcmp( kh, bh, (size_t)nb * sizeof(TA_Real) ) != 0 ||
       memcmp( kl, bl, (size_t)nb * sizeof(TA_Real) ) != 0 ||
       memcmp( kc, bc, (size_t)nb * sizeof(TA_Real) ) != 0 )
   {
      printf( "HA Fail: the sub-range call is not a re-anchor (nb %d vs %d)\n",
              (int)nb2, (int)nb );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   /* (5) Earn unstable_period: warm-up buys back start-independence. */
   if( TA_SetUnstablePeriod( TA_FUNC_UNST_HA, 64 ) != TA_SUCCESS )
   {
      printf( "HA Fail: TA_SetUnstablePeriod rejected 64\n" );
      return ha_restore( TA_TESTUTIL_TFRR_BAD_RETCODE );
   }
   rc = TA_HA( 100, HA_N - 1, rawO, rawH, rawL, rawC, &beg2, &nb2, ko, kh, kl, kc );
   if( rc != TA_SUCCESS || beg2 != 100 || nb2 != HA_N - 100 )
   {
      printf( "HA Fail warm-up call: retCode %d range (%d,%d)\n",
              (int)rc, (int)beg2, (int)nb2 );
      return ha_restore( TA_TESTUTIL_TFRR_BAD_RETCODE );
   }
   for( i = 0; i < nb2; i++ )
   {
      if( memcmp( &ko[i], &ao[100+i], sizeof(TA_Real) ) != 0 ||
          memcmp( &kc[i], &ac[100+i], sizeof(TA_Real) ) != 0 )
      {
         printf( "HA Fail: 64 warm-up bars did not converge at bar %d: open %.17g vs\n"
                 "       %.17g. Measured on this history, 56 bars suffice.\n",
                 100 + i, ko[i], ao[100+i] );
         return ha_restore( TA_TESTUTIL_TFRR_BAD_CALCULATION );
      }
   }
   /* The lookback IS the knob, and a knob past the series empties the call. */
   if( TA_HA_Lookback() != 64 )
   {
      printf( "HA Fail: lookback %d with the knob at 64\n", (int)TA_HA_Lookback() );
      return ha_restore( TA_TESTUTIL_TFRR_BAD_CALCULATION );
   }
   if( TA_SetUnstablePeriod( TA_FUNC_UNST_HA, HA_N + 10 ) != TA_SUCCESS )
   {
      printf( "HA Fail: TA_SetUnstablePeriod rejected a large knob\n" );
      return ha_restore( TA_TESTUTIL_TFRR_BAD_RETCODE );
   }
   rc = TA_HA( 0, HA_N - 1, rawO, rawH, rawL, rawC, &beg2, &nb2, ko, kh, kl, kc );
   if( rc != TA_SUCCESS || beg2 != 0 || nb2 != 0 )
   {
      printf( "HA Fail empty: retCode %d range (%d,%d), expected TA_SUCCESS (0,0)\n",
              (int)rc, (int)beg2, (int)nb2 );
      return ha_restore( TA_TESTUTIL_TFRR_BAD_RETCODE );
   }
   (void)ha_restore( TA_TEST_PASS );

   /* (6) Edges and four-way in-place aliasing. */
   rc = TA_HA( 0, 0, rawO, rawH, rawL, rawC, &beg, &nb, bo, bh, bl, bc );
   if( rc != TA_SUCCESS || beg != 0 || nb != 1 ||
       memcmp( &bo[0], &ha_gold_open[0], sizeof(TA_Real) ) != 0 ||
       memcmp( &bc[0], &ha_gold_close[0], sizeof(TA_Real) ) != 0 )
   {
      printf( "HA Fail edge (0,0)\n" );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }
   rc = TA_HA( HA_N - 1, HA_N - 1, rawO, rawH, rawL, rawC, &beg, &nb, bo, bh, bl, bc );
   if( rc != TA_SUCCESS || beg != HA_N - 1 || nb != 1 )
   {
      printf( "HA Fail edge (N-1,N-1): retCode %d range (%d,%d)\n",
              (int)rc, (int)beg, (int)nb );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   /* The anchor bar seeds from its own open and close. */
   if( bo[0] != ( rawO[HA_N-1] + rawC[HA_N-1] ) / 2.0 )
   {
      printf( "HA Fail edge (N-1,N-1): open %.17g is not the anchor bar's own seed\n",
              bo[0] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }
   for( i = 0; i < HA_N; i++ )
   {
      io[i] = rawO[i];
      ih[i] = rawH[i];
      il[i] = rawL[i];
      ic[i] = rawC[i];
   }
   rc = TA_HA( 0, HA_N - 1, io, ih, il, ic, &beg, &nb, io, ih, il, ic );
   if( rc != TA_SUCCESS || nb != HA_N ||
       memcmp( io, ao, HA_N * sizeof(TA_Real) ) != 0 ||
       memcmp( ih, ah, HA_N * sizeof(TA_Real) ) != 0 ||
       memcmp( il, al, HA_N * sizeof(TA_Real) ) != 0 ||
       memcmp( ic, ac, HA_N * sizeof(TA_Real) ) != 0 )
   {
      printf( "HA Fail: the four-way in-place call differs from the separate-buffer one\n" );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   /* (7) Malformed bars: the clamp arms a well-formed bar cannot reach. */
   rc = TA_HA( 0, HA_MAL - 1, mal_open, mal_high, mal_low, mal_close,
               &beg, &nb, bo, bh, bl, bc );
   if( rc != TA_SUCCESS || beg != 0 || nb != HA_MAL )
   {
      printf( "HA Fail malformed: retCode %d range (%d,%d)\n",
              (int)rc, (int)beg, (int)nb );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   for( i = 0; i < HA_MAL; i++ )
   {
      if( bo[i] != mal_exp_open[i]  || bh[i] != mal_exp_high[i] ||
          bl[i] != mal_exp_low[i]   || bc[i] != mal_exp_close[i] )
      {
         printf( "HA Fail malformed bar %d: (%.17g,%.17g,%.17g,%.17g) != "
                 "(%.17g,%.17g,%.17g,%.17g)\n", i,
                 bo[i], bh[i], bl[i], bc[i],
                 mal_exp_open[i], mal_exp_high[i], mal_exp_low[i], mal_exp_close[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   return TA_TEST_PASS;
}
