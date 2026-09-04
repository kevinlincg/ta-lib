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
 */

   /**
    * Number of leading input bars {@link Core#RMA} consumes before it can
    * produce its first value.
    * <p>Equivalently, the index of the first bar with a value when the whole
    * series is requested. Feed at least {@code lookback + 1} bars to get any
    * output.
    * <p>This function is recursive, so the result also includes this
    * {@code Core}'s unstable-period setting — which is why it is an instance
    * method.
    *
    * @param optInTimePeriod Number of bars in the average; sets smoothing alpha
    *        = 1/period (default 30; range 1..100000; {@code Integer.MIN_VALUE} selects
    *        the default).
    * @return The lookback, or {@code -1} if a parameter is out of range.
    */
   public int RMA_Lookback( int optInTimePeriod )
   {
      if( optInTimePeriod == Integer.MIN_VALUE ) {
         optInTimePeriod = 30;
      } else if( optInTimePeriod < 1 || optInTimePeriod > 100000 ) {
         return -1;
      }
      return optInTimePeriod - 1 + this.unstablePeriod[FuncUnstId.RMA.ordinal()] ;

   }
   RetCode RMA_Impl( int startIdx,
                     int endIdx,
                     double inReal[],
                     int optInTimePeriod,
                     MInteger outBegIdx,
                     MInteger outNBElement,
                     double outReal[] )
   {
      double tempReal = 0;
      double prevMA = 0;
      double wAlpha = 0;
      double wBeta = 0;
      int i = 0;
      int today = 0;
      int outIdx = 0;
      int lookbackTotal = 0;
      if( (startIdx < 0) || (startIdx > MAX_INDEX) ) {
         return RetCode.OutOfRangeStartIndex ;
      }
      if( (endIdx < 0) || (endIdx > MAX_INDEX) || (endIdx < startIdx)) {
         return RetCode.OutOfRangeEndIndex ;
      }
      if( optInTimePeriod == Integer.MIN_VALUE ) {
         optInTimePeriod = 30;
      } else if( optInTimePeriod < 1 || optInTimePeriod > 100000 ) {
         return RetCode.BadParam;
      }
      outBegIdx.value = 0;
      outNBElement.value = 0;
      /* Identify the minimum number of price bar needed
       * to calculate at least one output.
       */
      lookbackTotal = RMA_Lookback(optInTimePeriod);
      /* Move up the start index if there is not
       * enough initial data.
       */
      if( startIdx < lookbackTotal ) {
         startIdx = lookbackTotal;
      }
      /* Make sure there is still something to evaluate. */
      if( startIdx > endIdx ) {
         return RetCode.Success ;
      }
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
      wBeta = (double)(optInTimePeriod - 1) / (double)optInTimePeriod;
      wAlpha = 1.0 - wBeta;
      outBegIdx.value = startIdx;
      /* Seed with a simple average of the first 'period' values. */
      today = startIdx - lookbackTotal;
      i = optInTimePeriod;
      tempReal = 0.0;
      while( i-- > 0 ) {
         tempReal += inReal[today++];
      }
      prevMA = tempReal / optInTimePeriod;
      /* Skip the unstable period. */
      while( today <= startIdx ) {
         prevMA = Math.fma(wBeta, prevMA, wAlpha * inReal[today]);
         today += 1;
      }
      outReal[0] = prevMA;
      outIdx = 1;
      while( today <= endIdx ) {
         prevMA = Math.fma(wBeta, prevMA, wAlpha * inReal[today]);
         today += 1;
         outReal[outIdx++] = prevMA;
      }
      outNBElement.value = outIdx;
      return RetCode.Success ;
   }
   RetCode RMA_Impl( int startIdx,
                     int endIdx,
                     float inReal[],
                     int optInTimePeriod,
                     MInteger outBegIdx,
                     MInteger outNBElement,
                     double outReal[] )
   {
      double tempReal = 0;
      double prevMA = 0;
      double wAlpha = 0;
      double wBeta = 0;
      int i = 0;
      int today = 0;
      int outIdx = 0;
      int lookbackTotal = 0;
      if( (startIdx < 0) || (startIdx > MAX_INDEX) ) {
         return RetCode.OutOfRangeStartIndex ;
      }
      if( (endIdx < 0) || (endIdx > MAX_INDEX) || (endIdx < startIdx)) {
         return RetCode.OutOfRangeEndIndex ;
      }
      if( optInTimePeriod == Integer.MIN_VALUE ) {
         optInTimePeriod = 30;
      } else if( optInTimePeriod < 1 || optInTimePeriod > 100000 ) {
         return RetCode.BadParam;
      }
      outBegIdx.value = 0;
      outNBElement.value = 0;
      lookbackTotal = RMA_Lookback(optInTimePeriod);
      if( startIdx < lookbackTotal ) {
         startIdx = lookbackTotal;
      }
      if( startIdx > endIdx ) {
         return RetCode.Success ;
      }
      wBeta = (double)(optInTimePeriod - 1) / (double)optInTimePeriod;
      wAlpha = 1.0 - wBeta;
      outBegIdx.value = startIdx;
      today = startIdx - lookbackTotal;
      i = optInTimePeriod;
      tempReal = 0.0;
      while( i-- > 0 ) {
         tempReal += (double)inReal[today++];
      }
      prevMA = tempReal / optInTimePeriod;
      while( today <= startIdx ) {
         prevMA = Math.fma(wBeta, prevMA, wAlpha * (double)inReal[today]);
         today += 1;
      }
      outReal[0] = prevMA;
      outIdx = 1;
      while( today <= endIdx ) {
         prevMA = Math.fma(wBeta, prevMA, wAlpha * (double)inReal[today]);
         today += 1;
         outReal[outIdx++] = prevMA;
      }
      outNBElement.value = outIdx;
      return RetCode.Success ;
   }
   /**
    * Wilder's smoothed moving average: an exponential average whose smoothing
    * factor is {@code 1/period} rather than the usual {@code 2/(period+1)},
    * seeded with a simple average of the first {@code period} bars. This is the
    * smoothing J. Welles Wilder Jr. used throughout *New Concepts in Technical
    * Trading Systems* (1978) and the one already embedded inside {@code ATR},
    * {@code RSI}, {@code ADX} and {@code PLUS_DM}; here it is available on its
    * own. It reacts about half as fast as an {@code EMA} of the same period,
    * which is what makes it the smoother of choice for volatility and
    * directional-movement work. Sold under several names for one object: RMA,
    * SMMA, Wilder's Smoothing, WildersAverage, WilderMA.
    * <p><b>Formula</b>
    * <pre>{@code
    * alpha = 1 / period; RMA_t = alpha * price_t + (1 - alpha) * RMA_{t-1}. Seed: RMA = SMA of the first `period` bars.
    * }</pre>
    * <p><b>Notes</b>
    * <ul>
    * <li>{@code RMA(TRANGE(high, low, close), period)} is {@code ATR(period)} bit for bit, provided both functions' unstable periods are set to the same value. Each function owns its own unstable-period knob, so that is the caller's to arrange.</li>
    * <li>Wilder's own canonical period is 14, and pandas-ta uses 10; the default here is 30, following the rest of the TA-Lib moving-average family.</li>
    * <li>The smoothing factor {@code 1/period} equals an {@code EMA}'s {@code 2/(period+1)} at {@code 2*period-1}, which is why Wilder's 14-period smoothing is often described as a 27-day EMA. The *seed windows* differ, though — {@code period} bars against {@code 2*period-1} — so {@code RMA(x, period)} is not {@code EMA(x, 2*period-1)}: the two start far apart and converge only slowly.</li>
    * <li>A period of 1 performs no smoothing: alpha is exactly 1 and the output is a copy of the input.</li>
    * </ul>
    * <p>Values are written only where the indicator is defined. The returned
    * {@link OutRange} says where they start and how many there are; nothing
    * outside that range is touched, and the library never pads with NaN. A
    * valid range shorter than {@link Core#RMA_Lookback} is a <b>success with no
    * values</b> ({@code count() == 0}), not an error.
    *
    * @param startIdx First bar of the requested range (inclusive).
    * @param endIdx Last bar of the requested range (inclusive).
    * @param inReal price/data series to smooth.
    * @param optInTimePeriod Number of bars in the average; sets smoothing alpha
    *        = 1/period (default 30; range 1..100000; {@code Integer.MIN_VALUE} selects
    *        the default).
    * @param outReal the Wilder-smoothed average. Must hold at least
    *        {@code endIdx - startIdx + 1} values.
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
    * @see Core#EMA
    * @see Core#SMA
    * @see Core#ATR
    * @see Core#RSI
    * @see Core#MA
    */
   public OutRange RMA( int startIdx,
                        int endIdx,
                        double inReal[],
                        int optInTimePeriod,
                        double outReal[] )
   {
      requireIndexRange("RMA", startIdx, endIdx);
      int guardStart = clampedStart("RMA", startIdx, RMA_Lookback(optInTimePeriod));
      int guardInLen = endIdx + 1;
      int guardOutLen = guardStart > endIdx ? 0 : endIdx - guardStart + 1;
      requireLength("RMA", "inReal", inReal, guardInLen);
      requireLength("RMA", "outReal", outReal, guardOutLen);
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      RetCode retCode = RMA_Impl(startIdx, endIdx, inReal, optInTimePeriod, outBegIdx, outNBElement, outReal);
      if( retCode != RetCode.Success ) {
         throw failure("RMA", retCode);
      }
      return new OutRange(outBegIdx.value, outNBElement.value);
   }
   /**
    * Wilder's smoothed moving average: an exponential average whose smoothing
    * factor is {@code 1/period} rather than the usual {@code 2/(period+1)},
    * seeded with a simple average of the first {@code period} bars. This is the
    * smoothing J. Welles Wilder Jr. used throughout *New Concepts in Technical
    * Trading Systems* (1978) and the one already embedded inside {@code ATR},
    * {@code RSI}, {@code ADX} and {@code PLUS_DM}; here it is available on its
    * own. It reacts about half as fast as an {@code EMA} of the same period,
    * which is what makes it the smoother of choice for volatility and
    * directional-movement work. Sold under several names for one object: RMA,
    * SMMA, Wilder's Smoothing, WildersAverage, WilderMA.
    * <p><b>Formula</b>
    * <pre>{@code
    * alpha = 1 / period; RMA_t = alpha * price_t + (1 - alpha) * RMA_{t-1}. Seed: RMA = SMA of the first `period` bars.
    * }</pre>
    * <p><b>Notes</b>
    * <ul>
    * <li>{@code RMA(TRANGE(high, low, close), period)} is {@code ATR(period)} bit for bit, provided both functions' unstable periods are set to the same value. Each function owns its own unstable-period knob, so that is the caller's to arrange.</li>
    * <li>Wilder's own canonical period is 14, and pandas-ta uses 10; the default here is 30, following the rest of the TA-Lib moving-average family.</li>
    * <li>The smoothing factor {@code 1/period} equals an {@code EMA}'s {@code 2/(period+1)} at {@code 2*period-1}, which is why Wilder's 14-period smoothing is often described as a 27-day EMA. The *seed windows* differ, though — {@code period} bars against {@code 2*period-1} — so {@code RMA(x, period)} is not {@code EMA(x, 2*period-1)}: the two start far apart and converge only slowly.</li>
    * <li>A period of 1 performs no smoothing: alpha is exactly 1 and the output is a copy of the input.</li>
    * </ul>
    * <p>This is the {@code float[]} overload. The arithmetic is performed in
    * {@code double} before being written to the {@code double[]} output, so a
    * result beyond {@code float} range is still representable.
    * <p>Values are written only where the indicator is defined. The returned
    * {@link OutRange} says where they start and how many there are; nothing
    * outside that range is touched, and the library never pads with NaN. A
    * valid range shorter than {@link Core#RMA_Lookback} is a <b>success with no
    * values</b> ({@code count() == 0}), not an error.
    *
    * @param startIdx First bar of the requested range (inclusive).
    * @param endIdx Last bar of the requested range (inclusive).
    * @param inReal price/data series to smooth.
    * @param optInTimePeriod Number of bars in the average; sets smoothing alpha
    *        = 1/period (default 30; range 1..100000; {@code Integer.MIN_VALUE} selects
    *        the default).
    * @param outReal the Wilder-smoothed average. Must hold at least
    *        {@code endIdx - startIdx + 1} values.
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
    * @see Core#EMA
    * @see Core#SMA
    * @see Core#ATR
    * @see Core#RSI
    * @see Core#MA
    */
   public OutRange RMA( int startIdx,
                        int endIdx,
                        float inReal[],
                        int optInTimePeriod,
                        double outReal[] )
   {
      requireIndexRange("RMA", startIdx, endIdx);
      int guardStart = clampedStart("RMA", startIdx, RMA_Lookback(optInTimePeriod));
      int guardInLen = endIdx + 1;
      int guardOutLen = guardStart > endIdx ? 0 : endIdx - guardStart + 1;
      requireLength("RMA", "inReal", inReal, guardInLen);
      requireLength("RMA", "outReal", outReal, guardOutLen);
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      RetCode retCode = RMA_Impl(startIdx, endIdx, inReal, optInTimePeriod, outBegIdx, outNBElement, outReal);
      if( retCode != RetCode.Success ) {
         throw failure("RMA", retCode);
      }
      return new OutRange(outBegIdx.value, outNBElement.value);
   }
/**** Streaming API *****/

   /**
    * A live RMA stream (unrelated to {@code java.util.stream}): one value per
    * closed bar, bit-identical to {@link Core#RMA} over the same series.
    * Open with {@link Core#rmaOpen}; there is no close — the handle is
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
   public static final class RmaStream {
      Core core;
      int optInTimePeriod;
      double prevMA;
      double wAlpha;
      double wBeta;
      double cur_outReal;
      int outRangeBegIdx;
      int outRangeCount;

      RmaStream( Core core ) { this.core = core; }

      /**
       * The bars this stream has an output for, in the input series'
       * coordinates: {@code [begIdx, begIdx + count)}.
       * <p>It is what {@link Core#RMA} reports over the same bars: the
       * opener sets it to {@code (lookback, historyLen - lookback)}, every
       * {@code update} adds one to the count — a bar rejected for being
       * non-finite included, because it still happened — {@code peek} leaves
       * it alone, and {@code clone()} carries it verbatim. A plain
       * {@code open} hands back only the last value, a subset of this range,
       * because the caller chose not to take the fill.
       */
      public OutRange outRange() { return new OutRange(outRangeBegIdx, outRangeCount); }

      RmaStream( RmaStream other ) {
         this.core = other.core;
         this.optInTimePeriod = other.optInTimePeriod;
         this.prevMA = other.prevMA;
         this.wAlpha = other.wAlpha;
         this.wBeta = other.wBeta;
         this.cur_outReal = other.cur_outReal;
         this.outRangeBegIdx = other.outRangeBegIdx;
         this.outRangeCount = other.outRangeCount;
      }

      /**
       * Commit one closed bar, returning the new current value.
       * Never allocates handle state.
       * <p>Throws {@link IllegalArgumentException} if any bar value is not
       * finite (NaN or an infinity). That check runs before anything is
       * written, so the state is left exactly as it was: the rejected bar's
       * output is the previous value, held, and {@link #value()} answers it.
       * The stream stays usable, so skip the bar or re-open on a clean
       * history. {@link #outRange()} does advance: the bar happened and
       * occupies a position in the series, so the handle counts it, which is
       * what keeps two handles on one feed aligned when only one rejects.
       * This is the one place the streaming tier is stricter than
       * the batch API, which computes on whatever it is given: a handle
       * retains its state, so a single non-finite bar would poison every
       * later value it produces.
       */
      public double update( double inReal ) {
         if( !Double.isFinite(inReal) ) {
            if( this.outRangeCount < MAX_INDEX ) this.outRangeCount++;
            throw new TaLibArgumentException("RMA update: BadParam", RetCode.BadParam);
         }
         core.rmaStepImpl(this, inReal);
         if( this.outRangeCount < MAX_INDEX ) this.outRangeCount++;
         return this.cur_outReal;
      }

      /**
       * Commit {@code n} closed bars and write their {@code n} values, in one
       * call — exactly {@code n} back-to-back {@code update} calls, with one
       * set of argument checks instead of {@code n}. {@code n} is
       * {@code inReal.length}; the outputs must hold at least that many, and must
       * not be the same array as an input or as each other.
       * <p>{@link #outRange()} counts what this call took in, which is what makes a
       * rejection readable: a non-finite bar {@code k} throws
       * {@link IllegalArgumentException} exactly as {@code update} would, with
       * the bars before {@code k} committed and written, bar {@code k} and
       * everything after it not, and the count advanced by {@code k + 1} —
       * the committed bars plus the rejected one.
       */
      public void updateAndFill( double inReal[], double outReal[] ) {
         requireArgument("RMA updateAndFill", "inReal", inReal);
         requireArgument("RMA updateAndFill", "outReal", outReal);
         final int barCount = inReal.length;
         if( outReal.length < barCount || (Object)outReal == (Object)inReal )
            throw new TaLibArgumentException("RMA updateAndFill: BadParam", RetCode.BadParam);
         for( int i = 0; i < barCount; i++ ) {
            if( !Double.isFinite(inReal[i]) ) {
               if( this.outRangeCount < MAX_INDEX ) this.outRangeCount++;
               throw new TaLibArgumentException("RMA updateAndFill: BadParam", RetCode.BadParam);
            }
            core.rmaStepImpl(this, inReal[i]);
            outReal[i] = this.cur_outReal;
            if( this.outRangeCount < MAX_INDEX ) this.outRangeCount++;
         }
      }

      /**
       * Evaluate a forming bar without committing — bit-identical to what the
       * next {@code update} with the same bar would return — the same
       * transition, with every store it would make carried in a local instead.
       * Never writes this handle, so peeks may
       * run concurrently with each other. It copies nothing: the frame runs against this handle, reading its
       * buffers and storing what the step would commit into locals, so the cost
       * does not grow with the period and {@code peek} never allocates.
       */
      public double peek( double inReal ) {
         if( !Double.isFinite(inReal) )
            throw new TaLibArgumentException("RMA peek: BadParam", RetCode.BadParam);
         RmaStream sp = this;
         double cur_outReal = sp.cur_outReal;
         double prevMA = sp.prevMA;
         prevMA = Math.fma(sp.wBeta, prevMA, sp.wAlpha * inReal);
         cur_outReal = prevMA;
         return cur_outReal;
      }

      /**
       * The value at the last bar this stream counted — the bar
       * {@link #outRange()} ends on. The last history bar right after open,
       * then whatever the latest accepted {@code update} returned.
       * A pure field read; {@code peek} does not change it.
       */
      public double value() {
         return this.cur_outReal;
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
      public RmaStream clone() {
         return new RmaStream(this);
      }
   }
   void rmaStepImpl( RmaStream sp, double inReal )
   {
      sp.prevMA = Math.fma(sp.wBeta, sp.prevMA, sp.wAlpha * inReal);
      sp.cur_outReal = sp.prevMA;
   }
   private RetCode rmaOpenImpl( RmaStream sp, double inReal[], int startIdx, int optInTimePeriod, MInteger outBegIdx, MInteger outNBElement, double outReal[], int outStride )
   {
      double tempReal = 0;
      double prevMA = 0;
      double wAlpha = 0;
      double wBeta = 0;
      int i = 0;
      int today = 0;
      int outIdx = 0;
      int lookbackTotal = 0;
      int historyLen = inReal.length;
      int endIdx = historyLen - 1;
      if( historyLen < 1 ) {
         return RetCode.OutOfRangeStartIndex;
      }
      if( historyLen > MAX_INDEX + 1 ) {
         return RetCode.OutOfRangeEndIndex;
      }
      if( optInTimePeriod == Integer.MIN_VALUE ) {
         optInTimePeriod = 30;
      } else if( optInTimePeriod < 1 || optInTimePeriod > 100000 ) {
         return RetCode.BadParam;
      }
      if( startIdx > endIdx ) {
         outBegIdx.value = 0;
         outNBElement.value = 0;
         return RetCode.InsufficientHistory;
      }
      outBegIdx.value = 0;
      outNBElement.value = 0;
      /* Identify the minimum number of price bar needed
       * to calculate at least one output.
       */
      lookbackTotal = RMA_Lookback(optInTimePeriod);
      /* Move up the start index if there is not
       * enough initial data.
       */
      if( startIdx < lookbackTotal ) {
         startIdx = lookbackTotal;
      }
      /* Make sure there is still something to evaluate. */
      if( startIdx > endIdx ) {
         return RetCode.InsufficientHistory ;
      }
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
      wBeta = (double)(optInTimePeriod - 1) / (double)optInTimePeriod;
      wAlpha = 1.0 - wBeta;
      outBegIdx.value = startIdx;
      /* Seed with a simple average of the first 'period' values. */
      today = startIdx - lookbackTotal;
      i = optInTimePeriod;
      tempReal = 0.0;
      while( i-- > 0 ) {
         tempReal += inReal[today++];
      }
      prevMA = tempReal / optInTimePeriod;
      /* Skip the unstable period. */
      while( today <= startIdx ) {
         prevMA = Math.fma(wBeta, prevMA, wAlpha * inReal[today]);
         today += 1;
      }
      outReal[0 * outStride] = prevMA;
      outIdx = 1;
      while( today <= endIdx ) {
         prevMA = Math.fma(wBeta, prevMA, wAlpha * inReal[today]);
         today += 1;
         outReal[outIdx++ * outStride] = prevMA;
      }
      outNBElement.value = outIdx;
      /* Capture the live batch state into the handle. */
      sp.optInTimePeriod = optInTimePeriod;
      sp.prevMA = prevMA;
      sp.wAlpha = wAlpha;
      sp.wBeta = wBeta;
      sp.cur_outReal = outReal[(outNBElement.value - 1) * outStride];
      return RetCode.Success;
   }
   /* rmaOpenAndFill anchored at startIdx — the composed-open fusion seam. */
   RmaStream rmaOpenAndFillInternal( double inReal[], int startIdx, int optInTimePeriod, MInteger outBegIdx, MInteger outNBElement, double outReal[] )
   {
      RmaStream sp = new RmaStream(this);
      RetCode retCode = rmaOpenImpl(sp, inReal, startIdx, optInTimePeriod, outBegIdx, outNBElement, outReal, 1);
      sp.outRangeBegIdx = outBegIdx.value;
      sp.outRangeCount = outNBElement.value;
      if( retCode == RetCode.Success ) {
         return sp;
      }
      if( retCode == RetCode.InsufficientHistory ) {
         throw new InsufficientHistoryException("RMA openAndFill: history shorter than lookback + 1");
      }
      if( retCode == RetCode.InternalError ) {
         throw new TaLibStateException("RMA openAndFill: internal error", retCode);
      }
      throw new TaLibArgumentException("RMA openAndFill: " + retCode, retCode);
   }
   /* Internal startIdx-anchored open behind rmaOpen (composition seam). */
   RmaStream rmaOpenInternal( double inReal[], int startIdx, int optInTimePeriod )
   {
      RmaStream sp = new RmaStream(this);
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      double[] sink_outReal = new double[1];
      RetCode retCode = rmaOpenImpl(sp, inReal, startIdx, optInTimePeriod, outBegIdx, outNBElement, sink_outReal, 0);
      sp.outRangeBegIdx = outBegIdx.value;
      sp.outRangeCount = outNBElement.value;
      if( retCode == RetCode.Success ) {
         return sp;
      }
      if( retCode == RetCode.InsufficientHistory ) {
         throw new InsufficientHistoryException("RMA open: history shorter than lookback + 1");
      }
      if( retCode == RetCode.InternalError ) {
         throw new TaLibStateException("RMA open: internal error", retCode);
      }
      throw new TaLibArgumentException("RMA open: " + retCode, retCode);
   }
   /**
    * Open a live RMA stream over the warm-up history; the handle's
    * {@code value()} starts at the last history bar's value — bit-identical
    * to {@link Core#RMA} at that bar.
    * <p>The history must hold at least {@code RMA_Lookback(...) + 1} bars
    * (unstable-period aware), or {@link InsufficientHistoryException} is
    * thrown. Out-of-range parameters throw {@link IllegalArgumentException}
    * ({@code Integer.MIN_VALUE} selects an integer parameter's documented
    * default, as in the batch API). An EMPTY history throws
    * {@link IndexOutOfBoundsException} — its implied {@code startIdx} of 0
    * names no bar — and a null argument {@link IllegalArgumentException},
    * both ahead of everything above.
    */
   public RmaStream rmaOpen( double inReal[], int optInTimePeriod )
   {
      requireArgument("RMA open", "inReal", inReal);
      requireHistory("RMA open", inReal.length);
      return rmaOpenInternal(inReal, 0, optInTimePeriod);
   }
   /**
    * {@link Core#rmaOpen} that also fills the output array(s) bit-identically
    * to {@link Core#RMA} over the whole history in the same single pass
    * (no separate batch call needed for the warm-up plot). Output arrays must
    * not alias the inputs or each other, and must hold
    * {@code historyLen - lookback} values — both checked before anything is
    * written, so an undersized array is an {@link IllegalArgumentException}
    * naming it rather than a fault from inside the fill.
    * <p>The range written is on the returned handle:
    * {@link RmaStream#outRange()}.
    */
   public RmaStream rmaOpenAndFill( double inReal[], int optInTimePeriod, double outReal[] )
   {
      requireArgument("RMA openAndFill", "inReal", inReal);
      requireHistory("RMA openAndFill", inReal.length);
      int guardOutLen = openFillCount("RMA openAndFill", inReal.length, RMA_Lookback(optInTimePeriod));
      requireLength("RMA openAndFill", "outReal", outReal, guardOutLen);
      if( (Object)outReal == (Object)inReal ) {
         throw new TaLibArgumentException("RMA openAndFill: " + RetCode.BadParam, RetCode.BadParam);
      }
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      return rmaOpenAndFillInternal(inReal, 0, optInTimePeriod, outBegIdx, outNBElement, outReal);
   }
