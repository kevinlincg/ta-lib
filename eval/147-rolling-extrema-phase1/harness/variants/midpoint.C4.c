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
   double highest, lowest, tmpHigh, tmpLow, bkM_highest, bkM_lowest;
   double *bkV_highest;
   double *ftM_highest;
   double *bkV_lowest;
   double *ftM_lowest;
   int outIdx, nbInitialElementNeeded, trailingIdx, today, i, bkN_highest, ftN_highest, ftP_highest, bkN_lowest, ftN_lowest, ftP_lowest, highestIdx, lowestIdx;

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
      /* Two-stack queue: amortized O(1) with no per-element index
       * bookkeeping.  The back stack holds the raw values of the newest run
       * plus a running extremum scalar; the front stack holds, per entry, the
       * extremum of that entry and every newer front entry, so retiring the
       * oldest bar is a pointer bump.  When the front runs out, the back is
       * drained into it in one O(p) pass -- once every p bars, hence O(1)
       * amortized, with an O(p) latency spike on the draining bar.  Both
       * stacks hold COPIES, so the input and the output may alias.
       */
      bkV_highest = malloc((optInTimePeriod) * sizeof(double));
      if( !bkV_highest )
         return TA_ALLOC_ERR;
      ftM_highest = malloc((optInTimePeriod) * sizeof(double));
      if( !ftM_highest )
         return TA_ALLOC_ERR;
      bkV_lowest = malloc((optInTimePeriod) * sizeof(double));
      if( !bkV_lowest )
         return TA_ALLOC_ERR;
      ftM_lowest = malloc((optInTimePeriod) * sizeof(double));
      if( !ftM_lowest )
         return TA_ALLOC_ERR;

      bkN_highest = 0;
      ftN_highest = 0;
      ftP_highest = 0;
      bkM_highest = 0.0;
      bkN_lowest = 0;
      ftN_lowest = 0;
      ftP_lowest = 0;
      bkM_lowest = 0.0;

      /* Prime with the bars preceding startIdx. */
      i = trailingIdx;
      while( i < startIdx )
      {
         tmpHigh = inReal[i];
         if( bkN_highest == 0 )
         {
            bkM_highest = tmpHigh;
         }
         else if( tmpHigh > bkM_highest )
         {
            bkM_highest = tmpHigh;
         }
         bkV_highest[bkN_highest] = tmpHigh;
         bkN_highest++;

         tmpLow = inReal[i];
         if( bkN_lowest == 0 )
         {
            bkM_lowest = tmpLow;
         }
         else if( tmpLow < bkM_lowest )
         {
            bkM_lowest = tmpLow;
         }
         bkV_lowest[bkN_lowest] = tmpLow;
         bkN_lowest++;
         i++;
      }

      while( today <= endIdx )
      {
         tmpHigh = inReal[today];
         if( bkN_highest == 0 )
         {
            bkM_highest = tmpHigh;
         }
         else if( tmpHigh > bkM_highest )
         {
            bkM_highest = tmpHigh;
         }
         bkV_highest[bkN_highest] = tmpHigh;
         bkN_highest++;

         if( ftP_highest < ftN_highest )
         {
            highest = ftM_highest[ftP_highest];
            if( bkM_highest > highest )
            {
               highest = bkM_highest;
            }
         }
         else
         {
            highest = bkM_highest;
         }

         tmpLow = inReal[today];
         if( bkN_lowest == 0 )
         {
            bkM_lowest = tmpLow;
         }
         else if( tmpLow < bkM_lowest )
         {
            bkM_lowest = tmpLow;
         }
         bkV_lowest[bkN_lowest] = tmpLow;
         bkN_lowest++;

         if( ftP_lowest < ftN_lowest )
         {
            lowest = ftM_lowest[ftP_lowest];
            if( bkM_lowest < lowest )
            {
               lowest = bkM_lowest;
            }
         }
         else
         {
            lowest = bkM_lowest;
         }

         outReal[outIdx++] = (highest+lowest)/2.0;

         if( ftP_highest == ftN_highest )
         {
            /* Front exhausted: drain the back stack into it, annotating each
             * entry with the extremum of itself and every NEWER back entry.
             * One O(p) pass every p bars.
             */
            i = bkN_highest - 1;
            highest = bkV_highest[i];
            ftM_highest[i] = highest;
            TA_UNROLL(4)
            while( i > 0 )
            {
               i--;
               tmpHigh = bkV_highest[i];
               if( tmpHigh > highest )
               {
                  highest = tmpHigh;
               }
               ftM_highest[i] = highest;
            }
            ftN_highest = bkN_highest;
            ftP_highest = 0;
            bkN_highest = 0;
         }
         ftP_highest++;

         if( ftP_lowest == ftN_lowest )
         {
            /* Front exhausted: drain the back stack into it, annotating each
             * entry with the extremum of itself and every NEWER back entry.
             * One O(p) pass every p bars.
             */
            i = bkN_lowest - 1;
            lowest = bkV_lowest[i];
            ftM_lowest[i] = lowest;
            TA_UNROLL(4)
            while( i > 0 )
            {
               i--;
               tmpLow = bkV_lowest[i];
               if( tmpLow < lowest )
               {
                  lowest = tmpLow;
               }
               ftM_lowest[i] = lowest;
            }
            ftN_lowest = bkN_lowest;
            ftP_lowest = 0;
            bkN_lowest = 0;
         }
         ftP_lowest++;
         trailingIdx++;
         today++;
      }

      free(bkV_highest);
      free(ftM_highest);
      free(bkV_lowest);
      free(ftM_lowest);
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
