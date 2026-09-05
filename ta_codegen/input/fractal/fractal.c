/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  KL       Kevin Lin
 *
 * Change history:
 *
 *  MMDDYY BY     Description
 *  -------------------------------------------------------------------
 *  090526 KL     First version (issue #371).
 */

int fractal_lookback(int optInLeftBars, int optInRightBars)
{
   return optInLeftBars + optInRightBars;
}

TA_RetCode fractal(int startIdx, int endIdx,
   const double inHigh[],
   const double inLow[],
   int optInLeftBars,
   int optInRightBars,
   int *outBegIdx, int *outNBElement,
   int outSwingHigh[],
   int outSwingLow[])
{
   double candHigh, candLow;
   int outIdx, lookbackTotal;
   int today, candIdx, trailingIdx, i, swingHigh, swingLow;

   /* A bar is a swing high when its high is STRICTLY above the high of
    * every one of the optInLeftBars bars before it and of the
    * optInRightBars bars after it; the swing low is the mirror on inLow.
    * The verdict on a candidate bar can only be reached once its right
    * arm exists, so it is reported at the confirmation bar
    * candidate+optInRightBars -- that offset is what makes the function
    * causal, and it is where both oracle libraries report it too.
    *
    * A window that ties on either arm is not a pivot. Both arms use the
    * same strict comparison; the asymmetric >-left / >=-right variant some
    * charting docs describe would flag the later bar of a flat top.
    */
   lookbackTotal = optInLeftBars + optInRightBars;

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

   /* The window is rescanned per bar (as MIN/MAX do when their cached
    * extremum leaves): a cached running extremum cannot answer this
    * question, because the candidate sits in the MIDDLE of the window and
    * a tie has to be distinguished from a strict win.
    *
    * The integer outputs can never share a real input's buffer -- different
    * element type; issue #130.
    */
   outIdx = 0;
   today = startIdx;
   trailingIdx = startIdx - lookbackTotal;

   while( today <= endIdx )
   {
      candIdx     = trailingIdx + optInLeftBars;
      candHigh    = inHigh[candIdx];
      candLow     = inLow[candIdx];
      swingHigh   = 100;
      swingLow    = 100;

      i = trailingIdx;
      TA_UNROLL(4)
      while( i < candIdx )
      {
         if( inHigh[i] >= candHigh )
            swingHigh = 0;
         if( inLow[i] <= candLow )
            swingLow = 0;
         i++;
      }

      i = candIdx + 1;
      TA_UNROLL(4)
      while( i <= today )
      {
         if( inHigh[i] >= candHigh )
            swingHigh = 0;
         if( inLow[i] <= candLow )
            swingLow = 0;
         i++;
      }

      outSwingHigh[outIdx] = swingHigh;
      outSwingLow[outIdx]  = swingLow;
      outIdx++;
      trailingIdx++;
      today++;
   }

   /* Keep the outBegIdx relative to the
    * caller input before returning.
    */
   *outBegIdx    = startIdx;
   *outNBElement = outIdx;

   return TA_SUCCESS;
}
