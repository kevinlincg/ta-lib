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
 *   The generic --codegen sweep value-compares NOTHING here: the frozen
 *   ta_ref_serve predates the function, so it can only run the self-comparing
 *   float leg. These legs plus --xlang-hash are the value coverage.
 *
 *   (1) EXTERNAL GOLDEN, bit-exact. All four outputs over the 252-bar SREF
 *       OHLC, captured from pandas-ta-classic 0.6.52 (CPython 3.11, pandas
 *       3.0.5) via `df.ta.ha()`. Every operation in the recurrence is an
 *       addition or a division by an exact power of two, performed in the same
 *       order, so equality is the assertion a tolerance would only weaken --
 *       and memcmp, not ==, so a signed-zero divergence cannot pass as equal.
 *
 *   (2) THE SEED, on a corpus that can see it. On SREF the two published seed
 *       conventions -- HA_open[0] = (O+C)/2, versus ta4j emitting the raw bar
 *       -- produce bit-identical output from bar 1 onward, because SREF bar 0
 *       happens to satisfy open + close == high + low (92.5 + 91.5 ==
 *       93.25 + 90.75). Leg 1 therefore cannot discriminate them past bar 0.
 *       S12 is a 12-bar corpus whose first bar violates that identity; its
 *       golden comes from the same pandas capture, and the leg carries its own
 *       non-vacuity control: an in-test raw-bar seed run over S12 must DIFFER
 *       at every bar from 1 on, or the corpus is not doing its job.
 *
 *   (3) THE UNSTABLE PERIOD is a warm-up, not a different answer. With k set,
 *       a call anchored at bar m seeds the recurrence at m - k, and the result
 *       converges on the full-history value as k grows. MEASURED on this
 *       corpus: k = 54 is the smallest warm-up that reproduces the full-history
 *       HA_open bit-for-bit at EVERY bar it can be asked for; k = 10 does not,
 *       which is the control that keeps the k = 54 assertion non-vacuous.
 *       Also pinned: outBegIdx == k, and k = 0 emitting the seed at bar 0.
 *
 *   (4) EDGES: a single-bar call at the anchor; an all-flat OHLC window, where
 *       every output must be exactly the flat price (no epsilon -- the whole
 *       computation is exact there); and all four in-place calls, each output
 *       in turn handed the input it is most likely to clobber, which is the
 *       aliasing case this body's tempHigh/tempLow exist for.
 *
 *   (5) HA_close IS NOT TA_AVGPRICE -- same four terms, different summation
 *       order, and floating-point addition does not associate. Asserted to
 *       differ on at least one bar and to agree to within rounding on every
 *       bar, so the leg fails both if the two are unified and if they ever
 *       drift apart for a real reason.
 */

/**** Headers ****/
#include <stdio.h>
#include <string.h>

#include "ta_test_priv.h"
#include "ta_test_func.h"
#include "ta_utility.h"
#include "server_verify.h"

/**** Local declarations. ****/
#define HA_SREF_BARS  252
#define HA_S12_BARS   12
#define HA_OUT_CAP    512

/* Comparison counters, printed at the end so a leg that silently stops
 * comparing anything is visible rather than green. */
static int g_haGoldenCmp;
static int g_haSeedCmp;
static int g_haWarmupCmp;

static int ha_bits_differ( double a, double b )
{
   return memcmp( &a, &b, sizeof(double) ) != 0;
}

/* GOLDEN: pandas-ta-classic 0.6.52 `df.ta.ha()` over the 252-bar SREF OHLC
 * (TA_SREF_{open,high,low,close}_daily_ref_0_PRIV, which is what the history
 * argument carries), printed at full precision so each literal round-trips to
 * the exact double pandas held. */
static const TA_Real haSref_Open[] =
{
   92.0, 92.0, 92.58250000000001, 93.810625, 94.2496875,
   94.38671875, 94.361484375, 93.9969921875, 92.96349609375, 91.986748046875,
   91.8646240234375, 93.30731201171875, 95.48990600585938, 96.8037030029297, 93.83185150146485,
   91.93967575073242, 92.07983787536621, 91.64866893768311, 90.64058446884155, 90.59404223442078,
   90.81264611721039, 89.9063230586052, 88.77753652930261, 87.5450182646513, 85.77250913232565,
   84.51187956616283, 83.56843978308142, 83.2067198915407, 85.08398494577035, 86.06949247288517,
   86.64037123644259, 86.0776856182213, 86.22259280911065, 86.34629640455532, 86.87252320227766,
   87.85438660113883, 87.91594330056941, 87.0129716502847, 85.95211082514234, 85.00793041257117,
   84.75834020628558, 84.2072951031428, 85.33052255157139, 86.72463627578568, 88.02294313789284,
   89.35584656894642, 90.06917328447321, 90.8283366422366, 90.5472933211183, 90.31302166055914,
   90.57463583027958, 90.00981791513979, 89.11865895756989, 87.75870447878495, 85.93060223939247,
   84.46592611969623, 84.38921305984812, 85.07335652992406, 85.65417826496203, 86.95271413248102,
   88.08198206624051, 88.74411603312026, 88.54018301656014, 89.61009150828008, 90.74692075414004,
   91.44783537707002, 92.184542688535, 92.5922713442675, 92.02301067213375, 91.50775533606688,
   90.98137766803345, 89.88131383401672, 88.41003191700835, 86.33001595850418, 85.0087579792521,
   84.91937898962604, 91.40906449481302, 95.24390724740651, 99.35257862370325, 102.66066431185163,
   103.09595715592582, 102.93860357796291, 103.47742678898146, 104.16433839449073, 105.30154419724536,
   105.40514709862268, 105.39819854931133, 106.26534927465566, 107.80892463732783, 108.95946231866392,
   110.14035615933196, 115.07017807966598, 117.550714039833, 117.8459820199165, 118.57174100995826,
   118.40712050497913, 117.79043525248957, 116.75146762624479, 115.0319838131224, 113.1841169065612,
   113.9245584532806, 114.7347792266403, 115.23488961332015, 113.95244480666008, 113.17122240333003,
   113.38186120166502, 113.94843060083251, 116.70046530041625, 117.40273265020812, 117.58261632510406,
   116.43880816255202, 115.523154081276, 115.432827040638, 116.020163520319, 118.1263317601595,
   119.11816588007974, 119.82533294003987, 121.91141647001993, 123.20445823500997, 122.99347911750499,
   122.9567395587525, 123.39086977937626, 123.37543488968814, 123.64771744484406, 125.62760872242202,
   127.852554361211, 129.5362771806055, 130.79188859030276, 131.38844429515137, 132.46047214757567,
   134.06773607378784, 135.6988680368939, 136.42068401844693, 136.99159200922347, 136.86329600461175,
   136.62039800230588, 136.27269900115294, 132.94134950057648, 130.61067475028824, 128.24158737514412,
   126.22954368757206, 124.92602184378603, 125.12676092189301, 126.2196304609465, 125.96731523047325,
   125.81990761523662, 124.6674538076183, 122.76247690380916, 121.18498845190459, 121.1862442259523,
   122.28062211297615, 122.56156105648807, 121.56203052824404, 121.65476526412202, 122.03738263206101,
   122.59619131603051, 124.39059565801526, 126.21029782900763, 126.27764891450381, 124.75507445725191,
   123.82128722862595, 123.70689361431297, 123.10219680715649, 122.71359840357825, 123.04304920178913,
   123.63902460089457, 123.78826230044729, 124.11913115022364, 125.41831557511182, 125.63040778755591,
   127.18895389377795, 128.838226946889, 130.2791134734445, 132.06205673672224, 134.1022783683611,
   133.87238918418055, 133.28744459209025, 133.2537222960451, 132.11936114802256, 130.0521805740113,
   129.21484028700564, 128.57617014350282, 127.28808507175141, 125.98779253587571, 124.36139626793786,
   124.40569813396894, 123.82034906698448, 122.52642453349225, 121.74696226674612, 120.28723113337307,
   119.47111556668654, 120.52430778334326, 120.66840389167163, 119.05170194583582, 116.35335097291791,
   115.37167548645895, 113.79458774322947, 110.86479387161474, 108.61239693580737, 107.68744846790369,
   107.42997423395184, 107.22248711697591, 108.07124355848796, 99.78562177924398, 96.74406088962199,
   95.29203044481099, 95.2697652224055, 94.74363261120274, 94.74681630560137, 96.18590815280069,
   96.89670407640034, 96.20585203820016, 95.56292601910008, 94.08646300955004, 92.80948150477502,
   92.51474075238751, 93.29487037619376, 94.49743518809689, 95.35746759404844, 95.31873379702422,
   95.19686689851211, 94.75343344925605, 94.39046672462803, 95.171483362314, 98.38949168115701,
   102.4759958405785, 105.08049792028925, 104.57899896014462, 104.92199948007232, 104.64099974003616,
   103.99174987001808, 103.57962493500904, 104.08856246750452, 107.35553123375226, 110.92776561687613,
   113.99513280843806, 115.87631640421903, 116.46815820210952, 113.24907910105476, 111.26578955052739,
   110.1953947752637, 108.49644738763185, 108.32697369381592, 109.20973684690796, 109.16111842345398,
   109.180559211727, 109.1127796058635, 109.10263980293175, 109.24631990146588, 109.52065995073295,
   109.59657997536647, 109.48578998768323
};

static const TA_Real haSref_High[] =
{
   93.25, 94.94, 96.375, 96.19, 96.0,
   94.72, 95.0, 93.9969921875, 92.96349609375, 92.75,
   96.25, 99.625, 99.125, 96.8037030029297, 93.83185150146485,
   93.25, 93.405, 91.64866893768311, 91.97, 92.25,
   90.81264611721039, 89.9063230586052, 88.77753652930261, 87.5450182646513, 85.77250913232565,
   84.75, 84.44, 89.405, 88.125, 89.125,
   87.155, 87.25, 87.375, 88.97, 90.0,
   89.845, 87.91594330056941, 87.0129716502847, 85.95211082514234, 85.47,
   84.75834020628558, 88.5, 89.47, 90.0, 92.44,
   91.44, 92.97, 91.72, 91.155, 91.75,
   90.57463583027958, 90.00981791513979, 89.11865895756989, 87.75870447878495, 85.93060223939247,
   85.25, 86.625, 87.94, 89.375, 90.625,
   90.75, 88.845, 91.97, 93.375, 93.815,
   94.03, 94.03, 92.5922713442675, 92.02301067213375, 91.94,
   90.98137766803345, 89.88131383401672, 88.41003191700835, 86.33001595850418, 85.94,
   99.375, 103.28, 105.375, 107.625, 105.25,
   104.5, 105.5, 106.125, 107.94, 106.25,
   107.0, 108.75, 110.94, 110.94, 114.22,
   123.0, 121.75, 119.815, 120.315, 119.375,
   118.40712050497913, 117.79043525248957, 116.75146762624479, 115.0319838131224, 118.315,
   116.87, 116.75, 115.23488961332015, 114.62, 115.31,
   116.0, 121.69, 119.87, 120.87, 117.58261632510406,
   116.5, 116.0, 118.31, 121.5, 122.0,
   121.44, 125.75, 127.75, 124.19, 124.44,
   125.75, 124.69, 125.31, 132.0, 131.31,
   132.25, 133.88, 133.5, 135.5, 137.44,
   138.69, 139.19, 138.5, 138.13, 137.5,
   138.88, 136.27269900115294, 132.94134950057648, 130.61067475028824, 128.24158737514412,
   126.22954368757206, 126.5, 128.69, 126.62, 126.69,
   126.0, 124.6674538076183, 122.76247690380916, 124.0, 127.0,
   124.44, 122.56156105648807, 123.75, 123.81, 124.5,
   127.87, 128.56, 129.63, 126.27764891450381, 124.75507445725191,
   124.87, 123.70689361431297, 124.06, 125.87, 125.19,
   125.62, 126.0, 128.5, 126.75, 129.75,
   132.69, 133.94, 136.5, 137.69, 135.56,
   133.87238918418055, 135.0, 133.2537222960451, 132.11936114802256, 130.88,
   129.63, 128.57617014350282, 127.81, 125.98779253587571, 126.81,
   124.75, 123.82034906698448, 122.52642453349225, 121.74696226674612, 120.28723113337307,
   123.25, 122.75, 120.66840389167163, 119.05170194583582, 116.69,
   115.37167548645895, 113.79458774322947, 110.86479387161474, 108.87, 109.0,
   108.5, 113.06, 108.07124355848796, 99.78562177924398, 96.74406088962199,
   96.0, 95.56, 95.31, 99.0, 98.81,
   96.89670407640034, 96.20585203820016, 95.56292601910008, 94.08646300955004, 93.94,
   95.5, 97.06, 97.5, 96.25, 96.37,
   95.19686689851211, 94.87, 98.25, 105.12, 108.44,
   109.87, 105.08049792028925, 106.0, 104.94, 104.64099974003616,
   104.44, 106.31, 112.87, 116.5, 119.19,
   121.0, 122.12, 116.46815820210952, 113.24907910105476, 111.26578955052739,
   110.1953947752637, 109.69, 111.06, 110.44, 110.12,
   110.31, 110.44, 110.0, 110.75, 110.5,
   110.5, 109.5
};

static const TA_Real haSref_Low[] =
{
   90.75, 91.405, 92.58250000000001, 93.5, 92.815,
   93.5, 92.0, 89.75, 89.44, 90.625,
   91.8646240234375, 93.30731201171875, 95.48990600585938, 88.815, 86.75,
   90.94, 88.905, 88.78, 89.25, 89.75,
   87.5, 86.53, 84.625, 82.28, 81.565,
   80.875, 81.25, 83.2067198915407, 85.08398494577035, 85.97,
   84.405, 85.095, 85.5, 85.53, 86.87252320227766,
   86.565, 84.655, 83.25, 82.565, 83.44,
   82.53, 84.2072951031428, 85.33052255157139, 86.72463627578568, 88.02294313789284,
   89.35584656894642, 90.06917328447321, 89.0, 88.565, 90.095,
   89.0, 86.47, 84.0, 83.315, 82.0,
   83.25, 84.38921305984812, 85.07335652992406, 85.65417826496203, 86.95271413248102,
   88.08198206624051, 87.345, 88.54018301656014, 89.61009150828008, 89.53,
   91.155, 92.0, 90.53, 89.97, 88.815,
   86.75, 85.065, 82.03, 81.5, 82.565,
   84.91937898962604, 91.40906449481302, 95.24390724740651, 99.35257862370325, 101.75,
   101.72, 101.72, 103.155, 104.16433839449073, 103.655,
   104.0, 105.39819854931133, 106.26534927465566, 107.80892463732783, 107.75,
   110.14035615933196, 115.07017807966598, 116.0, 117.8459820199165, 116.53,
   116.25, 114.595, 110.875, 110.5, 110.72,
   112.62, 114.19, 111.19, 109.44, 111.56,
   112.44, 113.94843060083251, 116.06, 116.56, 113.31,
   112.56, 114.0, 114.75, 116.020163520319, 118.1263317601595,
   119.11816588007974, 119.82533294003987, 121.91141647001993, 121.75, 121.56,
   122.9567395587525, 122.19, 122.75, 123.64771744484406, 125.62760872242202,
   127.852554361211, 129.5362771806055, 130.63, 131.38844429515137, 132.46047214757567,
   134.06773607378784, 135.6988680368939, 136.19, 134.5, 135.38,
   133.69, 126.06, 126.87, 123.5, 122.62,
   122.75, 123.56, 125.12676092189301, 124.62, 124.37,
   121.81, 118.19, 118.06, 117.56, 121.0,
   121.12, 118.94, 119.81, 121.0, 122.0,
   122.59619131603051, 124.39059565801526, 123.5, 121.25, 121.06,
   122.31, 121.0, 120.87, 122.06, 122.75,
   122.69, 122.87, 124.11913115022364, 124.25, 125.63040778755591,
   127.18895389377795, 128.838226946889, 130.2791134734445, 132.06205673672224, 132.0,
   131.94, 131.94, 129.56, 123.75, 126.0,
   126.25, 124.37, 121.44, 120.44, 121.37,
   121.69, 120.0, 119.62, 115.5, 116.75,
   119.06, 119.06, 115.06, 111.06, 113.12,
   110.0, 105.0, 104.69, 103.87, 104.69,
   105.44, 107.0, 89.0, 92.5, 92.12,
   94.62, 92.81, 94.25, 94.74681630560137, 96.18590815280069,
   93.69, 93.5, 90.0, 90.19, 90.5,
   92.12, 93.29487037619376, 94.49743518809689, 93.0, 93.87,
   93.0, 92.62, 93.56, 95.171483362314, 98.38949168115701,
   102.4759958405785, 101.81, 104.12, 103.37, 102.12,
   102.25, 103.37, 104.08856246750452, 107.35553123375226, 110.92776561687613,
   113.99513280843806, 112.25, 107.56, 106.56, 106.87,
   104.5, 105.75, 108.32697369381592, 107.75, 108.06,
   108.0, 108.19, 108.12, 109.06, 108.75,
   108.56, 106.62
};

static const TA_Real haSref_Close[] =
{
   92.0, 93.165, 95.03875, 94.68875, 94.52375,
   94.33625, 93.6325, 91.93, 91.01, 91.7425,
   94.75, 97.6725, 98.1175, 90.86, 90.0475,
   92.22, 91.2175, 89.6325, 90.5475, 91.03125,
   89.0, 87.64875, 86.3125, 84.0, 83.25125,
   82.625, 82.845, 86.96125, 87.055, 87.21125,
   85.515, 86.3675, 86.47, 87.39875, 88.83625,
   87.97749999999999, 86.11, 84.89125, 84.06375, 84.50874999999999,
   83.65625, 86.45375, 88.11874999999999, 89.32125, 90.68875,
   90.7825, 91.58749999999999, 90.26625, 90.07875, 90.83625,
   89.445, 88.2275, 86.39875, 84.10249999999999, 83.00125,
   84.3125, 85.7575, 86.235, 88.25125, 89.21125,
   89.40625, 88.33625, 90.68, 91.88374999999999, 92.14875,
   92.92125, 93.0, 91.45375000000001, 90.9925, 90.455,
   88.78125, 86.93875, 84.25, 83.6875, 84.83,
   97.89875, 99.07875, 103.46124999999999, 105.96875, 103.53125,
   102.78125, 104.01625000000001, 104.85125, 106.43875, 105.50875,
   105.39125, 107.1325, 109.3525, 110.11, 111.32124999999999,
   120.0, 120.03125, 118.14125, 119.2975, 118.24249999999999,
   117.17375, 115.71249999999999, 113.3125, 111.33625, 114.66499999999999,
   115.545, 115.735, 112.67, 112.39, 113.5925,
   114.515, 119.4525, 118.105, 117.7625, 115.295,
   114.6075, 115.3425, 116.6075, 120.2325, 120.11,
   120.5325, 123.9975, 124.4975, 122.7825, 122.92,
   123.825, 123.36, 123.92, 127.6075, 130.0775,
   131.22, 132.0475, 131.985, 133.5325, 135.675,
   137.32999999999998, 137.14249999999998, 137.5625, 136.735, 136.3775,
   135.925, 129.61, 128.28, 125.8725, 124.2175,
   123.6225, 125.3275, 127.3125, 125.715, 125.6725,
   123.515, 120.8575, 119.6075, 121.1875, 123.375,
   122.8425, 120.5625, 121.7475, 122.42, 123.155,
   126.185, 128.03, 126.345, 123.2325, 122.8875,
   123.5925, 122.4975, 122.325, 123.3725, 124.235,
   123.9375, 124.45, 126.7175, 125.8425, 128.7475,
   130.4875, 131.72, 133.845, 136.14249999999998, 133.64249999999998,
   132.7025, 133.22, 130.985, 127.985, 128.3775,
   127.9375, 126.0, 124.6875, 122.735, 124.45,
   123.235, 121.2325, 120.9675, 118.8275, 118.655,
   121.5775, 120.8125, 117.435, 113.655, 114.39,
   112.2175, 107.935, 106.36, 106.7625, 107.1725,
   107.015, 108.92, 91.5, 93.7025, 93.84,
   95.2475, 94.2175, 94.75, 97.625, 97.6075,
   95.515, 94.92, 92.61, 91.5325, 92.22,
   94.075, 95.7, 96.2175, 95.28, 95.075,
   94.31, 94.0275, 95.9525, 101.6075, 106.5625,
   107.685, 104.0775, 105.265, 104.36, 103.3425,
   103.1675, 104.5975, 110.6225, 114.5, 117.0625,
   117.7575, 117.06, 110.03, 109.2825, 109.125,
   106.7975, 108.1575, 110.0925, 109.1125, 109.2,
   109.045, 109.0925, 109.39, 109.795, 109.6725,
   109.375, 108.295
};

/* Corpus S12 and its golden, from the same pandas-ta-classic capture. Bar 0 is
 * chosen so that open + close != high + low, which is the only shape in which
 * the two published seed conventions differ past bar 0. */
static const TA_Real haS12_In_open[] =
{
   10.0, 12.5, 13.0, 12.0, 11.5, 13.5,
   14.0, 13.25, 12.75, 14.5, 15.0, 14.25
};

static const TA_Real haS12_In_high[] =
{
   15.0, 13.5, 14.25, 13.0, 13.75, 14.5,
   15.5, 14.0, 14.75, 15.25, 16.0, 15.5
};

static const TA_Real haS12_In_low[] =
{
   9.0, 11.0, 12.25, 11.0, 11.0, 12.75,
   13.5, 12.5, 12.25, 13.75, 14.25, 13.5
};

static const TA_Real haS12_In_close[] =
{
   12.5, 13.0, 12.0, 11.5, 13.5, 14.0,
   13.25, 12.75, 14.5, 15.0, 14.25, 15.25
};

static const TA_Real haS12_Gold_open[] =
{
   11.25, 11.4375, 11.96875, 12.421875, 12.1484375, 12.29296875,
   12.990234375, 13.5263671875, 13.32568359375, 13.444091796875, 14.0345458984375, 14.45477294921875
};

static const TA_Real haS12_Gold_high[] =
{
   15.0, 13.5, 14.25, 13.0, 13.75, 14.5,
   15.5, 14.0, 14.75, 15.25, 16.0, 15.5
};

static const TA_Real haS12_Gold_low[] =
{
   9.0, 11.0, 11.96875, 11.0, 11.0, 12.29296875,
   12.990234375, 12.5, 12.25, 13.444091796875, 14.0345458984375, 13.5
};

static const TA_Real haS12_Gold_close[] =
{
   11.625, 12.5, 12.875, 11.875, 12.4375, 13.6875,
   14.0625, 13.125, 13.5625, 14.625, 14.875, 14.625
};

/* The measured warm-up: with the unstable period at HA_WARMUP_FULL, an anchored
 * single-bar call reproduces the full-history value bit-for-bit at every bar of
 * this corpus. HA_WARMUP_SHORT does not, and is the control. */
#define HA_WARMUP_FULL   54
#define HA_WARMUP_SHORT  10

ErrorNumber test_func_ha( TA_History *history )
{
   static TA_Real outO[HA_OUT_CAP], outH[HA_OUT_CAP], outL[HA_OUT_CAP], outC[HA_OUT_CAP];
   static TA_Real fullO[HA_SREF_BARS], fullC[HA_SREF_BARS];
   static TA_Real aliasO[HA_SREF_BARS];
   static TA_Real flatO[32], flatH[32], flatL[32], flatC[32];
   TA_RetCode rc;
   TA_Integer beg, nb;
   ErrorNumber e;
   int nbBars = (int)history->nbBars;
   int i, m, converged;

   g_haGoldenCmp = g_haSeedCmp = g_haWarmupCmp = 0;

   TA_SetUnstablePeriod( TA_FUNC_UNST_HA, 0 );

   if( nbBars != HA_SREF_BARS )
   {
      printf( "HA Fail: the goldens were captured on the %d-bar corpus, got %d\n",
              HA_SREF_BARS, nbBars );
      return TA_TESTUTIL_TFRR_BAD_PARAM;
   }

   /* ---- (1) External golden, bit-exact, all four outputs ---- */
   rc = TA_HA( 0, nbBars - 1, history->open, history->high, history->low, history->close,
               &beg, &nb, outO, outH, outL, outC );
   if( rc != TA_SUCCESS || beg != 0 || nb != nbBars )
   {
      printf( "HA Fail: retCode %d range (%d,%d), expected (0,%d)\n",
              (int)rc, (int)beg, (int)nb, nbBars );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   for( i = 0; i < nbBars; i++ )
   {
      g_haGoldenCmp += 4;
      if( ha_bits_differ( outO[i], haSref_Open[i] )
          || ha_bits_differ( outH[i], haSref_High[i] )
          || ha_bits_differ( outL[i], haSref_Low[i] )
          || ha_bits_differ( outC[i], haSref_Close[i] ) )
      {
         printf( "HA oracle Fail [pandas-ta-classic 0.6.52] at bar %d:\n"
                 "   got      (%.17g, %.17g, %.17g, %.17g)\n"
                 "   expected (%.17g, %.17g, %.17g, %.17g)\n",
                 i, outO[i], outH[i], outL[i], outC[i],
                 haSref_Open[i], haSref_High[i], haSref_Low[i], haSref_Close[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* Cross-language: the same call, bit-identical on every language server. */
   if( server_verify_active() )
   {
      e = server_verify( "HA", 0, nbBars - 1, nbBars,
                         rc, beg, nb,
                         (const TA_Real*[]){ history->open, history->high,
                                             history->low, history->close, NULL },
                         NULL, 0,
                         (const TA_Real*[]){ outO, outH, outL, outC, NULL }, NULL );
      if( e != TA_TEST_PASS )
         return e;
   }

   memcpy( fullO, outO, (size_t)nbBars * sizeof(TA_Real) );
   memcpy( fullC, outC, (size_t)nbBars * sizeof(TA_Real) );

   /* ---- (2) The seed, on a corpus that can see it ---- */
   rc = TA_HA( 0, HA_S12_BARS - 1,
               haS12_In_open, haS12_In_high, haS12_In_low, haS12_In_close,
               &beg, &nb, outO, outH, outL, outC );
   if( rc != TA_SUCCESS || beg != 0 || nb != HA_S12_BARS )
   {
      printf( "HA Fail S12: retCode %d range (%d,%d), expected (0,%d)\n",
              (int)rc, (int)beg, (int)nb, HA_S12_BARS );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   for( i = 0; i < HA_S12_BARS; i++ )
   {
      g_haSeedCmp += 4;
      if( ha_bits_differ( outO[i], haS12_Gold_open[i] )
          || ha_bits_differ( outH[i], haS12_Gold_high[i] )
          || ha_bits_differ( outL[i], haS12_Gold_low[i] )
          || ha_bits_differ( outC[i], haS12_Gold_close[i] ) )
      {
         printf( "HA oracle Fail [pandas-ta-classic 0.6.52, S12] at bar %d:\n"
                 "   got      (%.17g, %.17g, %.17g, %.17g)\n"
                 "   expected (%.17g, %.17g, %.17g, %.17g)\n",
                 i, outO[i], outH[i], outL[i], outC[i],
                 haS12_Gold_open[i], haS12_Gold_high[i],
                 haS12_Gold_low[i], haS12_Gold_close[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }
   /* Non-vacuity control for that leg: run the OTHER published seed (ta4j's --
    * bar 0 is the raw bar) over the same corpus. It must disagree at every bar
    * from 1 on; if it does not, S12 has drifted into the degenerate shape SREF
    * has, and leg 2 is pinning nothing the seed can change. */
   {
      double rawOpen  = haS12_In_open[0];
      double rawClose = haS12_In_close[0];

      for( i = 1; i < HA_S12_BARS; i++ )
      {
         rawOpen  = (rawOpen + rawClose) / 2.0;
         rawClose = (((haS12_In_open[i] + haS12_In_high[i])
                      + haS12_In_low[i]) + haS12_In_close[i]) / 4.0;
         if( !ha_bits_differ( rawOpen, outO[i] ) )
         {
            printf( "HA seed control Fail: the raw-bar seed agrees at bar %d "
                    "(%.17g), so S12 cannot discriminate the two conventions\n",
                    i, rawOpen );
            return TA_TESTUTIL_TFRR_BAD_CALCULATION;
         }
      }
   }

   /* ---- (3) The unstable period is a warm-up, not a different answer ---- */

   /* k = 0 at the anchor: one candle, seeded from its own bar. */
   rc = TA_HA( 0, 0, history->open, history->high, history->low, history->close,
               &beg, &nb, outO, outH, outL, outC );
   if( rc != TA_SUCCESS || beg != 0 || nb != 1
       || ha_bits_differ( outO[0], haSref_Open[0] )
       || ha_bits_differ( outH[0], haSref_High[0] )
       || ha_bits_differ( outL[0], haSref_Low[0] )
       || ha_bits_differ( outC[0], haSref_Close[0] ) )
   {
      printf( "HA Fail single-bar seed: rc %d range (%d,%d), "
              "(%.17g,%.17g,%.17g,%.17g)\n",
              (int)rc, (int)beg, (int)nb, outO[0], outH[0], outL[0], outC[0] );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   /* k > 0 with the anchor at 0: the front is trimmed and nothing else moves,
    * because the recurrence still starts at bar 0. */
   TA_SetUnstablePeriod( TA_FUNC_UNST_HA, 7 );
   rc = TA_HA( 0, nbBars - 1, history->open, history->high, history->low, history->close,
               &beg, &nb, outO, outH, outL, outC );
   if( rc != TA_SUCCESS || beg != 7 || nb != nbBars - 7 )
   {
      printf( "HA Fail unstable=7: rc %d range (%d,%d), expected (7,%d)\n",
              (int)rc, (int)beg, (int)nb, nbBars - 7 );
      return TA_TESTUTIL_TFRR_BAD_BEGIDX;
   }
   for( i = 0; i < nb; i++ )
   {
      g_haWarmupCmp += 2;
      if( ha_bits_differ( outO[i], fullO[i + 7] )
          || ha_bits_differ( outC[i], fullC[i + 7] ) )
      {
         printf( "HA Fail unstable=7 at bar %d: (%.17g,%.17g) != full-history "
                 "(%.17g,%.17g)\n",
                 i + 7, outO[i], outC[i], fullO[i + 7], fullC[i + 7] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* The warm-up itself: anchor each call at one bar, so the recurrence is
    * seeded k bars back rather than at bar 0. MEASURED: HA_WARMUP_FULL is
    * enough everywhere on this corpus. */
   TA_SetUnstablePeriod( TA_FUNC_UNST_HA, HA_WARMUP_FULL );
   for( m = HA_WARMUP_FULL; m < nbBars; m++ )
   {
      rc = TA_HA( m, m, history->open, history->high, history->low, history->close,
                  &beg, &nb, outO, outH, outL, outC );
      g_haWarmupCmp += 2;
      if( rc != TA_SUCCESS || beg != m || nb != 1
          || ha_bits_differ( outO[0], fullO[m] )
          || ha_bits_differ( outC[0], fullC[m] ) )
      {
         printf( "HA Fail warm-up k=%d at bar %d: rc %d range (%d,%d) "
                 "(%.17g,%.17g) != full-history (%.17g,%.17g)\n",
                 HA_WARMUP_FULL, m, (int)rc, (int)beg, (int)nb,
                 outO[0], outC[0], fullO[m], fullC[m] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* The control. A shorter warm-up must NOT reproduce the full history
    * everywhere -- otherwise the loop above passes for free and pins nothing
    * about convergence. */
   TA_SetUnstablePeriod( TA_FUNC_UNST_HA, HA_WARMUP_SHORT );
   converged = 1;
   for( m = HA_WARMUP_SHORT; m < nbBars && converged; m++ )
   {
      rc = TA_HA( m, m, history->open, history->high, history->low, history->close,
                  &beg, &nb, outO, outH, outL, outC );
      if( rc != TA_SUCCESS )
      {
         printf( "HA Fail warm-up control: rc %d at bar %d\n", (int)rc, m );
         return TA_TESTUTIL_TFRR_BAD_RETCODE;
      }
      if( ha_bits_differ( outO[0], fullO[m] ) )
         converged = 0;
   }
   if( converged )
   {
      printf( "HA warm-up control Fail: k=%d already reproduces the full "
              "history at every bar, so the k=%d leg asserts nothing\n",
              HA_WARMUP_SHORT, HA_WARMUP_FULL );
      return TA_TESTUTIL_TFRR_BAD_CALCULATION;
   }

   TA_SetUnstablePeriod( TA_FUNC_UNST_HA, 0 );

   /* ---- (4) Edges ---- */

   /* Flat OHLC: every output is exactly the flat price. Nothing here is
    * approximate -- the four-price average of four equal values and the
    * midpoint of two equal values are both exact. */
   for( i = 0; i < 32; i++ )
   {
      flatO[i] = flatH[i] = flatL[i] = flatC[i] = 123.25;
   }
   rc = TA_HA( 0, 31, flatO, flatH, flatL, flatC, &beg, &nb, outO, outH, outL, outC );
   if( rc != TA_SUCCESS || beg != 0 || nb != 32 )
   {
      printf( "HA Fail flat: rc %d range (%d,%d)\n", (int)rc, (int)beg, (int)nb );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   for( i = 0; i < 32; i++ )
   {
      if( outO[i] != 123.25 || outH[i] != 123.25
          || outL[i] != 123.25 || outC[i] != 123.25 )
      {
         printf( "HA Fail flat at bar %d: (%.17g,%.17g,%.17g,%.17g)\n",
                 i, outO[i], outH[i], outL[i], outC[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* In-place: outHAOpen IS inOpen. Every value this bar still owes must have
    * been read before the first store, which is what the body's tempHigh /
    * tempLow carry. */
   memcpy( aliasO, history->open, (size_t)nbBars * sizeof(TA_Real) );
   rc = TA_HA( 0, nbBars - 1, aliasO, history->high, history->low, history->close,
               &beg, &nb, aliasO, outH, outL, outC );
   if( rc != TA_SUCCESS || beg != 0 || nb != nbBars )
   {
      printf( "HA Fail in-place: rc %d range (%d,%d)\n", (int)rc, (int)beg, (int)nb );
      return TA_TESTUTIL_TFRR_BAD_RETCODE;
   }
   for( i = 0; i < nbBars; i++ )
   {
      if( ha_bits_differ( aliasO[i], haSref_Open[i] )
          || ha_bits_differ( outH[i], haSref_High[i] )
          || ha_bits_differ( outL[i], haSref_Low[i] )
          || ha_bits_differ( outC[i], haSref_Close[i] ) )
      {
         printf( "HA Fail in-place at bar %d: (%.17g,%.17g,%.17g,%.17g)\n",
                 i, aliasO[i], outH[i], outL[i], outC[i] );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
   }

   /* The other three aliasing pairs. Each output in turn is handed the input
    * buffer it is most likely to clobber: the clamped outputs read the raw high
    * and low of the bar they are writing. */
   {
      static const int aliasCase[3] = { 1, 2, 3 };  /* which output aliases */
      int k;

      for( k = 0; k < 3; k++ )
      {
         const TA_Real *src = ( aliasCase[k] == 3 ) ? history->close
                            : ( aliasCase[k] == 1 ) ? history->high
                                                    : history->low;
         memcpy( aliasO, src, (size_t)nbBars * sizeof(TA_Real) );

         if( aliasCase[k] == 1 )
            rc = TA_HA( 0, nbBars - 1, history->open, aliasO, history->low,
                        history->close, &beg, &nb, outO, aliasO, outL, outC );
         else if( aliasCase[k] == 2 )
            rc = TA_HA( 0, nbBars - 1, history->open, history->high, aliasO,
                        history->close, &beg, &nb, outO, outH, aliasO, outC );
         else
            rc = TA_HA( 0, nbBars - 1, history->open, history->high, history->low,
                        aliasO, &beg, &nb, outO, outH, outL, aliasO );

         if( rc != TA_SUCCESS || beg != 0 || nb != nbBars )
         {
            printf( "HA Fail in-place case %d: rc %d range (%d,%d)\n",
                    aliasCase[k], (int)rc, (int)beg, (int)nb );
            return TA_TESTUTIL_TFRR_BAD_RETCODE;
         }
         for( i = 0; i < nbBars; i++ )
         {
            const TA_Real got = aliasO[i];
            const TA_Real want = ( aliasCase[k] == 1 ) ? haSref_High[i]
                               : ( aliasCase[k] == 2 ) ? haSref_Low[i]
                                                       : haSref_Close[i];
            if( ha_bits_differ( got, want ) )
            {
               printf( "HA Fail in-place case %d at bar %d: %.17g != %.17g\n",
                       aliasCase[k], i, got, want );
               return TA_TESTUTIL_TFRR_BAD_CALCULATION;
            }
         }
      }
   }

   /* ---- (5) HA_close is NOT a composed TA_AVGPRICE call ----
    * Same four terms, different summation order, and floating-point addition
    * does not associate. Asserted to DIFFER on at least one bar: without this
    * the summation-order rule in ha.c is unenforced prose, and a future
    * "simplification" routing HA_close through the shipped TA_AVGPRICE would
    * pass every other leg here. The upper bound on the same comparison is what
    * keeps the leg from passing for the wrong reason -- these are the same
    * quantity, so anything past a rounding difference is a real defect. */
   {
      int differing = 0;

      rc = TA_AVGPRICE( 0, nbBars - 1, history->open, history->high,
                        history->low, history->close, &beg, &nb, outC );
      if( rc != TA_SUCCESS || beg != 0 || nb != nbBars )
      {
         printf( "HA Fail AVGPRICE: rc %d range (%d,%d)\n",
                 (int)rc, (int)beg, (int)nb );
         return TA_TESTUTIL_TFRR_BAD_RETCODE;
      }
      for( i = 0; i < nbBars; i++ )
      {
         double d = outC[i] - haSref_Close[i];

         if( d < 0.0 )
            d = -d;
         if( d > 1e-12 * haSref_Close[i] )
         {
            printf( "HA Fail AVGPRICE bar %d: %.17g vs %.17g is past rounding\n",
                    i, outC[i], haSref_Close[i] );
            return TA_TESTUTIL_TFRR_BAD_CALCULATION;
         }
         if( ha_bits_differ( outC[i], haSref_Close[i] ) )
            differing++;
      }
      if( differing == 0 )
      {
         printf( "HA Fail: HA_close is now bit-identical to TA_AVGPRICE on all "
                 "%d bars, so the summation order stopped being the published "
                 "one\n", nbBars );
         return TA_TESTUTIL_TFRR_BAD_CALCULATION;
      }
      printf( "HA: HA_close differs from TA_AVGPRICE on %d of %d bars\n",
              differing, nbBars );
   }

   printf( "HA: %d golden, %d seed, %d warm-up comparisons\n",
           g_haGoldenCmp, g_haSeedCmp, g_haWarmupCmp );

   return TA_TEST_PASS;
}
