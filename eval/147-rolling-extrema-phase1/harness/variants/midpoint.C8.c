/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  MF       Mario Fortier
 *
 * Change history:
 *
 *  MMDDYY BY   Description
 *  -------------------------------------------------------------------
 *  110199 MF   Template creation.
 *  052603 MF   Adapt code to compile with .NET Managed C++
 *
 */

int midpoint_lookback(int optInTimePeriod)
{
   return (optInTimePeriod-1);
}

TA_RetCode midpoint(int startIdx, int endIdx,
   const double inReal[],
   int optInTimePeriod,
   int *outBegIdx, int *outNBElement,
   double outReal[])
{
   double highest, lowest, tmpHigh, tmpLow;
   int outIdx, nbInitialElementNeeded, trailingIdx, today, i, highestIdx, lowestIdx;

   nbInitialElementNeeded = (optInTimePeriod-1);

   /* Move up the start index if there is not
    * enough initial data.
    */
   if( startIdx < nbInitialElementNeeded )
      startIdx = nbInitialElementNeeded;

   /* Make sure there is still something to evaluate. */
   if( startIdx > endIdx )
   {
      *outBegIdx = 0;
      *outNBElement = 0;
      return TA_SUCCESS;
   }

   outIdx = 0;
   today       = startIdx;
   trailingIdx = startIdx-nbInitialElementNeeded;

   while( today <= endIdx )
   {
      highest = inReal[trailingIdx];
      lowest = inReal[trailingIdx];
      i = trailingIdx + 1;
      TA_UNROLL(4)
      while( i <= today )
      {
         tmpHigh = inReal[i];
         if( tmpHigh > highest ) highest = tmpHigh;
         tmpLow = inReal[i];
         if( tmpLow < lowest ) lowest = tmpLow;
         i++;
      }

      outReal[outIdx++] = (highest+lowest)/2.0;
      trailingIdx++;
      today++;
   }

   /* Keep the outBegIdx relative to the
    * caller input before returning.
    */
   *outBegIdx    = startIdx;
   *outNBElement = outIdx;

   return TA_SUCCESS;
}
