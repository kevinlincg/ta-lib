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
 *  090426 KL     First version (issue #347).
 */

int zlema_lookback(int optInTimePeriod)
{
   /* The de-lagged series is undefined for its first `lag` bars, and the EMA
    * over it then needs a full window to seed. ZLEMA has no unstable-period id
    * of its own: it converges as its EMA does, so it reads EMA's knob (the
    * DEMA/TEMA/TRIX convention). */
   return (optInTimePeriod-1)/2 + optInTimePeriod - 1
   + TA_GetUnstablePeriod(TA_FUNC_UNST_EMA);
}

TA_RetCode zlema(int startIdx, int endIdx,
   const double inReal[],
   int optInTimePeriod,
   int *outBegIdx, int *outNBElement,
   double outReal[])
{
   double k, tempReal, prevMA;
   int i, lag, today, trailingIdx, outIdx, lookbackTotal;

   /* Identify the minimum number of price bar needed
    * to calculate at least one output.
    */
   lookbackTotal = zlema_lookback( optInTimePeriod );

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

   *outBegIdx = startIdx;

   /* No smoothing at period of 1: the output is a copy of the input (same
    * convention as TA_MA for every MAType). The de-lag is already the identity
    * there -- lag is 0 and `2.0*x - x` is exact -- but the recursion is not:
    * at period 1 k is exactly 1.0, so the step reduces to (x-prev)+prev, which
    * returns x only while consecutive values stay within a factor of two of
    * each other. This is TA_EMA's trap, and it takes TA_EMA's explicit copy.
    */
   if( optInTimePeriod == 1 )
   {
      outIdx = 0;
      today = startIdx;
      while( today <= endIdx )
         outReal[outIdx++] = inReal[today++];
      *outNBElement = outIdx;
      return TA_SUCCESS;
   }

   k   = 2.0 / ((double)(optInTimePeriod + 1));
   lag = (optInTimePeriod-1)/2;

   /* The de-lagged value 2*P[t] - P[t-lag] is spelled `2.0*x - trailing`, with
    * one rounding rather than the two of x + (x - trailing), and the EMA step
    * keeps TA_EMA's own ((v-prev)*k)+prev. Both are the bit-exactness contract
    * against the shipped TA_EMA over a materialised de-lagged series, which the
    * regression test asserts by memcmp -- reordering either is a test failure,
    * not a style change.
    */
   today       = startIdx - lookbackTotal + lag;
   trailingIdx = today - lag;

   /* The first EMA value is a simple average of the first `period` de-lagged
    * values; it then becomes the seed for the recursion below.
    */
   i = optInTimePeriod;
   tempReal = 0.0;
   while( i-- > 0 )
      tempReal += 2.0*inReal[today++] - inReal[trailingIdx++];

   prevMA = tempReal / optInTimePeriod;

   while( today <= startIdx )
      prevMA = (((2.0*inReal[today++] - inReal[trailingIdx++]) - prevMA)*k) + prevMA;

   outReal[0] = prevMA;
   outIdx = 1;

   while( today <= endIdx )
   {
      prevMA = (((2.0*inReal[today++] - inReal[trailingIdx++]) - prevMA)*k) + prevMA;
      outReal[outIdx++] = prevMA;
   }

   *outNBElement = outIdx;

   return TA_SUCCESS;
}
