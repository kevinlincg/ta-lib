/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  KL       Kevin Lin
 *
 * Change history:
 *
 *  MMDDYY BY   Description
 *  -------------------------------------------------------------------
 *  090426 KL   First version (issue #348). Wilder's smoothing, already
 *              embedded in ATR/RSI/ADX, exposed as a standalone MA.
 *
 */

int rma_lookback(int optInTimePeriod)
{
   return optInTimePeriod - 1 + TA_GetUnstablePeriod(TA_FUNC_UNST_RMA);
}

TA_RetCode rma(int startIdx, int endIdx,
   const double inReal[],
   int optInTimePeriod,
   int *outBegIdx, int *outNBElement,
   double outReal[])
{
   double tempReal, prevMA, wAlpha, wBeta;
   int i, today, outIdx, lookbackTotal;

   *outBegIdx = 0;
   *outNBElement = 0;

   /* Identify the minimum number of price bar needed
    * to calculate at least one output.
    */
   lookbackTotal = rma_lookback( optInTimePeriod );

   /* Move up the start index if there is not
    * enough initial data.
    */
   if( startIdx < lookbackTotal )
      startIdx = lookbackTotal;

   /* Make sure there is still something to evaluate. */
   if( startIdx > endIdx )
      return TA_SUCCESS;

   /* This IS the smoothing inside TA_ATR, TA_RSI, TA_ADX and TA_PLUS_DM,
    * and TA_RMA(TA_TRANGE(h,l,c), n) is TA_ATR(n) bit for bit -- with the
    * two unstable periods equal, which is a contract the caller has to
    * hold up, since each function owns its own knob.
    *
    * That differential is the strongest gate this function has, and it is
    * the ONLY thing that sees the three choices below. Every one of them
    * therefore has to stay spelled exactly as ta_codegen/input/atr/atr.c
    * spells it:
    *
    *  - wAlpha derived FROM wBeta, never the reverse. Only that order
    *    makes wAlpha + wBeta exactly 1 (Sterbenz -- wBeta lands in
    *    [0.5, 1)); the alpha = 1.0/period spelling misses at 199982 of the
    *    first 200000 periods and mismatches shipped ATR on nearly every
    *    bar. The pair is exactly (1, 0) at period 1 -- hence no period-1
    *    arm below.
    *  - The seed is the first 'period' values summed from 0.0 in input
    *    order, then divided by the period.
    *  - The Wilder step is ONE statement. Splitting it unfuses the
    *    multiply-add and puts a second latency on the recurrence.
    *    The fused operand ORDER matters too -- fma(wBeta, prevMA,
    *    wAlpha * x) and fma(wAlpha, x, wBeta * prevMA) round
    *    differently -- but it is not spelled here: the generator
    *    canonicalizes the sum and elects the accumulator as the fused
    *    multiplicand, so writing the two products the other way round
    *    emits byte-identical C. To watch the differential catch an
    *    order change, patch the generated src/ta_func/ta_RMA.c.
    *
    * In-place (outReal being inReal) is supported: each bar's input is
    * read before that bar's output is written, and the output index never
    * exceeds the bar index of any remaining read.
    */
   wBeta  = (double)(optInTimePeriod - 1) / (double)optInTimePeriod;
   wAlpha = 1.0 - wBeta;

   *outBegIdx = startIdx;

   /* Seed with a simple average of the first 'period' values. */
   today = startIdx - lookbackTotal;
   i = optInTimePeriod;
   tempReal = 0.0;
   while( i-- > 0 )
      tempReal += inReal[today++];

   prevMA = tempReal / optInTimePeriod;

   /* Skip the unstable period. */
   while( today <= startIdx )
   {
      prevMA = wAlpha * inReal[today] + wBeta * prevMA;
      today++;
   }

   outReal[0] = prevMA;
   outIdx = 1;

   while( today <= endIdx )
   {
      prevMA = wAlpha * inReal[today] + wBeta * prevMA;
      today++;
      outReal[outIdx++] = prevMA;
   }

   *outNBElement = outIdx;

   return TA_SUCCESS;
}
