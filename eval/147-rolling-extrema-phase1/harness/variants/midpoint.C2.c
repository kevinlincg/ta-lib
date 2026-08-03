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
   double *suf_highest;
   double *pre_highest;
   double *suf_lowest;
   double *pre_lowest;
   int outIdx, nbInitialElementNeeded, trailingIdx, today, i, blockStart, nAvail, m, highestIdx, lowestIdx;

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
      /* Van Herk / Gil-Werman block scan, block-batched form.
       *
       * Same decomposition as the per-sample form, but the p outputs belonging
       * to one block boundary are produced together: one backward pass builds
       * the older block's suffix extrema, one forward pass builds the newer
       * block's prefix extrema, and a third branch-free pass combines them.
       * The three passes are straight-line loops with no data-dependent
       * branching, which is what lets a compiler vectorize them.  All three
       * scratch arrays hold COPIES, so the input and the output may alias.
       */
      suf_highest = malloc((optInTimePeriod) * sizeof(double));
      if( !suf_highest )
         return TA_ALLOC_ERR;
      pre_highest = malloc((optInTimePeriod) * sizeof(double));
      if( !pre_highest )
         return TA_ALLOC_ERR;
      suf_lowest = malloc((optInTimePeriod) * sizeof(double));
      if( !suf_lowest )
         return TA_ALLOC_ERR;
      pre_lowest = malloc((optInTimePeriod) * sizeof(double));
      if( !pre_lowest )
         return TA_ALLOC_ERR;

      blockStart = trailingIdx;

      while( today <= endIdx )
      {
         /* Suffix extrema of the block [blockStart, blockStart+p-1].  It is
          * fully available: today == blockStart + p - 1 <= endIdx here.
          */
         i = blockStart + optInTimePeriod - 1;
         highest = inReal[i];
         suf_highest[optInTimePeriod - 1] = highest;
         TA_UNROLL(4)
         while( i > blockStart )
         {
            i--;
            tmpHigh = inReal[i];
            if( tmpHigh > highest )
            {
               highest = tmpHigh;
            }
            suf_highest[i - blockStart] = highest;
         }

         i = blockStart + optInTimePeriod - 1;
         lowest = inReal[i];
         suf_lowest[optInTimePeriod - 1] = lowest;
         TA_UNROLL(4)
         while( i > blockStart )
         {
            i--;
            tmpLow = inReal[i];
            if( tmpLow < lowest )
            {
               lowest = tmpLow;
            }
            suf_lowest[i - blockStart] = lowest;
         }

         highest = suf_highest[0];
         lowest = suf_lowest[0];
         outReal[outIdx++] = (highest+lowest)/2.0;
         trailingIdx++;
         today++;
         if( today > endIdx )
         {
            blockStart = blockStart + optInTimePeriod;
         }
         else
         {
            /* Prefix extrema of the next block, clamped to what remains. */
            nAvail = endIdx - (blockStart + optInTimePeriod) + 1;
            if( nAvail > optInTimePeriod - 1 )
            {
               nAvail = optInTimePeriod - 1;
            }
            highest = inReal[blockStart + optInTimePeriod];
            pre_highest[0] = highest;
            i = 1;
            TA_UNROLL(4)
            while( i < nAvail )
            {
               tmpHigh = inReal[blockStart + optInTimePeriod + i];
               if( tmpHigh > highest )
               {
                  highest = tmpHigh;
               }
               pre_highest[i] = highest;
               i++;
            }

            lowest = inReal[blockStart + optInTimePeriod];
            pre_lowest[0] = lowest;
            i = 1;
            TA_UNROLL(4)
            while( i < nAvail )
            {
               tmpLow = inReal[blockStart + optInTimePeriod + i];
               if( tmpLow < lowest )
               {
                  lowest = tmpLow;
               }
               pre_lowest[i] = lowest;
               i++;
            }

            m = 1;
            while( m <= nAvail )
            {
               highest = suf_highest[m];
               if( pre_highest[m - 1] > highest )
               {
                  highest = pre_highest[m - 1];
               }

               lowest = suf_lowest[m];
               if( pre_lowest[m - 1] < lowest )
               {
                  lowest = pre_lowest[m - 1];
               }
               outReal[outIdx++] = (highest+lowest)/2.0;
               m++;
            }
            trailingIdx = trailingIdx + nAvail;
            today = today + nAvail;
            blockStart = blockStart + optInTimePeriod;
         }
      }

      free(suf_highest);
      free(pre_highest);
      free(suf_lowest);
      free(pre_lowest);
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
