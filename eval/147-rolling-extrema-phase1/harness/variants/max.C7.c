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
   double highest, tmp;
   double *dqV_highest;
   int *dqI_highest;
   int outIdx, nbInitialElementNeeded, trailingIdx, today, i, j, k, dqCap, dqMask, hd_highest, cnt_highest, highestIdx;

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

   if( optInTimePeriod <= 100000 )
   {
      /* Monotonic deque over a power-of-two capacity.  Same algorithm and
       * same amortized O(1) bound as the plain deque; the ring index wrap is a
       * mask rather than three compare-and-fixup branches.  Holds COPIES, so
       * the input and the output may alias.
       */
      dqCap = 1;
      while( dqCap < optInTimePeriod )
      {
         dqCap = dqCap + dqCap;
      }
      dqMask = dqCap - 1;

      dqV_highest = malloc((dqCap) * sizeof(double));
      if( !dqV_highest )
         return TA_ALLOC_ERR;
      dqI_highest = malloc((dqCap) * sizeof(int));
      if( !dqI_highest )
      {
         free(dqV_highest);
         return TA_ALLOC_ERR;
      }

      hd_highest  = 0;
      cnt_highest = 0;

      /* Prime the deques with the bars preceding startIdx. */
      i = trailingIdx;
      while( i < startIdx )
      {
         tmp = inReal[i];
         j = (hd_highest + cnt_highest) & dqMask;
         k = (j + dqMask) & dqMask;
         while( cnt_highest > 0 && dqV_highest[k] <= tmp )
         {
            cnt_highest--;
            j = k;
            k = (j + dqMask) & dqMask;
         }
         dqV_highest[j] = tmp;
         dqI_highest[j] = i;
         cnt_highest++;
         i++;
      }

      while( today <= endIdx )
      {
         if( trailingIdx > dqI_highest[hd_highest] )
         {
            cnt_highest--;
            hd_highest = (hd_highest + 1) & dqMask;
         }

         tmp = inReal[today];
         j = (hd_highest + cnt_highest) & dqMask;
         k = (j + dqMask) & dqMask;
         while( cnt_highest > 0 && dqV_highest[k] <= tmp )
         {
            cnt_highest--;
            j = k;
            k = (j + dqMask) & dqMask;
         }
         dqV_highest[j] = tmp;
         dqI_highest[j] = today;
         cnt_highest++;

         highest = dqV_highest[hd_highest];
         outReal[outIdx++] = highest;
         trailingIdx++;
         today++;
      }

      free(dqV_highest);
      free(dqI_highest);
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
