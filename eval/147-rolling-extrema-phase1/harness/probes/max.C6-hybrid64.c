/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  MF       Mario Fortier
 *  JV       Jesus Viver <324122@cienz.unizar.es>
 *
 * Change history:
 *
 *  MMDDYY BY   Description
 *  -------------------------------------------------------------------
 *  112400 MF   Template creation.
 *  101902 JV   Speed optimization of the algorithm
 *  102202 MF   Speed optimize a bit further
 *  052603 MF   Adapt code to compile with .NET Managed C++
 *
 */

int max_lookback(int optInTimePeriod)
{
   return (optInTimePeriod-1);
}

TA_RetCode max(int startIdx, int endIdx,
   const double inReal[],
   int optInTimePeriod,
   int *outBegIdx, int *outNBElement,
   double outReal[])
{
   double dqVal[64];
   int dqIdx[64];
   double highest, tmp;
   int outIdx, nbInitialElementNeeded;
   int trailingIdx, today, i, j, k, head, count, highestIdx;

   /* Identify the minimum number of price bar needed
    * to identify at least one output over the specified
    * period.
    */
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

   if( optInTimePeriod <= 64 )
   {
      head  = 0;
      count = 0;

      i = trailingIdx;
      while( i < startIdx )
      {
         tmp = inReal[i];
         j = head + count;
         if( j >= optInTimePeriod )
         {
            j -= optInTimePeriod;
         }
         k = j - 1;
         if( k < 0 )
         {
            k += optInTimePeriod;
         }
         while( count > 0 && dqVal[k] <= tmp )
         {
            count--;
            j = k;
            k = j - 1;
            if( k < 0 )
            {
               k += optInTimePeriod;
            }
         }
         dqVal[j] = tmp;
         dqIdx[j] = i;
         count++;
         i++;
      }

      while( today <= endIdx )
      {
         if( dqIdx[head] < trailingIdx )
         {
            count--;
            head++;
            if( head == optInTimePeriod )
            {
               head = 0;
            }
         }

         tmp = inReal[today];
         j = head + count;
         if( j >= optInTimePeriod )
         {
            j -= optInTimePeriod;
         }
         k = j - 1;
         if( k < 0 )
         {
            k += optInTimePeriod;
         }
         while( count > 0 && dqVal[k] <= tmp )
         {
            count--;
            j = k;
            k = j - 1;
            if( k < 0 )
            {
               k += optInTimePeriod;
            }
         }
         dqVal[j] = tmp;
         dqIdx[j] = today;
         count++;

         outReal[outIdx++] = dqVal[head];
         trailingIdx++;
         today++;
      }
   }
   else
   {
      highestIdx  = -1;
      highest     = 0.0;

      while( today <= endIdx )
      {
         tmp = inReal[today];

         if( highestIdx < trailingIdx )
         {
            highestIdx = trailingIdx;
            highest = inReal[highestIdx];
            i = highestIdx;
            TA_UNROLL(4)
            while( ++i<=today )
            {
               tmp = inReal[i];
               if( tmp > highest )
               {
                  highestIdx = i;
                  highest = tmp;
               }
            }
         }
         else if( tmp >= highest )
         {
            highestIdx = today;
            highest = tmp;
         }

         outReal[outIdx++] = highest;
         trailingIdx++;
         today++;
      }
   }

   /* Keep the outBegIdx relative to the
    * caller input before returning.
    */
   *outBegIdx    = startIdx;
   *outNBElement = outIdx;

   return TA_SUCCESS;
}
