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
   /* The unstable period is the ONLY lookback: bar 0 is computable on its
    * own, so with the knob at its default 0 every input bar produces an
    * output bar.
    */
   return TA_GetUnstablePeriod(TA_FUNC_UNST_HA);
}

TA_RetCode ha(int startIdx, int endIdx,
   const double inOpen[],
   const double inHigh[],
   const double inLow[],
   const double inClose[],
   int *outBegIdx,
   int *outNBElement,
   double outHAOpen[],
   double outHAHigh[],
   double outHALow[],
   double outHAClose[])
{
   int today, outIdx, lookbackTotal;
   double haOpen, haClose, haHigh, haLow, prevHAOpen, prevHAClose;
   double tempOpen, tempHigh, tempLow, tempClose;

   /* Heikin-Ashi ("average bar"): an OHLC-to-OHLC smoothing transform.
    *
    *   HA_close[i] = (open[i] + high[i] + low[i] + close[i]) / 4
    *   HA_open [0] = (open[0] + close[0]) / 2
    *   HA_open [i] = (HA_open[i-1] + HA_close[i-1]) / 2
    *   HA_high [i] = max(high[i], HA_open[i], HA_close[i])
    *   HA_low  [i] = min(low [i], HA_open[i], HA_close[i])
    *
    * Keep the four-term sum in THIS left-to-right order. It is the oracles'
    * association, and floating-point addition is not associative:
    * TA_AVGPRICE sums the same four terms as (H+L+C+O)/4 and differs from
    * HA_close in the last place on ordinary bars, so HA_close is not a
    * composed AVGPRICE call and the two must not be unified.
    *
    * Every operation is one addition or one division by an exact power of
    * two, so the transform is exact whenever its inputs are -- which is why
    * the external captures are frozen at tolerance 0 rather than a band.
    *
    * BOTH stability flags are declared, and they answer different questions.
    * `unstable_period` is the ABI knob: the open's recursion carries a factor
    * of 1/2 per bar, so a seed's influence halves every step and dies out
    * entirely within a few dozen bars -- a caller who spends them gets a
    * start-independent series (56 bars suffice on the regtest history, and
    * test_ha.c pins that). `path_dependent` declares
    * what is true at the DEFAULT of 0: the recursion re-seeds at the anchor,
    * so HA(3, 7, ...) starts its open at (open[3]+close[3])/2 rather than
    * warming up from bar 0. Without it the range-stability leg compares a
    * sub-range call against a full-range one and fails; with only it, the
    * knob that makes the two converge would not exist.
    */

   lookbackTotal = ha_lookback();

   if( startIdx < lookbackTotal )
      startIdx = lookbackTotal;

   /* Make sure there is still something to evaluate. */
   if( startIdx > endIdx )
   {
      *outBegIdx    = 0;
      *outNBElement = 0;
      return TA_SUCCESS;
   }

   /* The seed is carried as a VIRTUAL previous candle rather than as a
    * special first iteration: seeding the pair with the anchor bar's own
    * open and close makes the uniform recursion produce
    * (open+close)/2 at that bar, which is the published seed. One loop body
    * then serves the anchor bar and every bar after it -- and that same pair
    * is the streaming tier's initial state, so batch and stream share one
    * definition of "where the recursion starts".
    */
   today       = startIdx - lookbackTotal;
   prevHAOpen  = inOpen[today];
   prevHAClose = inClose[today];

   /* Warm-up: advance the recursion across the unstable-period bars without
    * emitting. Empty at the default knob of 0.
    */
   while( today < startIdx )
   {
      tempOpen  = inOpen[today];
      tempHigh  = inHigh[today];
      tempLow   = inLow[today];
      tempClose = inClose[today];
      haOpen    = (prevHAOpen + prevHAClose) / 2.0;
      haClose   = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;
      prevHAOpen  = haOpen;
      prevHAClose = haClose;
      today++;
   }

   outIdx = 0;

   while( today <= endIdx )
   {
      tempOpen  = inOpen[today];
      tempHigh  = inHigh[today];
      tempLow   = inLow[today];
      tempClose = inClose[today];

      haOpen  = (prevHAOpen + prevHAClose) / 2.0;
      haClose = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;

      /* An elementwise clamp of the raw bar against the two body edges, so
       * the extremes carry no state of their own.
       */
      haHigh = tempHigh;
      if( haOpen > haHigh )
         haHigh = haOpen;
      if( haClose > haHigh )
         haHigh = haClose;

      haLow = tempLow;
      if( haOpen < haLow )
         haLow = haOpen;
      if( haClose < haLow )
         haLow = haClose;

      /* Written only after this bar's four inputs are in locals above: an
       * output buffer is allowed to alias any input, and output slot k lands
       * on input bar k, which is at or behind `today`.
       */
      outHAOpen[outIdx]  = haOpen;
      outHAHigh[outIdx]  = haHigh;
      outHALow[outIdx]   = haLow;
      outHAClose[outIdx] = haClose;
      outIdx++;

      prevHAOpen  = haOpen;
      prevHAClose = haClose;
      today++;
   }

   *outBegIdx    = startIdx;
   *outNBElement = outIdx;

   return TA_SUCCESS;
}
