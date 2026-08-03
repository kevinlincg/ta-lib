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
   double highest, lowest, tmpHigh, tmpLow, pre_highest, pre_lowest;
   double *suf_highest;
   double *suf_lowest;
   int outIdx, nbInitialElementNeeded, trailingIdx, today, i, blockStart, highestIdx, lowestIdx;

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
      /* Van Herk / Gil-Werman block scan, per-sample form.
       *
       * Cut the range into blocks of optInTimePeriod bars aligned on the first
       * window's trailing edge.  Every length-p window either IS a block or
       * straddles exactly one block boundary, so its extremum is
       *
       *     combine( suffix-extremum of the older block from trailingIdx,
       *              prefix-extremum of the newer block up to today )
       *
       * The suffix array of a block is materialised once per p bars, on the bar
       * where the window coincides with that block; the prefix is a running
       * scalar.  Two comparisons per bar in the steady state, no rescan, no
       * input-dependent behaviour.  The suffix arrays hold COPIES, so the input
       * and the output may be the same buffer.
       */
      suf_highest = malloc((optInTimePeriod) * sizeof(double));
      if( !suf_highest )
         return TA_ALLOC_ERR;
      suf_lowest = malloc((optInTimePeriod) * sizeof(double));
      if( !suf_lowest )
         return TA_ALLOC_ERR;

      blockStart = trailingIdx;

      while( today <= endIdx )
      {
         if( trailingIdx == blockStart )
         {
            /* today == blockStart + optInTimePeriod - 1: the window is exactly
             * this block.  Materialise its suffix extrema backwards.
             */
            i = today;
            highest = inReal[i];
            suf_highest[i - blockStart] = highest;
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
            highest = suf_highest[0];

            i = today;
            lowest = inReal[i];
            suf_lowest[i - blockStart] = lowest;
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
            lowest = suf_lowest[0];
         }
         else
         {
            tmpHigh = inReal[today];
            if( trailingIdx == blockStart + 1 )
            {
               pre_highest = tmpHigh;
            }
            else if( tmpHigh > pre_highest )
            {
               pre_highest = tmpHigh;
            }
            highest = suf_highest[trailingIdx - blockStart];
            if( pre_highest > highest )
            {
               highest = pre_highest;
            }

            tmpLow = inReal[today];
            if( trailingIdx == blockStart + 1 )
            {
               pre_lowest = tmpLow;
            }
            else if( tmpLow < pre_lowest )
            {
               pre_lowest = tmpLow;
            }
            lowest = suf_lowest[trailingIdx - blockStart];
            if( pre_lowest < lowest )
            {
               lowest = pre_lowest;
            }
         }

         outReal[outIdx++] = (highest+lowest)/2.0;

         trailingIdx++;
         if( trailingIdx == blockStart + optInTimePeriod )
         {
            blockStart = blockStart + optInTimePeriod;
         }
         today++;
      }

      free(suf_highest);
      free(suf_lowest);
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
