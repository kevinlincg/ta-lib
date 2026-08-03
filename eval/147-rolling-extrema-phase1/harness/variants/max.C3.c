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
   double highest, tmp, pre_highest;
   double *suf_highest;
   int outIdx, nbInitialElementNeeded, trailingIdx, today, i, blockStart, highestIdx;

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
               tmp = inReal[i];
               if( tmp > highest )
               {
                  highest = tmp;
               }
               suf_highest[i - blockStart] = highest;
            }
            highest = suf_highest[0];
         }
         else
         {
            tmp = inReal[today];
            if( trailingIdx == blockStart + 1 )
            {
               pre_highest = tmp;
            }
            else if( tmp > pre_highest )
            {
               pre_highest = tmp;
            }
            highest = suf_highest[trailingIdx - blockStart];
            if( pre_highest > highest )
            {
               highest = pre_highest;
            }
         }

         outReal[outIdx++] = highest;

         trailingIdx++;
         if( trailingIdx == blockStart + optInTimePeriod )
         {
            blockStart = blockStart + optInTimePeriod;
         }
         today++;
      }

      free(suf_highest);
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
