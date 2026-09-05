/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  MF       Mario Fortier
 *
 *
 * Change history:
 *
 *  MMDDYY BY     Description
 *  -------------------------------------------------------------------
 *  120802 MF     Template creation.
 *  052603 MF     Adapt code to compile with .NET Managed C++
 *  062704 MF     Fix limit case to avoid divid by zero (or by
 *                a value close to zero induce by the imprecision
 *                of floating points).
 *  070226 MF,CC  Allow period of 1: output is a copy of the input,
 *                consistent with TA_MA (issues #48, #59). The natural
 *                KAMA math at period=1 would be a fixed-alpha EMA
 *                (efficiency ratio is always 1), which would disagree
 *                with TA_MA's period-1 copy, so identity is explicit.
 *  082326 MF,CC  Fix #253. Recognize a flat window by counting bars and drop
 *                the fixed TA_IS_ZERO band beside the efficiency ratio, which
 *                forced the fastest adaptation on any instrument quoted small
 *                enough to fall under it.
 *  090526 KL     Gate the efficiency ratio on its own denominator (#381). The
 *                bar count is a proxy for it, and a sliding sum reaches 0.0 on
 *                windows the count calls live.
 */

int kama_lookback(int optInTimePeriod)
{
   if( optInTimePeriod == 1 )
      return TA_GetUnstablePeriod(TA_FUNC_UNST_KAMA);

   return optInTimePeriod + TA_GetUnstablePeriod(TA_FUNC_UNST_KAMA);
}

TA_RetCode kama(int startIdx, int endIdx,
   const double inReal[],
   int optInTimePeriod,
   int *outBegIdx, int *outNBElement,
   double outReal[])
{
   const double constMax = 2.0/(30.0+1.0);
   const double constDiff = 2.0/(2.0+1.0) - constMax;

   double tempReal, tempReal2;
   double sumROC1, periodROC, prevKAMA;
   int i, today, outIdx, lookbackTotal;
   int trailingIdx;
   int nullRun;
   double trailingValue;

   /* Default return values */
   *outBegIdx = 0;
   *outNBElement = 0;

   /* No smoothing at period of 1: the output is a copy of the input
    * (same convention as TA_MA for every MAType). The unstable period
    * still delays the first output for API consistency.
    */
   if( optInTimePeriod == 1 )
   {
      lookbackTotal = TA_GetUnstablePeriod(TA_FUNC_UNST_KAMA);
      if( startIdx < lookbackTotal )
         startIdx = lookbackTotal;
      if( startIdx > endIdx )
         return TA_SUCCESS;

      *outBegIdx = startIdx;
      outIdx = 0;
      today = startIdx;
      while( today <= endIdx )
         outReal[outIdx++] = inReal[today++];
      *outNBElement = outIdx;
      return TA_SUCCESS;
   }

   /* Identify the minimum number of price bar needed
    * to calculate at least one output.
    */
   lookbackTotal = optInTimePeriod + TA_GetUnstablePeriod(TA_FUNC_UNST_KAMA);

   /* Move up the start index if there is not
    * enough initial data.
    */
   if( startIdx < lookbackTotal )
      startIdx = lookbackTotal;

   /* Make sure there is still something to evaluate. */
   if( startIdx > endIdx )
   {
      *outBegIdx = 0;
      *outNBElement = 0;
      return TA_SUCCESS;
   }

   /* Initialize the variables by going through
    * the lookback period.
    */
   sumROC1 = 0.0;
   /* Consecutive 1-day changes of exactly zero, counted so that a flat window
    * can be recognized exactly (the shape #244 needed for MFI). sumROC1 cannot
    * answer that question itself once the window starts sliding: it is
    * maintained by add-then-subtract, so a window that has gone flat leaves it
    * holding rounding residue of arbitrary sign rather than zero, and the
    * efficiency ratio then divides that residue into itself.
    */
   nullRun = 0;
   today = startIdx-lookbackTotal;
   trailingIdx = today;
   i = optInTimePeriod;
   while( i-- > 0 )
   {
      tempReal  = inReal[today++];
      tempReal -= inReal[today];
      sumROC1  += fabs(tempReal);
      if( tempReal == 0.0 )
         nullRun++;
      else
         nullRun = 0;
   }

   /* At this point sumROC1 represent the
    * summation of the 1-day price difference
    * over the (optInTimePeriod-1)
    */

   /* Calculate the first KAMA */

   /* The yesterday price is used here as the previous KAMA. */
   prevKAMA = inReal[today-1];

   tempReal  = inReal[today];
   tempReal2 = inReal[trailingIdx++];
   periodROC = tempReal-tempReal2;

   /* Save the trailing value. Do this because inReal
    * and outReal can be pointers to the same buffer.
    */
   trailingValue = tempReal2;

   /* Calculate the efficiency ratio.
    *
    * Two thresholds, and they answer different questions.
    *
    * `sumROC1 <= periodROC` is the ratio clamp: it pins an up-move to exactly
    * 1.0 where FP would give 1.0000000000000002. It is scale-consistent (both
    * sides carry the quote unit), unlike the fixed TA_IS_ZERO band that used
    * to sit beside it -- that one declared the window flat, and forced the
    * fastest adaptation, for every window of an instrument quoted below it
    * (issue #253). A genuinely flat window is recognized by the exact bar
    * count instead.
    *
    * `sumROC1 <= 0.0` is the denominator test, and the clamp cannot serve as
    * one: it compares against the SIGNED numerator, so it is false for every
    * down move. Reaching it needs no flat window -- see the sliding sum's
    * comment below. Unreachable at THIS site, where a priming sum has only
    * ever had non-negative terms added to it, and written anyway so both
    * sites read as one rule.
    */
   if( sumROC1 <= 0.0 || sumROC1 <= periodROC )
      tempReal = 1.0;
   else
      tempReal = fabs(periodROC/sumROC1);

   /* Calculate the smoothing constant */
   tempReal  = (tempReal*constDiff)+constMax;
   tempReal *= tempReal;

   /* Calculate the KAMA like an EMA, using the
    * smoothing constant as the adaptive factor.
    */
   prevKAMA = ((inReal[today++]-prevKAMA)*tempReal) + prevKAMA;

   /* 'today' keep track of where the processing is within the
    * input.
    */

   /* Skip the unstable period. Do the whole processing
    * needed for KAMA, but do not write it in the output.
    */
   while( today <= startIdx )
   {
      tempReal  = inReal[today];
      tempReal2 = inReal[trailingIdx++];
      periodROC = tempReal-tempReal2;

      /* Adjust sumROC1:
       *  - Remove trailing ROC1
       *  - Add new ROC1
       */
      sumROC1 -= fabs(trailingValue-tempReal2);
      sumROC1 += fabs(tempReal-inReal[today-1]);

      /* Once a whole window of flat bars has gone by, every 1-day change it
       * spans is exactly zero, so the sum is known to be exactly zero and the
       * residue can be dropped -- a window that flat has periodROC == 0 too,
       * so the ratio is decided by 0 <= 0 and is 1.
       *
       * The purge does NOT make the sum's own value redundant. It fires on a
       * flat window, and a subtract-then-add sum reaches 0.0 on windows that
       * are not flat: a term absorbed on the way in (a 1e16 move parked in the
       * sum swallows a 1e-300 one) is subtracted later at full precision, and
       * the sum lands on exactly 0.0 with nullRun far below optInTimePeriod.
       * Hence the explicit `sumROC1 <= 0.0` at every division below.
       */
      if( tempReal - inReal[today-1] == 0.0 )
         nullRun++;
      else
         nullRun = 0;
      if( nullRun >= optInTimePeriod )
      {
         nullRun = optInTimePeriod;
         sumROC1 = 0.0;
      }

      /* Save the trailing value. Do this because inReal
       * and outReal can be pointers to the same buffer.
       */
      trailingValue = tempReal2;

      /* Calculate the efficiency ratio (denominator test first). */
      if( sumROC1 <= 0.0 || sumROC1 <= periodROC )
         tempReal = 1.0;
      else
         tempReal = fabs(periodROC/sumROC1);

      /* Calculate the smoothing constant */
      tempReal  = (tempReal*constDiff)+constMax;
      tempReal *= tempReal;

      /* Calculate the KAMA like an EMA, using the
       * smoothing constant as the adaptive factor.
       */
      prevKAMA = ((inReal[today++]-prevKAMA)*tempReal) + prevKAMA;
   }

   /* Write the first value. */
   outReal[0] = prevKAMA;
   outIdx = 1;
   *outBegIdx = today-1;

   /* Do the KAMA calculation for the requested range. */
   while( today <= endIdx )
   {
      tempReal  = inReal[today];
      tempReal2 = inReal[trailingIdx++];
      periodROC = tempReal-tempReal2;

      /* Adjust sumROC1:
       *  - Remove trailing ROC1
       *  - Add new ROC1
       */
      sumROC1 -= fabs(trailingValue-tempReal2);
      sumROC1 += fabs(tempReal-inReal[today-1]);

      /* Once a whole window of flat bars has gone by, every 1-day change it
       * spans is exactly zero, so the sum is known to be exactly zero and the
       * residue can be dropped -- a window that flat has periodROC == 0 too,
       * so the ratio is decided by 0 <= 0 and is 1.
       *
       * The purge does NOT make the sum's own value redundant. It fires on a
       * flat window, and a subtract-then-add sum reaches 0.0 on windows that
       * are not flat: a term absorbed on the way in (a 1e16 move parked in the
       * sum swallows a 1e-300 one) is subtracted later at full precision, and
       * the sum lands on exactly 0.0 with nullRun far below optInTimePeriod.
       * Hence the explicit `sumROC1 <= 0.0` at every division below.
       */
      if( tempReal - inReal[today-1] == 0.0 )
         nullRun++;
      else
         nullRun = 0;
      if( nullRun >= optInTimePeriod )
      {
         nullRun = optInTimePeriod;
         sumROC1 = 0.0;
      }

      /* Save the trailing value. Do this because inReal
       * and outReal can be pointers to the same buffer.
       */
      trailingValue = tempReal2;

      /* Calculate the efficiency ratio (denominator test first). */
      if( sumROC1 <= 0.0 || sumROC1 <= periodROC )
         tempReal = 1.0;
      else
         tempReal = fabs(periodROC / sumROC1);

      /* Calculate the smoothing constant */
      tempReal  = (tempReal*constDiff)+constMax;
      tempReal *= tempReal;

      /* Calculate the KAMA like an EMA, using the
       * smoothing constant as the adaptive factor.
       */
      prevKAMA = ((inReal[today++]-prevKAMA)*tempReal) + prevKAMA;
      outReal[outIdx++] = prevKAMA;
   }

   *outNBElement = outIdx;

   return TA_SUCCESS;
}
