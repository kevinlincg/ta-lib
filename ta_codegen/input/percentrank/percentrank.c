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

int percentrank_lookback(int optInTimePeriod)
{
   /* The current bar is excluded from its own window, so the first bar that can
    * be ranked is bar optInTimePeriod -- not optInTimePeriod-1.
    */
   return optInTimePeriod;
}

TA_RetCode percentrank(int startIdx, int endIdx,
   const double inReal[],
   int optInTimePeriod,
   int *outBegIdx, int *outNBElement,
   double outReal[])
{
   int today, outIdx, i, count;
   double current;

   if( startIdx < optInTimePeriod )
      startIdx = optInTimePeriod;

   /* Make sure there is still something to evaluate. */
   if( startIdx > endIdx )
   {
      *outBegIdx = 0;
      *outNBElement = 0;
      return TA_SUCCESS;
   }

   outIdx = 0;
   today = startIdx;

   while( today <= endIdx )
   {
      current = inReal[today];

      /* STRICTLY less than: a window value equal to the current one does not
       * count. Ties are the whole of the divergence from engines that rank with
       * '<=', so the comparison must not be relaxed to reach one of them.
       */
      count = 0;
      for( i = optInTimePeriod; i >= 1; i-- )
      {
         if( inReal[today-i] < current )
            count++;
      }

      /* Divide, THEN scale. (count/period)*100.0 and 100.0*count/period are
       * different doubles -- 90 of 249 bars differ on the 252-bar reference
       * series at period 3 -- and the published values this is gated against
       * are the divide-first ones.
       *
       * #130 in-place aliasing: every window read above happens before this
       * store. startIdx is clamped to at least optInTimePeriod, so outIdx never
       * runs ahead of today-optInTimePeriod, the oldest slot this bar reads and
       * one no later bar reads again.
       */
      outReal[outIdx] = ((double)count / (double)optInTimePeriod) * 100.0;

      outIdx++;
      today++;
   }

   *outBegIdx = startIdx;
   *outNBElement = outIdx;

   return TA_SUCCESS;
}
