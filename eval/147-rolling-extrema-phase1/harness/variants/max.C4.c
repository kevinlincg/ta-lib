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
   double highest, tmp, bkM_highest;
   double *bkV_highest;
   double *ftM_highest;
   int outIdx, nbInitialElementNeeded, trailingIdx, today, i, bkN_highest, ftN_highest, ftP_highest, highestIdx;

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

      bkN_highest = 0;
      ftN_highest = 0;
      ftP_highest = 0;
      bkM_highest = 0.0;

      /* Prime with the bars preceding startIdx. */
      i = trailingIdx;
      while( i < startIdx )
      {
         tmp = inReal[i];
         if( bkN_highest == 0 )
         {
            bkM_highest = tmp;
         }
         else if( tmp > bkM_highest )
         {
            bkM_highest = tmp;
         }
         bkV_highest[bkN_highest] = tmp;
         bkN_highest++;
         i++;
      }

      while( today <= endIdx )
      {
         tmp = inReal[today];
         if( bkN_highest == 0 )
         {
            bkM_highest = tmp;
         }
         else if( tmp > bkM_highest )
         {
            bkM_highest = tmp;
         }
         bkV_highest[bkN_highest] = tmp;
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

         outReal[outIdx++] = highest;

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
               tmp = bkV_highest[i];
               if( tmp > highest )
               {
                  highest = tmp;
               }
               ftM_highest[i] = highest;
            }
            ftN_highest = bkN_highest;
            ftP_highest = 0;
            bkN_highest = 0;
         }
         ftP_highest++;
         trailingIdx++;
         today++;
      }

      free(bkV_highest);
      free(ftM_highest);
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
