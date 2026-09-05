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
 *  090526 KL     First version (issue #373).
 */

   /**
    * Number of leading input bars {@link Core#HA} consumes before it can
    * produce its first value.
    * <p>Equivalently, the index of the first bar with a value when the whole
    * series is requested. Feed at least {@code lookback + 1} bars to get any
    * output.
    * <p>This function is recursive, so the result also includes this
    * {@code Core}'s unstable-period setting — which is why it is an instance
    * method.
    *
    * @return The lookback, or {@code -1} if a parameter is out of range.
    */
   public int HA_Lookback( )
   {
      /* The unstable period is the ONLY lookback: bar 0 is computable on its
       * own, so with the knob at its default 0 every input bar produces an
       * output bar.
       */
      return this.unstablePeriod[FuncUnstId.HA.ordinal()] ;

   }
   RetCode HA_Impl( int startIdx,
                    int endIdx,
                    double inOpen[],
                    double inHigh[],
                    double inLow[],
                    double inClose[],
                    MInteger outBegIdx,
                    MInteger outNBElement,
                    double outHAOpen[],
                    double outHAHigh[],
                    double outHALow[],
                    double outHAClose[] )
   {
      int today = 0;
      int outIdx = 0;
      int lookbackTotal = 0;
      double haOpen = 0;
      double haClose = 0;
      double haHigh = 0;
      double haLow = 0;
      double prevHAOpen = 0;
      double prevHAClose = 0;
      double tempOpen = 0;
      double tempHigh = 0;
      double tempLow = 0;
      double tempClose = 0;
      if( (startIdx < 0) || (startIdx > MAX_INDEX) ) {
         return RetCode.OutOfRangeStartIndex ;
      }
      if( (endIdx < 0) || (endIdx > MAX_INDEX) || (endIdx < startIdx)) {
         return RetCode.OutOfRangeEndIndex ;
      }
      if( outHAOpen == outHAHigh || outHAOpen == outHALow || outHAOpen == outHAClose || outHAHigh == outHALow || outHAHigh == outHAClose || outHALow == outHAClose ) {
         return RetCode.BadParam ;
      }
      /* Heikin-Ashi ("average bar"): an OHLC-to-OHLC smoothing transform.
       *
       *   HA_close[i] = (open[i] + high[i] + low[i] + close[i]) / 4
       *   HA_open [0] = (open[0] + close[0]) / 2
       *   HA_open [i] = (HA_open[i-1] + HA_close[i-1]) / 2
       *   HA_high [i] = max(high[i], HA_open[i], HA_close[i])
       *   HA_low  [i] = min(low [i], HA_open[i], HA_close[i])
       *
       * Keep the four-term sum in THIS left-to-right order. It is the oracles'
       * association, and floating-point addition is not associative:
       * TA_AVGPRICE sums the same four terms as (H+L+C+O)/4 and differs from
       * HA_close in the last place on ordinary bars, so HA_close is not a
       * composed AVGPRICE call and the two must not be unified.
       *
       * Every operation is one addition or one division by an exact power of
       * two, so the transform is exact whenever its inputs are -- which is why
       * the external captures are frozen at tolerance 0 rather than a band.
       *
       * BOTH stability flags are declared, and they answer different questions.
       * `unstable_period` is the ABI knob: the open's recursion carries a factor
       * of 1/2 per bar, so a seed's influence halves every step and dies out
       * entirely within a few dozen bars -- a caller who spends them gets a
       * start-independent series (56 bars suffice on the regtest history, and
       * test_ha.c pins that). `path_dependent` declares
       * what is true at the DEFAULT of 0: the recursion re-seeds at the anchor,
       * so HA(3, 7, ...) starts its open at (open[3]+close[3])/2 rather than
       * warming up from bar 0. Without it the range-stability leg compares a
       * sub-range call against a full-range one and fails; with only it, the
       * knob that makes the two converge would not exist.
       */
      lookbackTotal = HA_Lookback();
      if( startIdx < lookbackTotal ) {
         startIdx = lookbackTotal;
      }
      /* Make sure there is still something to evaluate. */
      if( startIdx > endIdx ) {
         outBegIdx.value = 0;
         outNBElement.value = 0;
         return RetCode.Success ;
      }
      /* The seed is carried as a VIRTUAL previous candle rather than as a
       * special first iteration: seeding the pair with the anchor bar's own
       * open and close makes the uniform recursion produce
       * (open+close)/2 at that bar, which is the published seed. One loop body
       * then serves the anchor bar and every bar after it -- and that same pair
       * is the streaming tier's initial state, so batch and stream share one
       * definition of "where the recursion starts".
       */
      today = startIdx - lookbackTotal;
      prevHAOpen = inOpen[today];
      prevHAClose = inClose[today];
      /* Warm-up: advance the recursion across the unstable-period bars without
       * emitting. Empty at the default knob of 0.
       */
      while( today < startIdx ) {
         tempOpen = inOpen[today];
         tempHigh = inHigh[today];
         tempLow = inLow[today];
         tempClose = inClose[today];
         haOpen = (prevHAOpen + prevHAClose) / 2.0;
         haClose = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;
         prevHAOpen = haOpen;
         prevHAClose = haClose;
         today += 1;
      }
      outIdx = 0;
      while( today <= endIdx ) {
         tempOpen = inOpen[today];
         tempHigh = inHigh[today];
         tempLow = inLow[today];
         tempClose = inClose[today];
         haOpen = (prevHAOpen + prevHAClose) / 2.0;
         haClose = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;
         /* An elementwise clamp of the raw bar against the two body edges, so
          * the extremes carry no state of their own.
          */
         haHigh = tempHigh;
         if( haOpen > haHigh ) {
            haHigh = haOpen;
         }
         if( haClose > haHigh ) {
            haHigh = haClose;
         }
         haLow = tempLow;
         if( haOpen < haLow ) {
            haLow = haOpen;
         }
         if( haClose < haLow ) {
            haLow = haClose;
         }
         /* Written only after this bar's four inputs are in locals above: an
          * output buffer is allowed to alias any input, and output slot k lands
          * on input bar k, which is at or behind `today`.
          */
         outHAOpen[outIdx] = haOpen;
         outHAHigh[outIdx] = haHigh;
         outHALow[outIdx] = haLow;
         outHAClose[outIdx] = haClose;
         outIdx += 1;
         prevHAOpen = haOpen;
         prevHAClose = haClose;
         today += 1;
      }
      outBegIdx.value = startIdx;
      outNBElement.value = outIdx;
      return RetCode.Success ;
   }
   RetCode HA_Impl( int startIdx,
                    int endIdx,
                    float inOpen[],
                    float inHigh[],
                    float inLow[],
                    float inClose[],
                    MInteger outBegIdx,
                    MInteger outNBElement,
                    double outHAOpen[],
                    double outHAHigh[],
                    double outHALow[],
                    double outHAClose[] )
   {
      int today = 0;
      int outIdx = 0;
      int lookbackTotal = 0;
      double haOpen = 0;
      double haClose = 0;
      double haHigh = 0;
      double haLow = 0;
      double prevHAOpen = 0;
      double prevHAClose = 0;
      double tempOpen = 0;
      double tempHigh = 0;
      double tempLow = 0;
      double tempClose = 0;
      if( (startIdx < 0) || (startIdx > MAX_INDEX) ) {
         return RetCode.OutOfRangeStartIndex ;
      }
      if( (endIdx < 0) || (endIdx > MAX_INDEX) || (endIdx < startIdx)) {
         return RetCode.OutOfRangeEndIndex ;
      }
      if( outHAOpen == outHAHigh || outHAOpen == outHALow || outHAOpen == outHAClose || outHAHigh == outHALow || outHAHigh == outHAClose || outHALow == outHAClose ) {
         return RetCode.BadParam ;
      }
      lookbackTotal = HA_Lookback();
      if( startIdx < lookbackTotal ) {
         startIdx = lookbackTotal;
      }
      if( startIdx > endIdx ) {
         outBegIdx.value = 0;
         outNBElement.value = 0;
         return RetCode.Success ;
      }
      today = startIdx - lookbackTotal;
      prevHAOpen = (double)inOpen[today];
      prevHAClose = (double)inClose[today];
      while( today < startIdx ) {
         tempOpen = (double)inOpen[today];
         tempHigh = (double)inHigh[today];
         tempLow = (double)inLow[today];
         tempClose = (double)inClose[today];
         haOpen = (prevHAOpen + prevHAClose) / 2.0;
         haClose = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;
         prevHAOpen = haOpen;
         prevHAClose = haClose;
         today += 1;
      }
      outIdx = 0;
      while( today <= endIdx ) {
         tempOpen = (double)inOpen[today];
         tempHigh = (double)inHigh[today];
         tempLow = (double)inLow[today];
         tempClose = (double)inClose[today];
         haOpen = (prevHAOpen + prevHAClose) / 2.0;
         haClose = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;
         haHigh = tempHigh;
         if( haOpen > haHigh ) {
            haHigh = haOpen;
         }
         if( haClose > haHigh ) {
            haHigh = haClose;
         }
         haLow = tempLow;
         if( haOpen < haLow ) {
            haLow = haOpen;
         }
         if( haClose < haLow ) {
            haLow = haClose;
         }
         outHAOpen[outIdx] = haOpen;
         outHAHigh[outIdx] = haHigh;
         outHALow[outIdx] = haLow;
         outHAClose[outIdx] = haClose;
         outIdx += 1;
         prevHAOpen = haOpen;
         prevHAClose = haClose;
         today += 1;
      }
      outBegIdx.value = startIdx;
      outNBElement.value = outIdx;
      return RetCode.Success ;
   }
   /**
    * Heikin-Ashi Candles: an OHLC-to-OHLC smoothing transform. Each output bar
    * is a synthetic candle whose close is the raw bar's average price, whose
    * open is the midpoint of the previous synthetic candle, and whose extremes
    * clamp the raw extremes against those two. Consecutive candles share a body
    * edge by construction, which is what suppresses the single-bar noise a raw
    * candle chart shows. The transform is a *filter*, not an oscillator: it
    * returns prices in the input's own units and is plotted in place of the raw
    * candles.
    * <p><b>Formula</b>
    * <pre>{@code
    * ```
    * HA_close[i] = (open[i] + high[i] + low[i] + close[i]) / 4
    * HA_open[0]  = (open[0] + close[0]) / 2
    * HA_open[i]  = (HA_open[i-1] + HA_close[i-1]) / 2
    * HA_high[i]  = max(high[i], HA_open[i], HA_close[i])
    * HA_low[i]   = min(low[i],  HA_open[i], HA_close[i])
    * ```
    * Every operation is an addition or a division by an exact power of two, so the transform is exact whenever its inputs are.
    * `HA_close` is **not** [`AVGPRICE`](/functions/avgprice). The four terms are the same, but the summation order is not, and floating-point addition is not associative: `AVGPRICE` sums `(H+L+C+O)`, this sums `(O+H+L+C)`, and the two differ in the last place on ordinary bars.
    * }</pre>
    * <p>Values are written only where the indicator is defined. The returned
    * {@link OutRange} says where they start and how many there are; nothing
    * outside that range is touched, and the library never pads with NaN. A
    * valid range shorter than {@link Core#HA_Lookback} is a <b>success with no
    * values</b> ({@code count() == 0}), not an error.
    *
    * @param startIdx First bar of the requested range (inclusive).
    * @param endIdx Last bar of the requested range (inclusive).
    * @param inOpen Open price of each bar.
    * @param inHigh High price of each bar.
    * @param inLow Low price of each bar.
    * @param inClose Close price of each bar.
    * @param outHAOpen Heikin-Ashi open, the previous synthetic candle's
    *        midpoint. Must hold at least {@code endIdx - startIdx + 1} values.
    * @param outHAHigh Heikin-Ashi high, the raw high clamped up by the
    *        synthetic body. Must hold at least {@code endIdx - startIdx + 1} values.
    * @param outHALow Heikin-Ashi low, the raw low clamped down by the synthetic
    *        body. Must hold at least {@code endIdx - startIdx + 1} values.
    * @param outHAClose Heikin-Ashi close, the raw bar's average price. Must
    *        hold at least {@code endIdx - startIdx + 1} values.
    * @return The range written: {@code begIdx} is the first bar with a value,
    *        {@code count} how many were written.
    * @throws IndexOutOfBoundsException if {@code startIdx} or {@code endIdx} is
    *        negative or above {@link Core#MAX_INDEX}, or {@code endIdx < startIdx}.
    * @throws IllegalArgumentException if an optional parameter is outside its
    *        documented range, two outputs share one array, or an array is absent or
    *        too short for the range requested — any input this function
    *        <i>declares</i> that does not reach {@code endIdx}, or an output that
    *        cannot hold the values produced. Declared, not read: a few candlestick
    *        patterns take an OHLC series they never index, and it is required all the
    *        same. An output this function documents as declinable is the one
    *        exception: {@code null} is how you decline it. Checked before anything is
    *        written, so a rejected call leaves every buffer untouched.
    *
    * @see Core#AVGPRICE
    * @see Core#TYPPRICE
    * @see Core#WCLPRICE
    */
   public OutRange HA( int startIdx,
                       int endIdx,
                       double inOpen[],
                       double inHigh[],
                       double inLow[],
                       double inClose[],
                       double outHAOpen[],
                       double outHAHigh[],
                       double outHALow[],
                       double outHAClose[] )
   {
      requireIndexRange("HA", startIdx, endIdx);
      int guardStart = clampedStart("HA", startIdx, HA_Lookback());
      int guardInLen = endIdx + 1;
      int guardOutLen = guardStart > endIdx ? 0 : endIdx - guardStart + 1;
      requireLength("HA", "inOpen", inOpen, guardInLen);
      requireLength("HA", "inHigh", inHigh, guardInLen);
      requireLength("HA", "inLow", inLow, guardInLen);
      requireLength("HA", "inClose", inClose, guardInLen);
      requireLength("HA", "outHAOpen", outHAOpen, guardOutLen);
      requireLength("HA", "outHAHigh", outHAHigh, guardOutLen);
      requireLength("HA", "outHALow", outHALow, guardOutLen);
      requireLength("HA", "outHAClose", outHAClose, guardOutLen);
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      RetCode retCode = HA_Impl(startIdx, endIdx, inOpen, inHigh, inLow, inClose, outBegIdx, outNBElement, outHAOpen, outHAHigh, outHALow, outHAClose);
      if( retCode != RetCode.Success ) {
         throw failure("HA", retCode);
      }
      return new OutRange(outBegIdx.value, outNBElement.value);
   }
   /**
    * Heikin-Ashi Candles: an OHLC-to-OHLC smoothing transform. Each output bar
    * is a synthetic candle whose close is the raw bar's average price, whose
    * open is the midpoint of the previous synthetic candle, and whose extremes
    * clamp the raw extremes against those two. Consecutive candles share a body
    * edge by construction, which is what suppresses the single-bar noise a raw
    * candle chart shows. The transform is a *filter*, not an oscillator: it
    * returns prices in the input's own units and is plotted in place of the raw
    * candles.
    * <p><b>Formula</b>
    * <pre>{@code
    * ```
    * HA_close[i] = (open[i] + high[i] + low[i] + close[i]) / 4
    * HA_open[0]  = (open[0] + close[0]) / 2
    * HA_open[i]  = (HA_open[i-1] + HA_close[i-1]) / 2
    * HA_high[i]  = max(high[i], HA_open[i], HA_close[i])
    * HA_low[i]   = min(low[i],  HA_open[i], HA_close[i])
    * ```
    * Every operation is an addition or a division by an exact power of two, so the transform is exact whenever its inputs are.
    * `HA_close` is **not** [`AVGPRICE`](/functions/avgprice). The four terms are the same, but the summation order is not, and floating-point addition is not associative: `AVGPRICE` sums `(H+L+C+O)`, this sums `(O+H+L+C)`, and the two differ in the last place on ordinary bars.
    * }</pre>
    * <p>This is the {@code float[]} overload. The arithmetic is performed in
    * {@code double} before being written to the {@code double[]} output, so a
    * result beyond {@code float} range is still representable.
    * <p>Values are written only where the indicator is defined. The returned
    * {@link OutRange} says where they start and how many there are; nothing
    * outside that range is touched, and the library never pads with NaN. A
    * valid range shorter than {@link Core#HA_Lookback} is a <b>success with no
    * values</b> ({@code count() == 0}), not an error.
    *
    * @param startIdx First bar of the requested range (inclusive).
    * @param endIdx Last bar of the requested range (inclusive).
    * @param inOpen Open price of each bar.
    * @param inHigh High price of each bar.
    * @param inLow Low price of each bar.
    * @param inClose Close price of each bar.
    * @param outHAOpen Heikin-Ashi open, the previous synthetic candle's
    *        midpoint. Must hold at least {@code endIdx - startIdx + 1} values.
    * @param outHAHigh Heikin-Ashi high, the raw high clamped up by the
    *        synthetic body. Must hold at least {@code endIdx - startIdx + 1} values.
    * @param outHALow Heikin-Ashi low, the raw low clamped down by the synthetic
    *        body. Must hold at least {@code endIdx - startIdx + 1} values.
    * @param outHAClose Heikin-Ashi close, the raw bar's average price. Must
    *        hold at least {@code endIdx - startIdx + 1} values.
    * @return The range written: {@code begIdx} is the first bar with a value,
    *        {@code count} how many were written.
    * @throws IndexOutOfBoundsException if {@code startIdx} or {@code endIdx} is
    *        negative or above {@link Core#MAX_INDEX}, or {@code endIdx < startIdx}.
    * @throws IllegalArgumentException if an optional parameter is outside its
    *        documented range, two outputs share one array, or an array is absent or
    *        too short for the range requested — any input this function
    *        <i>declares</i> that does not reach {@code endIdx}, or an output that
    *        cannot hold the values produced. Declared, not read: a few candlestick
    *        patterns take an OHLC series they never index, and it is required all the
    *        same. An output this function documents as declinable is the one
    *        exception: {@code null} is how you decline it. Checked before anything is
    *        written, so a rejected call leaves every buffer untouched.
    *
    * @see Core#AVGPRICE
    * @see Core#TYPPRICE
    * @see Core#WCLPRICE
    */
   public OutRange HA( int startIdx,
                       int endIdx,
                       float inOpen[],
                       float inHigh[],
                       float inLow[],
                       float inClose[],
                       double outHAOpen[],
                       double outHAHigh[],
                       double outHALow[],
                       double outHAClose[] )
   {
      requireIndexRange("HA", startIdx, endIdx);
      int guardStart = clampedStart("HA", startIdx, HA_Lookback());
      int guardInLen = endIdx + 1;
      int guardOutLen = guardStart > endIdx ? 0 : endIdx - guardStart + 1;
      requireLength("HA", "inOpen", inOpen, guardInLen);
      requireLength("HA", "inHigh", inHigh, guardInLen);
      requireLength("HA", "inLow", inLow, guardInLen);
      requireLength("HA", "inClose", inClose, guardInLen);
      requireLength("HA", "outHAOpen", outHAOpen, guardOutLen);
      requireLength("HA", "outHAHigh", outHAHigh, guardOutLen);
      requireLength("HA", "outHALow", outHALow, guardOutLen);
      requireLength("HA", "outHAClose", outHAClose, guardOutLen);
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      RetCode retCode = HA_Impl(startIdx, endIdx, inOpen, inHigh, inLow, inClose, outBegIdx, outNBElement, outHAOpen, outHAHigh, outHALow, outHAClose);
      if( retCode != RetCode.Success ) {
         throw failure("HA", retCode);
      }
      return new OutRange(outBegIdx.value, outNBElement.value);
   }
/**** Streaming API *****/

   /**
    * A live HA stream (unrelated to {@code java.util.stream}): one value per
    * closed bar, bit-identical to {@link Core#HA} over the same series.
    * Open with {@link Core#haOpen}; there is no close — the handle is
    * ordinary heap state, unreferenced handles are simply garbage-collected.
    * <p>Concurrency: a handle is single-writer — {@code update}, {@code peek},
    * {@code value} and {@code clone} must not race with an {@code update} on
    * the same handle. With no concurrent {@code update}, {@code peek}/
    * {@code value}/{@code clone} never write the stream and may be called
    * concurrently after safe publication. Independent streams (a
    * {@code clone()} result included) are fully independent.
    * <p>Not serializable by design: to checkpoint, retain the history and
    * re-open — the result is bit-identical by contract.
    */
   public static final class HaStream {
      Core core;
      double prevHAOpen;
      double prevHAClose;
      double cur_outHAOpen;
      double cur_outHAHigh;
      double cur_outHALow;
      double cur_outHAClose;
      int outRangeBegIdx;
      int outRangeCount;

      HaStream( Core core ) { this.core = core; }

      /**
       * The bars this stream has an output for, in the input series'
       * coordinates: {@code [begIdx, begIdx + count)}.
       * <p>It is what {@link Core#HA} reports over the same bars: the
       * opener sets it to {@code (lookback, historyLen - lookback)}, every
       * {@code update} adds one to the count — a bar rejected for being
       * non-finite included, because it still happened — {@code peek} leaves
       * it alone, and {@code clone()} carries it verbatim. A plain
       * {@code open} hands back only the last value, a subset of this range,
       * because the caller chose not to take the fill.
       */
      public OutRange outRange() { return new OutRange(outRangeBegIdx, outRangeCount); }

      HaStream( HaStream other ) {
         this.core = other.core;
         this.prevHAOpen = other.prevHAOpen;
         this.prevHAClose = other.prevHAClose;
         this.cur_outHAOpen = other.cur_outHAOpen;
         this.cur_outHAHigh = other.cur_outHAHigh;
         this.cur_outHALow = other.cur_outHALow;
         this.cur_outHAClose = other.cur_outHAClose;
         this.outRangeBegIdx = other.outRangeBegIdx;
         this.outRangeCount = other.outRangeCount;
      }

      /**
       * Commit one closed bar, writing the new current values into the {@code out} the CALLER owns.
       * Never allocates handle state.
       * <p>Throws {@link IllegalArgumentException} if any bar value is not
       * finite (NaN or an infinity). That check runs before anything is
       * written, so the state is left exactly as it was: the rejected bar's
       * output is the previous value, held, and {@link #value(HaOut)} answers it.
       * The stream stays usable, so skip the bar or re-open on a clean
       * history. {@link #outRange()} does advance: the bar happened and
       * occupies a position in the series, so the handle counts it, which is
       * what keeps two handles on one feed aligned when only one rejects.
       * This is the one place the streaming tier is stricter than
       * the batch API, which computes on whatever it is given: a handle
       * retains its state, so a single non-finite bar would poison every
       * later value it produces.
       */
      public void update( double inOpen, double inHigh, double inLow, double inClose, HaOut out ) {
         requireArgument("HA update", "out", out);
         if( !Double.isFinite(inOpen) || !Double.isFinite(inHigh) || !Double.isFinite(inLow) || !Double.isFinite(inClose) ) {
            if( this.outRangeCount < MAX_INDEX ) this.outRangeCount++;
            throw new TaLibArgumentException("HA update: BadParam", RetCode.BadParam);
         }
         core.haStepImpl(this, inOpen, inHigh, inLow, inClose);
         if( this.outRangeCount < MAX_INDEX ) this.outRangeCount++;
         out.haOpen = this.cur_outHAOpen;
         out.haHigh = this.cur_outHAHigh;
         out.haLow = this.cur_outHALow;
         out.haClose = this.cur_outHAClose;
      }

      /**
       * Commit {@code n} closed bars and write their {@code n} values, in one
       * call — exactly {@code n} back-to-back {@code update} calls, with one
       * set of argument checks instead of {@code n}. {@code n} is
       * {@code inOpen.length}; the outputs must hold at least that many, and must
       * not be the same array as an input or as each other.
       * <p>{@link #outRange()} counts what this call took in, which is what makes a
       * rejection readable: a non-finite bar {@code k} throws
       * {@link IllegalArgumentException} exactly as {@code update} would, with
       * the bars before {@code k} committed and written, bar {@code k} and
       * everything after it not, and the count advanced by {@code k + 1} —
       * the committed bars plus the rejected one.
       */
      public void updateAndFill( double inOpen[], double inHigh[], double inLow[], double inClose[], double outHAOpen[], double outHAHigh[], double outHALow[], double outHAClose[] ) {
         requireArgument("HA updateAndFill", "inOpen", inOpen);
         requireArgument("HA updateAndFill", "inHigh", inHigh);
         requireArgument("HA updateAndFill", "inLow", inLow);
         requireArgument("HA updateAndFill", "inClose", inClose);
         requireArgument("HA updateAndFill", "outHAOpen", outHAOpen);
         requireArgument("HA updateAndFill", "outHAHigh", outHAHigh);
         requireArgument("HA updateAndFill", "outHALow", outHALow);
         requireArgument("HA updateAndFill", "outHAClose", outHAClose);
         final int barCount = inOpen.length;
         if( inHigh.length != barCount || inLow.length != barCount || inClose.length != barCount || outHAOpen.length < barCount || outHAHigh.length < barCount || outHALow.length < barCount || outHAClose.length < barCount || (Object)outHAOpen == (Object)inOpen || (Object)outHAOpen == (Object)inHigh || (Object)outHAOpen == (Object)inLow || (Object)outHAOpen == (Object)inClose || (Object)outHAHigh == (Object)inOpen || (Object)outHAHigh == (Object)inHigh || (Object)outHAHigh == (Object)inLow || (Object)outHAHigh == (Object)inClose || (Object)outHALow == (Object)inOpen || (Object)outHALow == (Object)inHigh || (Object)outHALow == (Object)inLow || (Object)outHALow == (Object)inClose || (Object)outHAClose == (Object)inOpen || (Object)outHAClose == (Object)inHigh || (Object)outHAClose == (Object)inLow || (Object)outHAClose == (Object)inClose || (Object)outHAOpen == (Object)outHAHigh || (Object)outHAOpen == (Object)outHALow || (Object)outHAOpen == (Object)outHAClose || (Object)outHAHigh == (Object)outHALow || (Object)outHAHigh == (Object)outHAClose || (Object)outHALow == (Object)outHAClose )
            throw new TaLibArgumentException("HA updateAndFill: BadParam", RetCode.BadParam);
         for( int i = 0; i < barCount; i++ ) {
            if( !Double.isFinite(inOpen[i]) || !Double.isFinite(inHigh[i]) || !Double.isFinite(inLow[i]) || !Double.isFinite(inClose[i]) ) {
               if( this.outRangeCount < MAX_INDEX ) this.outRangeCount++;
               throw new TaLibArgumentException("HA updateAndFill: BadParam", RetCode.BadParam);
            }
            core.haStepImpl(this, inOpen[i], inHigh[i], inLow[i], inClose[i]);
            outHAOpen[i] = this.cur_outHAOpen;
            outHAHigh[i] = this.cur_outHAHigh;
            outHALow[i] = this.cur_outHALow;
            outHAClose[i] = this.cur_outHAClose;
            if( this.outRangeCount < MAX_INDEX ) this.outRangeCount++;
         }
      }

      /**
       * Evaluate a forming bar without committing — bit-identical to what the
       * next {@code update} with the same bar would write — the same
       * transition, with every store it would make carried in a local instead.
       * Never writes this handle, so peeks may
       * run concurrently with each other. It copies nothing: the frame runs against this handle, reading its
       * buffers and storing what the step would commit into locals, so the cost
       * does not grow with the period and {@code peek} never allocates.
       */
      public void peek( double inOpen, double inHigh, double inLow, double inClose, HaOut out ) {
         requireArgument("HA peek", "out", out);
         if( !Double.isFinite(inOpen) || !Double.isFinite(inHigh) || !Double.isFinite(inLow) || !Double.isFinite(inClose) )
            throw new TaLibArgumentException("HA peek: BadParam", RetCode.BadParam);
         HaStream sp = this;
         double haOpen = 0.0;
         double haClose = 0.0;
         double haHigh = 0.0;
         double haLow = 0.0;
         double tempOpen = 0.0;
         double tempHigh = 0.0;
         double tempLow = 0.0;
         double tempClose = 0.0;
         double cur_outHAClose = 0.0;
         double cur_outHAHigh = 0.0;
         double cur_outHALow = 0.0;
         double cur_outHAOpen = 0.0;
         tempOpen = inOpen;
         tempHigh = inHigh;
         tempLow = inLow;
         tempClose = inClose;
         haOpen = (sp.prevHAOpen + sp.prevHAClose) / 2.0;
         haClose = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;
         /* An elementwise clamp of the raw bar against the two body edges, so
          * the extremes carry no state of their own.
          */
         haHigh = tempHigh;
         if( haOpen > haHigh ) {
            haHigh = haOpen;
         }
         if( haClose > haHigh ) {
            haHigh = haClose;
         }
         haLow = tempLow;
         if( haOpen < haLow ) {
            haLow = haOpen;
         }
         if( haClose < haLow ) {
            haLow = haClose;
         }
         /* Written only after this bar's four inputs are in locals above: an
          * output buffer is allowed to alias any input, and output slot k lands
          * on input bar k, which is at or behind `today`.
          */
         cur_outHAOpen = haOpen;
         cur_outHAHigh = haHigh;
         cur_outHALow = haLow;
         cur_outHAClose = haClose;
         out.haOpen = cur_outHAOpen;
         out.haHigh = cur_outHAHigh;
         out.haLow = cur_outHALow;
         out.haClose = cur_outHAClose;
      }

      /**
       * The value at the last bar this stream counted — the bar
       * {@link #outRange()} ends on. The last history bar right after open,
       * then whatever the latest accepted {@code update} wrote.
       * A pure field read; {@code peek} does not change it. Overwrites {@code out}, allocating nothing.
       */
      public void value( HaOut out ) {
         requireArgument("HA value", "out", out);
         out.haOpen = this.cur_outHAOpen;
         out.haHigh = this.cur_outHAHigh;
         out.haLow = this.cur_outHALow;
         out.haClose = this.cur_outHAClose;
      }

      /**
       * An independent fork of this stream: both evolve separately from here
       * on. Buffers are copied and sub-streams cloned recursively; the
       * {@link Core} reference is shared, since a {@code Core} is immutable
       * for a stream's lifetime.
       *
       * <p>Not the {@code Cloneable} protocol: this calls a copy constructor,
       * never {@code super.clone()}, so it throws nothing.
       *
       * @return an independent stream at the same bar
       */
      @Override
      public HaStream clone() {
         return new HaStream(this);
      }
   }

   /**
    * The outputs of one HA bar, written by the stream into an object the
    * CALLER owns. Allocate one and reuse it: {@code update}, {@code peek}
    * and {@code value} overwrite its fields, so the sink itself costs
    * nothing per bar.
    *
    * <p><b>Its contents are only valid until the next call that writes it.</b>
    * It is a mutable buffer, not a reading: a reference kept past that call,
    * or one put in a collection, sees the value change underneath it. Copy the
    * fields out if the reading has to outlive the call.
    *
    * <p>Deliberately no {@code equals} or {@code hashCode}: a mutable type
    * with value equality breaks the {@code HashMap}/{@code HashSet}
    * invariant the moment a reused instance becomes a key. Compare the fields.
    */
   public static final class HaOut {
      /** Heikin-Ashi open, the previous synthetic candle's midpoint. */
      public double haOpen;
      /** Heikin-Ashi high, the raw high clamped up by the synthetic body. */
      public double haHigh;
      /** Heikin-Ashi low, the raw low clamped down by the synthetic body. */
      public double haLow;
      /** Heikin-Ashi close, the raw bar's average price. */
      public double haClose;
   }
   void haStepImpl( HaStream sp, double inOpen, double inHigh, double inLow, double inClose )
   {
      double haOpen = 0.0;
      double haClose = 0.0;
      double haHigh = 0.0;
      double haLow = 0.0;
      double tempOpen = 0.0;
      double tempHigh = 0.0;
      double tempLow = 0.0;
      double tempClose = 0.0;
      tempOpen = inOpen;
      tempHigh = inHigh;
      tempLow = inLow;
      tempClose = inClose;
      haOpen = (sp.prevHAOpen + sp.prevHAClose) / 2.0;
      haClose = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;
      /* An elementwise clamp of the raw bar against the two body edges, so
       * the extremes carry no state of their own.
       */
      haHigh = tempHigh;
      if( haOpen > haHigh ) {
         haHigh = haOpen;
      }
      if( haClose > haHigh ) {
         haHigh = haClose;
      }
      haLow = tempLow;
      if( haOpen < haLow ) {
         haLow = haOpen;
      }
      if( haClose < haLow ) {
         haLow = haClose;
      }
      /* Written only after this bar's four inputs are in locals above: an
       * output buffer is allowed to alias any input, and output slot k lands
       * on input bar k, which is at or behind `today`.
       */
      sp.cur_outHAOpen = haOpen;
      sp.cur_outHAHigh = haHigh;
      sp.cur_outHALow = haLow;
      sp.cur_outHAClose = haClose;
      sp.prevHAOpen = haOpen;
      sp.prevHAClose = haClose;
   }
   private RetCode haOpenImpl( HaStream sp, double inOpen[], double inHigh[], double inLow[], double inClose[], int startIdx, MInteger outBegIdx, MInteger outNBElement, double outHAOpen[], double outHAHigh[], double outHALow[], double outHAClose[], int outStride )
   {
      int today = 0;
      int outIdx = 0;
      int lookbackTotal = 0;
      double haOpen = 0;
      double haClose = 0;
      double haHigh = 0;
      double haLow = 0;
      double prevHAOpen = 0;
      double prevHAClose = 0;
      double tempOpen = 0;
      double tempHigh = 0;
      double tempLow = 0;
      double tempClose = 0;
      int historyLen = inOpen.length;
      int endIdx = historyLen - 1;
      if( historyLen < 1 ) {
         return RetCode.OutOfRangeStartIndex;
      }
      if( historyLen > MAX_INDEX + 1 ) {
         return RetCode.OutOfRangeEndIndex;
      }
      if( inHigh.length != inOpen.length || inLow.length != inOpen.length || inClose.length != inOpen.length ) {
         return RetCode.BadParam;
      }
      if( startIdx > endIdx ) {
         outBegIdx.value = 0;
         outNBElement.value = 0;
         return RetCode.InsufficientHistory;
      }
      /* Heikin-Ashi ("average bar"): an OHLC-to-OHLC smoothing transform.
       *
       *   HA_close[i] = (open[i] + high[i] + low[i] + close[i]) / 4
       *   HA_open [0] = (open[0] + close[0]) / 2
       *   HA_open [i] = (HA_open[i-1] + HA_close[i-1]) / 2
       *   HA_high [i] = max(high[i], HA_open[i], HA_close[i])
       *   HA_low  [i] = min(low [i], HA_open[i], HA_close[i])
       *
       * Keep the four-term sum in THIS left-to-right order. It is the oracles'
       * association, and floating-point addition is not associative:
       * TA_AVGPRICE sums the same four terms as (H+L+C+O)/4 and differs from
       * HA_close in the last place on ordinary bars, so HA_close is not a
       * composed AVGPRICE call and the two must not be unified.
       *
       * Every operation is one addition or one division by an exact power of
       * two, so the transform is exact whenever its inputs are -- which is why
       * the external captures are frozen at tolerance 0 rather than a band.
       *
       * BOTH stability flags are declared, and they answer different questions.
       * `unstable_period` is the ABI knob: the open's recursion carries a factor
       * of 1/2 per bar, so a seed's influence halves every step and dies out
       * entirely within a few dozen bars -- a caller who spends them gets a
       * start-independent series (56 bars suffice on the regtest history, and
       * test_ha.c pins that). `path_dependent` declares
       * what is true at the DEFAULT of 0: the recursion re-seeds at the anchor,
       * so HA(3, 7, ...) starts its open at (open[3]+close[3])/2 rather than
       * warming up from bar 0. Without it the range-stability leg compares a
       * sub-range call against a full-range one and fails; with only it, the
       * knob that makes the two converge would not exist.
       */
      lookbackTotal = HA_Lookback();
      if( startIdx < lookbackTotal ) {
         startIdx = lookbackTotal;
      }
      /* Make sure there is still something to evaluate. */
      if( startIdx > endIdx ) {
         outBegIdx.value = 0;
         outNBElement.value = 0;
         return RetCode.InsufficientHistory ;
      }
      /* The seed is carried as a VIRTUAL previous candle rather than as a
       * special first iteration: seeding the pair with the anchor bar's own
       * open and close makes the uniform recursion produce
       * (open+close)/2 at that bar, which is the published seed. One loop body
       * then serves the anchor bar and every bar after it -- and that same pair
       * is the streaming tier's initial state, so batch and stream share one
       * definition of "where the recursion starts".
       */
      today = startIdx - lookbackTotal;
      prevHAOpen = inOpen[today];
      prevHAClose = inClose[today];
      /* Warm-up: advance the recursion across the unstable-period bars without
       * emitting. Empty at the default knob of 0.
       */
      while( today < startIdx ) {
         tempOpen = inOpen[today];
         tempHigh = inHigh[today];
         tempLow = inLow[today];
         tempClose = inClose[today];
         haOpen = (prevHAOpen + prevHAClose) / 2.0;
         haClose = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;
         prevHAOpen = haOpen;
         prevHAClose = haClose;
         today += 1;
      }
      outIdx = 0;
      while( today <= endIdx ) {
         tempOpen = inOpen[today];
         tempHigh = inHigh[today];
         tempLow = inLow[today];
         tempClose = inClose[today];
         haOpen = (prevHAOpen + prevHAClose) / 2.0;
         haClose = (tempOpen + tempHigh + tempLow + tempClose) / 4.0;
         /* An elementwise clamp of the raw bar against the two body edges, so
          * the extremes carry no state of their own.
          */
         haHigh = tempHigh;
         if( haOpen > haHigh ) {
            haHigh = haOpen;
         }
         if( haClose > haHigh ) {
            haHigh = haClose;
         }
         haLow = tempLow;
         if( haOpen < haLow ) {
            haLow = haOpen;
         }
         if( haClose < haLow ) {
            haLow = haClose;
         }
         /* Written only after this bar's four inputs are in locals above: an
          * output buffer is allowed to alias any input, and output slot k lands
          * on input bar k, which is at or behind `today`.
          */
         outHAOpen[outIdx * outStride] = haOpen;
         outHAHigh[outIdx * outStride] = haHigh;
         outHALow[outIdx * outStride] = haLow;
         outHAClose[outIdx * outStride] = haClose;
         outIdx += 1;
         prevHAOpen = haOpen;
         prevHAClose = haClose;
         today += 1;
      }
      outBegIdx.value = startIdx;
      outNBElement.value = outIdx;
      /* Capture the live batch state into the handle. */
      sp.prevHAOpen = prevHAOpen;
      sp.prevHAClose = prevHAClose;
      sp.cur_outHAOpen = outHAOpen[(outNBElement.value - 1) * outStride];
      sp.cur_outHAHigh = outHAHigh[(outNBElement.value - 1) * outStride];
      sp.cur_outHALow = outHALow[(outNBElement.value - 1) * outStride];
      sp.cur_outHAClose = outHAClose[(outNBElement.value - 1) * outStride];
      return RetCode.Success;
   }
   /* haOpenAndFill anchored at startIdx — the composed-open fusion seam. */
   HaStream haOpenAndFillInternal( double inOpen[], double inHigh[], double inLow[], double inClose[], int startIdx, MInteger outBegIdx, MInteger outNBElement, double outHAOpen[], double outHAHigh[], double outHALow[], double outHAClose[] )
   {
      HaStream sp = new HaStream(this);
      RetCode retCode = haOpenImpl(sp, inOpen, inHigh, inLow, inClose, startIdx, outBegIdx, outNBElement, outHAOpen, outHAHigh, outHALow, outHAClose, 1);
      sp.outRangeBegIdx = outBegIdx.value;
      sp.outRangeCount = outNBElement.value;
      if( retCode == RetCode.Success ) {
         return sp;
      }
      if( retCode == RetCode.InsufficientHistory ) {
         throw new InsufficientHistoryException("HA openAndFill: history shorter than lookback + 1");
      }
      if( retCode == RetCode.InternalError ) {
         throw new TaLibStateException("HA openAndFill: internal error", retCode);
      }
      throw new TaLibArgumentException("HA openAndFill: " + retCode, retCode);
   }
   /* Internal startIdx-anchored open behind haOpen (composition seam). */
   HaStream haOpenInternal( double inOpen[], double inHigh[], double inLow[], double inClose[], int startIdx )
   {
      HaStream sp = new HaStream(this);
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      double[] sink_outHAOpen = new double[1];
      double[] sink_outHAHigh = new double[1];
      double[] sink_outHALow = new double[1];
      double[] sink_outHAClose = new double[1];
      RetCode retCode = haOpenImpl(sp, inOpen, inHigh, inLow, inClose, startIdx, outBegIdx, outNBElement, sink_outHAOpen, sink_outHAHigh, sink_outHALow, sink_outHAClose, 0);
      sp.outRangeBegIdx = outBegIdx.value;
      sp.outRangeCount = outNBElement.value;
      if( retCode == RetCode.Success ) {
         return sp;
      }
      if( retCode == RetCode.InsufficientHistory ) {
         throw new InsufficientHistoryException("HA open: history shorter than lookback + 1");
      }
      if( retCode == RetCode.InternalError ) {
         throw new TaLibStateException("HA open: internal error", retCode);
      }
      throw new TaLibArgumentException("HA open: " + retCode, retCode);
   }
   /**
    * Open a live HA stream over the warm-up history; the handle's
    * {@code value()} starts at the last history bar's value — bit-identical
    * to {@link Core#HA} at that bar.
    * <p>The history must hold at least {@code HA_Lookback(...) + 1} bars
    * (unstable-period aware), or {@link InsufficientHistoryException} is
    * thrown. Out-of-range parameters throw {@link IllegalArgumentException}
    * ({@code Integer.MIN_VALUE} selects an integer parameter's documented
    * default, as in the batch API). An EMPTY history throws
    * {@link IndexOutOfBoundsException} — its implied {@code startIdx} of 0
    * names no bar — and a null argument {@link IllegalArgumentException},
    * both ahead of everything above.
    */
   public HaStream haOpen( double inOpen[], double inHigh[], double inLow[], double inClose[] )
   {
      requireArgument("HA open", "inOpen", inOpen);
      requireHistory("HA open", inOpen.length);
      requireArgument("HA open", "inHigh", inHigh);
      requireArgument("HA open", "inLow", inLow);
      requireArgument("HA open", "inClose", inClose);
      requireHistoryLength("HA open", "inHigh", inHigh.length, inOpen.length);
      requireHistoryLength("HA open", "inLow", inLow.length, inOpen.length);
      requireHistoryLength("HA open", "inClose", inClose.length, inOpen.length);
      return haOpenInternal(inOpen, inHigh, inLow, inClose, 0);
   }
   /**
    * {@link Core#haOpen} that also fills the output array(s) bit-identically
    * to {@link Core#HA} over the whole history in the same single pass
    * (no separate batch call needed for the warm-up plot). Output arrays must
    * not alias the inputs or each other, and must hold
    * {@code historyLen - lookback} values — both checked before anything is
    * written, so an undersized array is an {@link IllegalArgumentException}
    * naming it rather than a fault from inside the fill.
    * <p>The range written is on the returned handle:
    * {@link HaStream#outRange()}.
    */
   public HaStream haOpenAndFill( double inOpen[], double inHigh[], double inLow[], double inClose[], double outHAOpen[], double outHAHigh[], double outHALow[], double outHAClose[] )
   {
      requireArgument("HA openAndFill", "inOpen", inOpen);
      requireHistory("HA openAndFill", inOpen.length);
      requireArgument("HA openAndFill", "inHigh", inHigh);
      requireArgument("HA openAndFill", "inLow", inLow);
      requireArgument("HA openAndFill", "inClose", inClose);
      int guardOutLen = openFillCount("HA openAndFill", inOpen.length, HA_Lookback());
      requireHistoryLength("HA openAndFill", "inHigh", inHigh.length, inOpen.length);
      requireHistoryLength("HA openAndFill", "inLow", inLow.length, inOpen.length);
      requireHistoryLength("HA openAndFill", "inClose", inClose.length, inOpen.length);
      requireLength("HA openAndFill", "outHAOpen", outHAOpen, guardOutLen);
      requireLength("HA openAndFill", "outHAHigh", outHAHigh, guardOutLen);
      requireLength("HA openAndFill", "outHALow", outHALow, guardOutLen);
      requireLength("HA openAndFill", "outHAClose", outHAClose, guardOutLen);
      if( (Object)outHAOpen == (Object)inOpen || (Object)outHAOpen == (Object)inHigh || (Object)outHAOpen == (Object)inLow || (Object)outHAOpen == (Object)inClose || (Object)outHAHigh == (Object)inOpen || (Object)outHAHigh == (Object)inHigh || (Object)outHAHigh == (Object)inLow || (Object)outHAHigh == (Object)inClose || (Object)outHALow == (Object)inOpen || (Object)outHALow == (Object)inHigh || (Object)outHALow == (Object)inLow || (Object)outHALow == (Object)inClose || (Object)outHAClose == (Object)inOpen || (Object)outHAClose == (Object)inHigh || (Object)outHAClose == (Object)inLow || (Object)outHAClose == (Object)inClose || (Object)outHAOpen == (Object)outHAHigh || (Object)outHAOpen == (Object)outHALow || (Object)outHAOpen == (Object)outHAClose || (Object)outHAHigh == (Object)outHALow || (Object)outHAHigh == (Object)outHAClose || (Object)outHALow == (Object)outHAClose ) {
         throw new TaLibArgumentException("HA openAndFill: " + RetCode.BadParam, RetCode.BadParam);
      }
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      return haOpenAndFillInternal(inOpen, inHigh, inLow, inClose, 0, outBegIdx, outNBElement, outHAOpen, outHAHigh, outHALow, outHAClose);
   }
