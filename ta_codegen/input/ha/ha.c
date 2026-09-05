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

int ha_lookback(void)
{
   return TA_GetUnstablePeriod(TA_FUNC_UNST_HA);
}

TA_RetCode ha(int startIdx, int endIdx,
   const double inOpen[],
   const double inHigh[],
   const double inLow[],
   const double inClose[],
   int *outBegIdx, int *outNBElement,
   double outHAOpen[],
   double outHAHigh[],
   double outHALow[],
   double outHAClose[])
{
   int i, outIdx, today, lookbackTotal;
   double haOpen, haClose, tempHigh, tempLow;

   *outBegIdx = 0;
   *outNBElement = 0;

   lookbackTotal = ha_lookback();

   if( startIdx < lookbackTotal )
      startIdx = lookbackTotal;

   if( startIdx > endIdx )
      return TA_SUCCESS;

   /* The seed is the published convention: the first candle opens at the raw
    * bar's own midpoint. Its influence halves every bar, which is why the
    * function is unstable-period rather than path-dependent -- a longer warm-up
    * buys convergence, it does not change the answer forever.
    */
   today   = startIdx - lookbackTotal;
   haOpen  = (inOpen[today] + inClose[today]) / 2.0;
   haClose = (((inOpen[today] + inHigh[today]) + inLow[today]) + inClose[today]) / 4.0;

   /* Warm-up. Emits nothing; it only carries the pair forward to startIdx. */
   for( i = today + 1; i <= startIdx; i++ )
   {
      haOpen  = (haOpen + haClose) / 2.0;
      haClose = (((inOpen[i] + inHigh[i]) + inLow[i]) + inClose[i]) / 4.0;
   }

   /* The summation order ((o+h)+l)+c and the two exact power-of-two divisions
    * are the whole numeric contract: every published implementation sums in
    * that order, so the result is bit-exact against them rather than close.
    * TA_AVGPRICE's (h+l+c+o)/4 is a different order and differs by 1 ulp.
    *
    * The high and low are elementwise over bar-i quantities only, so they carry
    * no state -- haOpen and haClose remain the entire recurrence.
    *
    * In-place is supported: this bar's four input values are read into the
    * recurrence (and into tempHigh/tempLow) before the first store, so an
    * output aliasing any input cannot clobber a value still owed to this bar.
    * The dialect has no 3-arg max/min; nest the 2-arg builtins.
    */
   outIdx = 0;

   tempHigh = inHigh[startIdx];
   tempLow  = inLow[startIdx];

   outHAOpen[outIdx]  = haOpen;
   outHAHigh[outIdx]  = max( max( tempHigh, haOpen ), haClose );
   outHALow[outIdx]   = min( min( tempLow, haOpen ), haClose );
   outHAClose[outIdx] = haClose;
   outIdx++;

   for( i = startIdx + 1; i <= endIdx; i++ )
   {
      tempHigh = inHigh[i];
      tempLow  = inLow[i];

      haOpen  = (haOpen + haClose) / 2.0;
      haClose = (((inOpen[i] + tempHigh) + tempLow) + inClose[i]) / 4.0;

      outHAOpen[outIdx]  = haOpen;
      outHAHigh[outIdx]  = max( max( tempHigh, haOpen ), haClose );
      outHALow[outIdx]   = min( min( tempLow, haOpen ), haClose );
      outHAClose[outIdx] = haClose;
      outIdx++;
   }

   *outBegIdx    = startIdx;
   *outNBElement = outIdx;

   return TA_SUCCESS;
}
