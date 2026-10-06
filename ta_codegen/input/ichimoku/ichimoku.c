/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  MF       Mario Fortier
 *  CC       Claude Code (AI assistant)
 *
 * Change history:
 *
 *  MMDDYY BY     Description
 *  -------------------------------------------------------------------
 *  100726 MF,CC  Initial version (#490).
 *
 */

int ichimoku_display_shift(int optInTenkanPeriod, int optInKijunPeriod, int optInSenkouBPeriod, int outputIdx)
{
   /* rL9: a shift of s means a chart draws the value computed at bar i at
    * bar i + s. The two spans are the leading ones -- Goichi Hosoda draws
    * them kijun bars into the future, which is what makes the cloud sit
    * ahead of price -- so their shift is POSITIVE, where dpo.c's and
    * fractal.c's are negative. The conversion and base lines are drawn on
    * the bar that computed them.
    *
    * The shift reads the optional parameters and the output index only,
    * never a setting (rL9).
    */
   if( outputIdx == 2 || outputIdx == 3 )
      return optInKijunPeriod;
   return 0;
}

int ichimoku_lookback(int optInTenkanPeriod, int optInKijunPeriod, int optInSenkouBPeriod)
{
   int longest;

   /* Each line is a midpoint over its own window, so each needs its own
    * window filled; the first bar that has all of them is the longest one's.
    * The MAX is not simplified to the Senkou B period: nothing orders the
    * three, and a kijun longer than senkouB dominates.
    *
    * Span A is the mean of the other two lines, so it adds nothing: it is
    * ready on the bar they both are.
    */
   longest = optInTenkanPeriod;
   if( optInKijunPeriod > longest )
      longest = optInKijunPeriod;
   if( optInSenkouBPeriod > longest )
      longest = optInSenkouBPeriod;

   return longest - 1;
}

TA_RetCode ichimoku(int startIdx, int endIdx,
   const double inHigh[],
   const double inLow[],
   int optInTenkanPeriod,
   int optInKijunPeriod,
   int optInSenkouBPeriod,
   int *outBegIdx, int *outNBElement,
   double outTenkanSen[],
   double outKijunSen[],
   double outSenkouSpanA[],
   double outSenkouSpanB[])
{
   double tenkan, kijun;
   double hiT, loT, hiK, loK, hiB, loB, tmp;
   int lookbackTotal, today, outIdx, i;
   int trailT, trailK, trailB;
   int hiIdxT, loIdxT, hiIdxK, loIdxK, hiIdxB, loIdxB;

   /* Goichi Hosoda's Ichimoku Kinko Hyo, the four lines that are computed
    * from price alone:
    *
    *    Tenkan-sen  = midpoint of the last tenkan bars
    *    Kijun-sen   = midpoint of the last kijun bars
    *    Senkou A    = mean of those two lines
    *    Senkou B    = midpoint of the last senkouB bars
    *
    * where a midpoint is (highest high + lowest low)/2 over the window, which
    * is TA_MIDPRICE. The two spans are drawn kijun bars ahead; that is
    * display-shift metadata (ichimoku_display_shift), never a shift of the
    * values, so every output is written at the bar that computed it (rL9).
    * The Chikou span is the close displaced backward and carries no
    * computation, so it is not an output here.
    *
    * SPAN A HALVES THE TWO ALREADY-ROUNDED MIDPOINTS. Folding it into
    * (hiT + loT + hiK + loK)/4 is the same value in real arithmetic and a
    * different double on a quarter of the bars; only a bit-exact gate
    * against TA_MIDPRICE and TA_MEDPRICE sees the difference.
    *
    * The three windows use the cached-extreme-plus-rescan idiom of stoch.c
    * rather than midprice.c's block scan. Both are exact -- an extremum is a
    * selection, so the bits are whichever input bar won, whatever the scan
    * order -- so the choice is streamability and cost, not correctness: the
    * block-scan form produces a whole block at a time and cannot be a per-bar
    * automaton, which is why midprice.c carries a midprice_ALT1 for the
    * streaming tier (#147). This form needs no twin.
    */

   lookbackTotal = ichimoku_lookback( optInTenkanPeriod, optInKijunPeriod,
      optInSenkouBPeriod );

   /* Move up the start index if there is not
    * enough initial data.
    */
   if( startIdx < lookbackTotal )
      startIdx = lookbackTotal;

   /* Make sure there is still something to evaluate. */
   if( startIdx > endIdx )
   {
      *outBegIdx = 0;
      *outNBElement = 0;
      return TA_SUCCESS;
   }

   outIdx = 0;
   today = startIdx;
   trailT = today - (optInTenkanPeriod - 1);
   trailK = today - (optInKijunPeriod - 1);
   trailB = today - (optInSenkouBPeriod - 1);

   hiIdxT = -1;
   loIdxT = -1;
   hiIdxK = -1;
   loIdxK = -1;
   hiIdxB = -1;
   loIdxB = -1;
   hiT = 0.0;
   loT = 0.0;
   hiK = 0.0;
   loK = 0.0;
   hiB = 0.0;
   loB = 0.0;

   while( today <= endIdx )
   {
      /* Tenkan window. The cached index is refreshed on a tie, so a flat
       * stretch never rescans. */
      tmp = inHigh[today];
      if( hiIdxT < trailT )
      {
         hiIdxT = trailT;
         hiT = inHigh[hiIdxT];
         i = hiIdxT;
         while( ++i <= today )
         {
            tmp = inHigh[i];
            if( tmp > hiT )
            {
               hiIdxT = i;
               hiT = tmp;
            }
         }
      }
      else if( tmp >= hiT )
      {
         hiIdxT = today;
         hiT = tmp;
      }

      tmp = inLow[today];
      if( loIdxT < trailT )
      {
         loIdxT = trailT;
         loT = inLow[loIdxT];
         i = loIdxT;
         while( ++i <= today )
         {
            tmp = inLow[i];
            if( tmp < loT )
            {
               loIdxT = i;
               loT = tmp;
            }
         }
      }
      else if( tmp <= loT )
      {
         loIdxT = today;
         loT = tmp;
      }

      /* Kijun window. */
      tmp = inHigh[today];
      if( hiIdxK < trailK )
      {
         hiIdxK = trailK;
         hiK = inHigh[hiIdxK];
         i = hiIdxK;
         while( ++i <= today )
         {
            tmp = inHigh[i];
            if( tmp > hiK )
            {
               hiIdxK = i;
               hiK = tmp;
            }
         }
      }
      else if( tmp >= hiK )
      {
         hiIdxK = today;
         hiK = tmp;
      }

      tmp = inLow[today];
      if( loIdxK < trailK )
      {
         loIdxK = trailK;
         loK = inLow[loIdxK];
         i = loIdxK;
         while( ++i <= today )
         {
            tmp = inLow[i];
            if( tmp < loK )
            {
               loIdxK = i;
               loK = tmp;
            }
         }
      }
      else if( tmp <= loK )
      {
         loIdxK = today;
         loK = tmp;
      }

      /* Senkou B window. */
      tmp = inHigh[today];
      if( hiIdxB < trailB )
      {
         hiIdxB = trailB;
         hiB = inHigh[hiIdxB];
         i = hiIdxB;
         while( ++i <= today )
         {
            tmp = inHigh[i];
            if( tmp > hiB )
            {
               hiIdxB = i;
               hiB = tmp;
            }
         }
      }
      else if( tmp >= hiB )
      {
         hiIdxB = today;
         hiB = tmp;
      }

      tmp = inLow[today];
      if( loIdxB < trailB )
      {
         loIdxB = trailB;
         loB = inLow[loIdxB];
         i = loIdxB;
         while( ++i <= today )
         {
            tmp = inLow[i];
            if( tmp < loB )
            {
               loIdxB = i;
               loB = tmp;
            }
         }
      }
      else if( tmp <= loB )
      {
         loIdxB = today;
         loB = tmp;
      }

      /* Each midpoint is spelled as midprice.c spells it, and Span A halves
       * the two lines rather than the four extremes. */
      tenkan = (hiT + loT) / 2.0;
      kijun = (hiK + loK) / 2.0;

      outTenkanSen[outIdx] = tenkan;
      outKijunSen[outIdx] = kijun;
      outSenkouSpanA[outIdx] = (tenkan + kijun) / 2.0;
      outSenkouSpanB[outIdx] = (hiB + loB) / 2.0;
      outIdx = outIdx + 1;

      trailT = trailT + 1;
      trailK = trailK + 1;
      trailB = trailB + 1;
      today = today + 1;
   }

   *outNBElement = outIdx;
   *outBegIdx    = startIdx;

   return TA_SUCCESS;
}
