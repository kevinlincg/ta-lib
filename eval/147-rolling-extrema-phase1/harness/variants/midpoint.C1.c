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
   double *dqV_highest;
   int *dqI_highest;
   double *dqV_lowest;
   int *dqI_lowest;
   int outIdx, nbInitialElementNeeded, trailingIdx, today, i, j, k, hd_highest, cnt_highest, hd_lowest, cnt_lowest, highestIdx, lowestIdx;

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
      /* Monotonic deque per extremum channel: a strictly monotone run of
       * candidate extrema.  Each bar is pushed once and popped once, so the
       * cost is O(1) amortized per bar for every input shape -- there is no
       * rescan and no input-dependent behaviour.  The deques hold COPIES of
       * the input values, so the input and the output may be the same buffer.
       */
      dqV_highest = malloc((optInTimePeriod) * sizeof(double));
      if( !dqV_highest )
         return TA_ALLOC_ERR;
      dqI_highest = malloc((optInTimePeriod) * sizeof(int));
      if( !dqI_highest )
      {
         free(dqV_highest);
         return TA_ALLOC_ERR;
      }
      dqV_lowest = malloc((optInTimePeriod) * sizeof(double));
      if( !dqV_lowest )
         return TA_ALLOC_ERR;
      dqI_lowest = malloc((optInTimePeriod) * sizeof(int));
      if( !dqI_lowest )
      {
         free(dqV_lowest);
         return TA_ALLOC_ERR;
      }

      hd_highest  = 0;
      cnt_highest = 0;
      hd_lowest  = 0;
      cnt_lowest = 0;

      /* Prime the deques with the bars preceding startIdx. */
      i = trailingIdx;
      while( i < startIdx )
      {
         tmpHigh = inReal[i];
         j = hd_highest + cnt_highest;
         if( j >= optInTimePeriod )
         {
            j = j - optInTimePeriod;
         }
         k = j + optInTimePeriod - 1;
         if( k >= optInTimePeriod )
         {
            k = k - optInTimePeriod;
         }
         while( cnt_highest > 0 && dqV_highest[k] <= tmpHigh )
         {
            cnt_highest--;
            j = k;
            k = j + optInTimePeriod - 1;
            if( k >= optInTimePeriod )
            {
               k = k - optInTimePeriod;
            }
         }
         dqV_highest[j] = tmpHigh;
         dqI_highest[j] = i;
         cnt_highest++;

         tmpLow = inReal[i];
         j = hd_lowest + cnt_lowest;
         if( j >= optInTimePeriod )
         {
            j = j - optInTimePeriod;
         }
         k = j + optInTimePeriod - 1;
         if( k >= optInTimePeriod )
         {
            k = k - optInTimePeriod;
         }
         while( cnt_lowest > 0 && dqV_lowest[k] >= tmpLow )
         {
            cnt_lowest--;
            j = k;
            k = j + optInTimePeriod - 1;
            if( k >= optInTimePeriod )
            {
               k = k - optInTimePeriod;
            }
         }
         dqV_lowest[j] = tmpLow;
         dqI_lowest[j] = i;
         cnt_lowest++;
         i++;
      }

      while( today <= endIdx )
      {
         /* At most one entry per deque can leave the window per bar. */
         if( trailingIdx > dqI_highest[hd_highest] )
         {
            cnt_highest--;
            hd_highest++;
            if( hd_highest == optInTimePeriod )
            {
               hd_highest = 0;
            }
         }

         tmpHigh = inReal[today];
         j = hd_highest + cnt_highest;
         if( j >= optInTimePeriod )
         {
            j = j - optInTimePeriod;
         }
         k = j + optInTimePeriod - 1;
         if( k >= optInTimePeriod )
         {
            k = k - optInTimePeriod;
         }
         while( cnt_highest > 0 && dqV_highest[k] <= tmpHigh )
         {
            cnt_highest--;
            j = k;
            k = j + optInTimePeriod - 1;
            if( k >= optInTimePeriod )
            {
               k = k - optInTimePeriod;
            }
         }
         dqV_highest[j] = tmpHigh;
         dqI_highest[j] = today;
         cnt_highest++;

         if( trailingIdx > dqI_lowest[hd_lowest] )
         {
            cnt_lowest--;
            hd_lowest++;
            if( hd_lowest == optInTimePeriod )
            {
               hd_lowest = 0;
            }
         }

         tmpLow = inReal[today];
         j = hd_lowest + cnt_lowest;
         if( j >= optInTimePeriod )
         {
            j = j - optInTimePeriod;
         }
         k = j + optInTimePeriod - 1;
         if( k >= optInTimePeriod )
         {
            k = k - optInTimePeriod;
         }
         while( cnt_lowest > 0 && dqV_lowest[k] >= tmpLow )
         {
            cnt_lowest--;
            j = k;
            k = j + optInTimePeriod - 1;
            if( k >= optInTimePeriod )
            {
               k = k - optInTimePeriod;
            }
         }
         dqV_lowest[j] = tmpLow;
         dqI_lowest[j] = today;
         cnt_lowest++;

         highest = dqV_highest[hd_highest];
         lowest = dqV_lowest[hd_lowest];
         outReal[outIdx++] = (highest+lowest)/2.0;
         trailingIdx++;
         today++;
      }

      free(dqV_highest);
      free(dqI_highest);
      free(dqV_lowest);
      free(dqI_lowest);
   }
   else
   {
      highestIdx  = -1;
      highest     = 0.0;
      lowestIdx  = -1;
      lowest     = 0.0;

      while( today <= endIdx )
      {
         tmpHigh = inReal[today];
         tmpLow = inReal[today];

         if( highestIdx < trailingIdx )
         {
            highestIdx = trailingIdx;
            highest = inReal[highestIdx];
            i = highestIdx;
            TA_UNROLL(4)
            while( ++i<=today )
            {
               tmpHigh = inReal[i];
               if( tmpHigh > highest )
               {
                  highestIdx = i;
                  highest = tmpHigh;
               }
            }
         }
         else if( tmpHigh >= highest )
         {
            highestIdx = today;
            highest = tmpHigh;
         }

         if( lowestIdx < trailingIdx )
         {
            lowestIdx = trailingIdx;
            lowest = inReal[lowestIdx];
            i = lowestIdx;
            TA_UNROLL(4)
            while( ++i<=today )
            {
               tmpLow = inReal[i];
               if( tmpLow < lowest )
               {
                  lowestIdx = i;
                  lowest = tmpLow;
               }
            }
         }
         else if( tmpLow <= lowest )
         {
            lowestIdx = today;
            lowest = tmpLow;
         }

         outReal[outIdx++] = (highest+lowest)/2.0;
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
