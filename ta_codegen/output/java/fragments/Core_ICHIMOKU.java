/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  MF       Mario Fortier
 *  CC       Claude Code (AI assistant)
 *
 * Change history:
 *
 *  MMDDYY BY     Description
 *  -------------------------------------------------------------------
 *  100726 MF,CC  Initial version (#490).
 */

   /**
    * Number of leading input bars {@link Core#ichimoku} consumes before it can
    * produce its first value.
    * <p>Equivalently, the index of the first bar with a value when the whole
    * series is requested. Feed at least {@code lookback + 1} bars to get any
    * output.
    *
    * @param optInTenkanPeriod Period of the conversion line (default 9; range
    *        2..100000; {@code Integer.MIN_VALUE} selects the default).
    * @param optInKijunPeriod Period of the base line, and the forward shift of
    *        the two spans (default 26; range 2..100000; {@code Integer.MIN_VALUE}
    *        selects the default).
    * @param optInSenkouBPeriod Period of the second leading span (default 52;
    *        range 2..100000; {@code Integer.MIN_VALUE} selects the default).
    * @return The lookback, or {@code -1} if a parameter is out of range.
    */
   public int ichimokuLookback( int optInTenkanPeriod, int optInKijunPeriod, int optInSenkouBPeriod )
   {
      if( optInTenkanPeriod == Integer.MIN_VALUE ) {
         optInTenkanPeriod = 9;
      } else if( optInTenkanPeriod < 2 || optInTenkanPeriod > 100000 ) {
         return -1;
      }
      if( optInKijunPeriod == Integer.MIN_VALUE ) {
         optInKijunPeriod = 26;
      } else if( optInKijunPeriod < 2 || optInKijunPeriod > 100000 ) {
         return -1;
      }
      if( optInSenkouBPeriod == Integer.MIN_VALUE ) {
         optInSenkouBPeriod = 52;
      } else if( optInSenkouBPeriod < 2 || optInSenkouBPeriod > 100000 ) {
         return -1;
      }
      int longest;
      /* Each line is a midpoint over its own window, so each needs its own
       * window filled; the first bar that has all of them is the longest one's.
       * The MAX is not simplified to the Senkou B period: nothing orders the
       * three, and a kijun longer than senkouB dominates.
       *
       * Span A is the mean of the other two lines, so it adds nothing: it is
       * ready on the bar they both are.
       */
      longest = optInTenkanPeriod;
      if( optInKijunPeriod > longest ) {
         longest = optInKijunPeriod;
      }
      if( optInSenkouBPeriod > longest ) {
         longest = optInSenkouBPeriod;
      }
      return longest - 1 ;

   }
   /**
    * How many bars ahead (positive) or behind (negative) of the bar that
    * computed it a chart draws one output of {@link Core#ichimoku}.
    * <p>The values are never shifted: this describes the drawing only.
    *
    * @param optInTenkanPeriod Period of the conversion line (default 9; range
    *        2..100000; {@code Integer.MIN_VALUE} selects the default).
    * @param optInKijunPeriod Period of the base line, and the forward shift of
    *        the two spans (default 26; range 2..100000; {@code Integer.MIN_VALUE}
    *        selects the default).
    * @param optInSenkouBPeriod Period of the second leading span (default 52;
    *        range 2..100000; {@code Integer.MIN_VALUE} selects the default).
    * @param outputIdx Position of the output in the batch signature, from 0.
    * @return The display shift, or {@code Integer.MIN_VALUE} if a parameter is
    *        out of range or the index names no output.
    */
   public int ichimokuDisplayShift( int optInTenkanPeriod, int optInKijunPeriod, int optInSenkouBPeriod, int outputIdx )
   {
      if( ichimokuLookback( optInTenkanPeriod, optInKijunPeriod, optInSenkouBPeriod ) < 0 ) {
         return Integer.MIN_VALUE;
      }
      if( optInTenkanPeriod == Integer.MIN_VALUE ) {
         optInTenkanPeriod = 9;
      } else if( optInTenkanPeriod < 2 || optInTenkanPeriod > 100000 ) {
         return Integer.MIN_VALUE;
      }
      if( optInKijunPeriod == Integer.MIN_VALUE ) {
         optInKijunPeriod = 26;
      } else if( optInKijunPeriod < 2 || optInKijunPeriod > 100000 ) {
         return Integer.MIN_VALUE;
      }
      if( optInSenkouBPeriod == Integer.MIN_VALUE ) {
         optInSenkouBPeriod = 52;
      } else if( optInSenkouBPeriod < 2 || optInSenkouBPeriod > 100000 ) {
         return Integer.MIN_VALUE;
      }
      if( outputIdx < 0 || outputIdx >= 4 ) {
         return Integer.MIN_VALUE;
      }
      if( outputIdx == 0 || outputIdx == 1 ) {
         return 0;
      }
      /* rL9: a shift of s means a chart draws the value computed at bar i at
       * bar i + s. The two spans are the leading ones -- Goichi Hosoda draws
       * them kijun bars into the future, which is what makes the cloud sit
       * ahead of price -- so their shift is POSITIVE, where dpo.c's and
       * fractal.c's are negative. The conversion and base lines are drawn on
       * the bar that computed them.
       *
       * The shift reads the optional parameters and the output index only,
       * never a setting (rL9).
       */
      if( outputIdx == 2 || outputIdx == 3 ) {
         return optInKijunPeriod ;
      }
      return 0 ;

   }
   RetCode ichimokuImpl( int startIdx,
                         int endIdx,
                         double inHigh[],
                         double inLow[],
                         int optInTenkanPeriod,
                         int optInKijunPeriod,
                         int optInSenkouBPeriod,
                         MInteger outBegIdx,
                         MInteger outNBElement,
                         double outTenkanSen[],
                         double outKijunSen[],
                         double outSenkouSpanA[],
                         double outSenkouSpanB[] )
   {
      double tenkan = 0;
      double kijun = 0;
      double hiT = 0;
      double loT = 0;
      double hiK = 0;
      double loK = 0;
      double hiB = 0;
      double loB = 0;
      double tmp = 0;
      int lookbackTotal = 0;
      int today = 0;
      int outIdx = 0;
      int i = 0;
      int trailT = 0;
      int trailK = 0;
      int trailB = 0;
      int hiIdxT = 0;
      int loIdxT = 0;
      int hiIdxK = 0;
      int loIdxK = 0;
      int hiIdxB = 0;
      int loIdxB = 0;
      if( (startIdx < 0) || (startIdx > INDEX_MAX) ) {
         return RetCode.OUT_OF_RANGE_START_INDEX ;
      }
      if( (endIdx < 0) || (endIdx > INDEX_MAX) || (endIdx < startIdx)) {
         return RetCode.OUT_OF_RANGE_END_INDEX ;
      }
      if( optInTenkanPeriod == Integer.MIN_VALUE ) {
         optInTenkanPeriod = 9;
      } else if( optInTenkanPeriod < 2 || optInTenkanPeriod > 100000 ) {
         return RetCode.BAD_PARAM;
      }
      if( optInKijunPeriod == Integer.MIN_VALUE ) {
         optInKijunPeriod = 26;
      } else if( optInKijunPeriod < 2 || optInKijunPeriod > 100000 ) {
         return RetCode.BAD_PARAM;
      }
      if( optInSenkouBPeriod == Integer.MIN_VALUE ) {
         optInSenkouBPeriod = 52;
      } else if( optInSenkouBPeriod < 2 || optInSenkouBPeriod > 100000 ) {
         return RetCode.BAD_PARAM;
      }
      if( outTenkanSen == outKijunSen || outTenkanSen == outSenkouSpanA || outTenkanSen == outSenkouSpanB || outKijunSen == outSenkouSpanA || outKijunSen == outSenkouSpanB || outSenkouSpanA == outSenkouSpanB ) {
         return RetCode.BAD_PARAM ;
      }
      /* Goichi Hosoda's Ichimoku Kinko Hyo, the four lines that are computed
       * from price alone:
       *
       *    Tenkan-sen  = midpoint of the last tenkan bars
       *    Kijun-sen   = midpoint of the last kijun bars
       *    Senkou A    = mean of those two lines
       *    Senkou B    = midpoint of the last senkouB bars
       *
       * where a midpoint is (highest high + lowest low)/2 over the window, which
       * is TA_MIDPRICE. The two spans are drawn kijun bars ahead; that is
       * display-shift metadata (ichimoku_display_shift), never a shift of the
       * values, so every output is written at the bar that computed it (rL9).
       * The Chikou span is the close displaced backward and carries no
       * computation, so it is not an output here.
       *
       * SPAN A HALVES THE TWO ALREADY-ROUNDED MIDPOINTS. Folding it into
       * (hiT + loT + hiK + loK)/4 is the same value in real arithmetic and a
       * different double on a quarter of the bars; only a bit-exact gate
       * against TA_MIDPRICE and TA_MEDPRICE sees the difference.
       *
       * The three windows use the cached-extreme-plus-rescan idiom of stoch.c
       * rather than midprice.c's block scan. Both are exact -- an extremum is a
       * selection, so the bits are whichever input bar won, whatever the scan
       * order -- so the choice is streamability and cost, not correctness: the
       * block-scan form produces a whole block at a time and cannot be a per-bar
       * automaton, which is why midprice.c carries a midprice_ALT1 for the
       * streaming tier (#147). This form needs no twin.
       */
      lookbackTotal = ichimokuLookback(optInTenkanPeriod, optInKijunPeriod, optInSenkouBPeriod);
      /* Move up the start index if there is not
       * enough initial data.
       */
      if( startIdx < lookbackTotal ) {
         startIdx = lookbackTotal;
      }
      /* Make sure there is still something to evaluate. */
      if( startIdx > endIdx ) {
         outBegIdx.value = 0;
         outNBElement.value = 0;
         return RetCode.SUCCESS ;
      }
      outIdx = 0;
      today = startIdx;
      trailT = today - (optInTenkanPeriod - 1);
      trailK = today - (optInKijunPeriod - 1);
      trailB = today - (optInSenkouBPeriod - 1);
      hiIdxT = -1;
      loIdxT = -1;
      hiIdxK = -1;
      loIdxK = -1;
      hiIdxB = -1;
      loIdxB = -1;
      hiT = 0.0;
      loT = 0.0;
      hiK = 0.0;
      loK = 0.0;
      hiB = 0.0;
      loB = 0.0;
      while( today <= endIdx ) {
         /* Tenkan window. The cached index is refreshed on a tie, so a flat
          * stretch never rescans.
          */
         tmp = inHigh[today];
         if( hiIdxT < trailT ) {
            hiIdxT = trailT;
            hiT = inHigh[hiIdxT];
            i = hiIdxT;
            while( ++i <= today ) {
               tmp = inHigh[i];
               if( tmp > hiT ) {
                  hiIdxT = i;
                  hiT = tmp;
               }
            }
         } else if( tmp >= hiT ) {
            hiIdxT = today;
            hiT = tmp;
         }
         tmp = inLow[today];
         if( loIdxT < trailT ) {
            loIdxT = trailT;
            loT = inLow[loIdxT];
            i = loIdxT;
            while( ++i <= today ) {
               tmp = inLow[i];
               if( tmp < loT ) {
                  loIdxT = i;
                  loT = tmp;
               }
            }
         } else if( tmp <= loT ) {
            loIdxT = today;
            loT = tmp;
         }
         /* Kijun window. */
         tmp = inHigh[today];
         if( hiIdxK < trailK ) {
            hiIdxK = trailK;
            hiK = inHigh[hiIdxK];
            i = hiIdxK;
            while( ++i <= today ) {
               tmp = inHigh[i];
               if( tmp > hiK ) {
                  hiIdxK = i;
                  hiK = tmp;
               }
            }
         } else if( tmp >= hiK ) {
            hiIdxK = today;
            hiK = tmp;
         }
         tmp = inLow[today];
         if( loIdxK < trailK ) {
            loIdxK = trailK;
            loK = inLow[loIdxK];
            i = loIdxK;
            while( ++i <= today ) {
               tmp = inLow[i];
               if( tmp < loK ) {
                  loIdxK = i;
                  loK = tmp;
               }
            }
         } else if( tmp <= loK ) {
            loIdxK = today;
            loK = tmp;
         }
         /* Senkou B window. */
         tmp = inHigh[today];
         if( hiIdxB < trailB ) {
            hiIdxB = trailB;
            hiB = inHigh[hiIdxB];
            i = hiIdxB;
            while( ++i <= today ) {
               tmp = inHigh[i];
               if( tmp > hiB ) {
                  hiIdxB = i;
                  hiB = tmp;
               }
            }
         } else if( tmp >= hiB ) {
            hiIdxB = today;
            hiB = tmp;
         }
         tmp = inLow[today];
         if( loIdxB < trailB ) {
            loIdxB = trailB;
            loB = inLow[loIdxB];
            i = loIdxB;
            while( ++i <= today ) {
               tmp = inLow[i];
               if( tmp < loB ) {
                  loIdxB = i;
                  loB = tmp;
               }
            }
         } else if( tmp <= loB ) {
            loIdxB = today;
            loB = tmp;
         }
         /* Each midpoint is spelled as midprice.c spells it, and Span A halves
          * the two lines rather than the four extremes.
          */
         tenkan = (hiT + loT) / 2.0;
         kijun = (hiK + loK) / 2.0;
         outTenkanSen[outIdx] = tenkan;
         outKijunSen[outIdx] = kijun;
         outSenkouSpanA[outIdx] = (tenkan + kijun) / 2.0;
         outSenkouSpanB[outIdx] = (hiB + loB) / 2.0;
         outIdx = outIdx + 1;
         trailT = trailT + 1;
         trailK = trailK + 1;
         trailB = trailB + 1;
         today = today + 1;
      }
      outNBElement.value = outIdx;
      outBegIdx.value = startIdx;
      return RetCode.SUCCESS ;
   }
   RetCode ichimokuImpl( int startIdx,
                         int endIdx,
                         float inHigh[],
                         float inLow[],
                         int optInTenkanPeriod,
                         int optInKijunPeriod,
                         int optInSenkouBPeriod,
                         MInteger outBegIdx,
                         MInteger outNBElement,
                         double outTenkanSen[],
                         double outKijunSen[],
                         double outSenkouSpanA[],
                         double outSenkouSpanB[] )
   {
      double tenkan = 0;
      double kijun = 0;
      double hiT = 0;
      double loT = 0;
      double hiK = 0;
      double loK = 0;
      double hiB = 0;
      double loB = 0;
      double tmp = 0;
      int lookbackTotal = 0;
      int today = 0;
      int outIdx = 0;
      int i = 0;
      int trailT = 0;
      int trailK = 0;
      int trailB = 0;
      int hiIdxT = 0;
      int loIdxT = 0;
      int hiIdxK = 0;
      int loIdxK = 0;
      int hiIdxB = 0;
      int loIdxB = 0;
      if( (startIdx < 0) || (startIdx > INDEX_MAX) ) {
         return RetCode.OUT_OF_RANGE_START_INDEX ;
      }
      if( (endIdx < 0) || (endIdx > INDEX_MAX) || (endIdx < startIdx)) {
         return RetCode.OUT_OF_RANGE_END_INDEX ;
      }
      if( optInTenkanPeriod == Integer.MIN_VALUE ) {
         optInTenkanPeriod = 9;
      } else if( optInTenkanPeriod < 2 || optInTenkanPeriod > 100000 ) {
         return RetCode.BAD_PARAM;
      }
      if( optInKijunPeriod == Integer.MIN_VALUE ) {
         optInKijunPeriod = 26;
      } else if( optInKijunPeriod < 2 || optInKijunPeriod > 100000 ) {
         return RetCode.BAD_PARAM;
      }
      if( optInSenkouBPeriod == Integer.MIN_VALUE ) {
         optInSenkouBPeriod = 52;
      } else if( optInSenkouBPeriod < 2 || optInSenkouBPeriod > 100000 ) {
         return RetCode.BAD_PARAM;
      }
      if( outTenkanSen == outKijunSen || outTenkanSen == outSenkouSpanA || outTenkanSen == outSenkouSpanB || outKijunSen == outSenkouSpanA || outKijunSen == outSenkouSpanB || outSenkouSpanA == outSenkouSpanB ) {
         return RetCode.BAD_PARAM ;
      }
      lookbackTotal = ichimokuLookback(optInTenkanPeriod, optInKijunPeriod, optInSenkouBPeriod);
      if( startIdx < lookbackTotal ) {
         startIdx = lookbackTotal;
      }
      if( startIdx > endIdx ) {
         outBegIdx.value = 0;
         outNBElement.value = 0;
         return RetCode.SUCCESS ;
      }
      outIdx = 0;
      today = startIdx;
      trailT = today - (optInTenkanPeriod - 1);
      trailK = today - (optInKijunPeriod - 1);
      trailB = today - (optInSenkouBPeriod - 1);
      hiIdxT = -1;
      loIdxT = -1;
      hiIdxK = -1;
      loIdxK = -1;
      hiIdxB = -1;
      loIdxB = -1;
      hiT = 0.0;
      loT = 0.0;
      hiK = 0.0;
      loK = 0.0;
      hiB = 0.0;
      loB = 0.0;
      while( today <= endIdx ) {
         tmp = (double)inHigh[today];
         if( hiIdxT < trailT ) {
            hiIdxT = trailT;
            hiT = (double)inHigh[hiIdxT];
            i = hiIdxT;
            while( ++i <= today ) {
               tmp = (double)inHigh[i];
               if( tmp > hiT ) {
                  hiIdxT = i;
                  hiT = tmp;
               }
            }
         } else if( tmp >= hiT ) {
            hiIdxT = today;
            hiT = tmp;
         }
         tmp = (double)inLow[today];
         if( loIdxT < trailT ) {
            loIdxT = trailT;
            loT = (double)inLow[loIdxT];
            i = loIdxT;
            while( ++i <= today ) {
               tmp = (double)inLow[i];
               if( tmp < loT ) {
                  loIdxT = i;
                  loT = tmp;
               }
            }
         } else if( tmp <= loT ) {
            loIdxT = today;
            loT = tmp;
         }
         tmp = (double)inHigh[today];
         if( hiIdxK < trailK ) {
            hiIdxK = trailK;
            hiK = (double)inHigh[hiIdxK];
            i = hiIdxK;
            while( ++i <= today ) {
               tmp = (double)inHigh[i];
               if( tmp > hiK ) {
                  hiIdxK = i;
                  hiK = tmp;
               }
            }
         } else if( tmp >= hiK ) {
            hiIdxK = today;
            hiK = tmp;
         }
         tmp = (double)inLow[today];
         if( loIdxK < trailK ) {
            loIdxK = trailK;
            loK = (double)inLow[loIdxK];
            i = loIdxK;
            while( ++i <= today ) {
               tmp = (double)inLow[i];
               if( tmp < loK ) {
                  loIdxK = i;
                  loK = tmp;
               }
            }
         } else if( tmp <= loK ) {
            loIdxK = today;
            loK = tmp;
         }
         tmp = (double)inHigh[today];
         if( hiIdxB < trailB ) {
            hiIdxB = trailB;
            hiB = (double)inHigh[hiIdxB];
            i = hiIdxB;
            while( ++i <= today ) {
               tmp = (double)inHigh[i];
               if( tmp > hiB ) {
                  hiIdxB = i;
                  hiB = tmp;
               }
            }
         } else if( tmp >= hiB ) {
            hiIdxB = today;
            hiB = tmp;
         }
         tmp = (double)inLow[today];
         if( loIdxB < trailB ) {
            loIdxB = trailB;
            loB = (double)inLow[loIdxB];
            i = loIdxB;
            while( ++i <= today ) {
               tmp = (double)inLow[i];
               if( tmp < loB ) {
                  loIdxB = i;
                  loB = tmp;
               }
            }
         } else if( tmp <= loB ) {
            loIdxB = today;
            loB = tmp;
         }
         tenkan = (hiT + loT) / 2.0;
         kijun = (hiK + loK) / 2.0;
         outTenkanSen[outIdx] = tenkan;
         outKijunSen[outIdx] = kijun;
         outSenkouSpanA[outIdx] = (tenkan + kijun) / 2.0;
         outSenkouSpanB[outIdx] = (hiB + loB) / 2.0;
         outIdx = outIdx + 1;
         trailT = trailT + 1;
         trailK = trailK + 1;
         trailB = trailB + 1;
         today = today + 1;
      }
      outNBElement.value = outIdx;
      outBegIdx.value = startIdx;
      return RetCode.SUCCESS ;
   }
   /**
    * Ichimoku Kinko Hyo, "one glance equilibrium chart": four lines built from
    * highs and lows alone. Goichi Hosoda's reading is that a market's balance
    * is visible without any smoothing — each line is the midpoint of a window,
    * the mean of its highest high and its lowest low, so it marks the level at
    * which that stretch of trading was evenly divided. The conversion line
    * turns fastest, the base line is the reference, and the two leading spans
    * are drawn ahead of price, where the band between them is read as support
    * or resistance before it is reached.
    * <p>Formula and more info at <a
    * href="https://ta-lib.org/functions/ichimoku">ta-lib.org/functions/ichimoku</a>.
    * <p><b>Notes</b>
    * <ul>
    * <li>Each line is {@code TA_MIDPRICE} over its own period, and Span A is {@code TA_MEDPRICE} of the other two lines. Span A halves the two midpoints after each has been rounded, rather than averaging the four extremes, which is a different value in the last bit on about a quarter of the bars.</li>
    * <li>The two spans are drawn {@code kijunPeriod} bars ahead of the bar that computed them. That is a display shift: it is reported through the display-shift call and changes nothing about the values, the lookback or the returned range. Every output is written at the bar that computed it.</li>
    * <li>The lookback is the longest of the three periods less one. It is not the Senkou B period: nothing orders the three, so a base line longer than the second span dominates.</li>
    * <li>The Chikou span, the close drawn backward, carries no computation and is not an output here: it is the input series with a display shift.</li>
    * </ul>
    * <p>Values are written only where the indicator is defined. The returned
    * {@link OutRange} says where they start and how many there are, and the
    * library never pads with NaN. A valid range that ends before
    * {@link Core#ichimokuLookback} is a <b>success with no values</b>
    * ({@code count() == 0}), not an error.
    *
    * @param startIdx First bar of the requested range (inclusive).
    * @param endIdx Last bar of the requested range (inclusive).
    * @param inHigh High price series.
    * @param inLow Low price series.
    * @param optInTenkanPeriod Period of the conversion line (default 9; range
    *        2..100000; {@code Integer.MIN_VALUE} selects the default).
    * @param optInKijunPeriod Period of the base line, and the forward shift of
    *        the two spans (default 26; range 2..100000; {@code Integer.MIN_VALUE}
    *        selects the default).
    * @param optInSenkouBPeriod Period of the second leading span (default 52;
    *        range 2..100000; {@code Integer.MIN_VALUE} selects the default).
    * @param outTenkanSen Conversion line. Must hold at least
    *        {@code endIdx - max(startIdx, ichimokuLookback(...)) + 1} values, and
    *        never be empty: an empty array is an absent output.
    * @param outKijunSen Base line. Must hold at least
    *        {@code endIdx - max(startIdx, ichimokuLookback(...)) + 1} values, and
    *        never be empty: an empty array is an absent output.
    * @param outSenkouSpanA First leading span, drawn ahead by the base period.
    *        Must hold at least
    *        {@code endIdx - max(startIdx, ichimokuLookback(...)) + 1} values, and
    *        never be empty: an empty array is an absent output.
    * @param outSenkouSpanB Second leading span, drawn ahead by the base period.
    *        Must hold at least
    *        {@code endIdx - max(startIdx, ichimokuLookback(...)) + 1} values, and
    *        never be empty: an empty array is an absent output.
    * @return The range written: {@code begIdx} is the first bar with a value,
    *        {@code count} how many were written.
    * @throws IndexOutOfBoundsException if {@code startIdx} or {@code endIdx} is
    *        negative or above {@link Core#INDEX_MAX}, or {@code endIdx < startIdx}.
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
    * @see Core#midprice
    * @see Core#medprice
    * @see Core#sar
    * @see Core#bbands
    */
   public OutRange ichimoku( int startIdx,
                             int endIdx,
                             double inHigh[],
                             double inLow[],
                             int optInTenkanPeriod,
                             int optInKijunPeriod,
                             int optInSenkouBPeriod,
                             double outTenkanSen[],
                             double outKijunSen[],
                             double outSenkouSpanA[],
                             double outSenkouSpanB[] )
   {
      requireIndexRange("ICHIMOKU", startIdx, endIdx);
      int guardStart = clampedStart("ICHIMOKU", startIdx, ichimokuLookback(optInTenkanPeriod, optInKijunPeriod, optInSenkouBPeriod));
      int guardInLen = endIdx + 1;
      int guardOutLen = guardStart > endIdx ? 0 : endIdx - guardStart + 1;
      requireLength("ICHIMOKU", "inHigh", inHigh, guardInLen);
      requireLength("ICHIMOKU", "inLow", inLow, guardInLen);
      requireLength("ICHIMOKU", "outTenkanSen", outTenkanSen, guardOutLen);
      requireLength("ICHIMOKU", "outKijunSen", outKijunSen, guardOutLen);
      requireLength("ICHIMOKU", "outSenkouSpanA", outSenkouSpanA, guardOutLen);
      requireLength("ICHIMOKU", "outSenkouSpanB", outSenkouSpanB, guardOutLen);
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      RetCode retCode = ichimokuImpl(startIdx, endIdx, inHigh, inLow, optInTenkanPeriod, optInKijunPeriod, optInSenkouBPeriod, outBegIdx, outNBElement, outTenkanSen, outKijunSen, outSenkouSpanA, outSenkouSpanB);
      if( retCode != RetCode.SUCCESS ) {
         throw failure("ICHIMOKU", retCode);
      }
      return new OutRange(outBegIdx.value, outNBElement.value);
   }
   /**
    * Ichimoku Kinko Hyo, "one glance equilibrium chart": four lines built from
    * highs and lows alone. Goichi Hosoda's reading is that a market's balance
    * is visible without any smoothing — each line is the midpoint of a window,
    * the mean of its highest high and its lowest low, so it marks the level at
    * which that stretch of trading was evenly divided. The conversion line
    * turns fastest, the base line is the reference, and the two leading spans
    * are drawn ahead of price, where the band between them is read as support
    * or resistance before it is reached.
    * <p>Formula and more info at <a
    * href="https://ta-lib.org/functions/ichimoku">ta-lib.org/functions/ichimoku</a>.
    * <p><b>Notes</b>
    * <ul>
    * <li>Each line is {@code TA_MIDPRICE} over its own period, and Span A is {@code TA_MEDPRICE} of the other two lines. Span A halves the two midpoints after each has been rounded, rather than averaging the four extremes, which is a different value in the last bit on about a quarter of the bars.</li>
    * <li>The two spans are drawn {@code kijunPeriod} bars ahead of the bar that computed them. That is a display shift: it is reported through the display-shift call and changes nothing about the values, the lookback or the returned range. Every output is written at the bar that computed it.</li>
    * <li>The lookback is the longest of the three periods less one. It is not the Senkou B period: nothing orders the three, so a base line longer than the second span dominates.</li>
    * <li>The Chikou span, the close drawn backward, carries no computation and is not an output here: it is the input series with a display shift.</li>
    * </ul>
    * <p>This is the {@code float[]} overload. The arithmetic is performed in
    * {@code double} before being written to the {@code double[]} output, so a
    * result beyond {@code float} range is still representable.
    * <p>Values are written only where the indicator is defined. The returned
    * {@link OutRange} says where they start and how many there are, and the
    * library never pads with NaN. A valid range that ends before
    * {@link Core#ichimokuLookback} is a <b>success with no values</b>
    * ({@code count() == 0}), not an error.
    *
    * @param startIdx First bar of the requested range (inclusive).
    * @param endIdx Last bar of the requested range (inclusive).
    * @param inHigh High price series.
    * @param inLow Low price series.
    * @param optInTenkanPeriod Period of the conversion line (default 9; range
    *        2..100000; {@code Integer.MIN_VALUE} selects the default).
    * @param optInKijunPeriod Period of the base line, and the forward shift of
    *        the two spans (default 26; range 2..100000; {@code Integer.MIN_VALUE}
    *        selects the default).
    * @param optInSenkouBPeriod Period of the second leading span (default 52;
    *        range 2..100000; {@code Integer.MIN_VALUE} selects the default).
    * @param outTenkanSen Conversion line. Must hold at least
    *        {@code endIdx - max(startIdx, ichimokuLookback(...)) + 1} values, and
    *        never be empty: an empty array is an absent output.
    * @param outKijunSen Base line. Must hold at least
    *        {@code endIdx - max(startIdx, ichimokuLookback(...)) + 1} values, and
    *        never be empty: an empty array is an absent output.
    * @param outSenkouSpanA First leading span, drawn ahead by the base period.
    *        Must hold at least
    *        {@code endIdx - max(startIdx, ichimokuLookback(...)) + 1} values, and
    *        never be empty: an empty array is an absent output.
    * @param outSenkouSpanB Second leading span, drawn ahead by the base period.
    *        Must hold at least
    *        {@code endIdx - max(startIdx, ichimokuLookback(...)) + 1} values, and
    *        never be empty: an empty array is an absent output.
    * @return The range written: {@code begIdx} is the first bar with a value,
    *        {@code count} how many were written.
    * @throws IndexOutOfBoundsException if {@code startIdx} or {@code endIdx} is
    *        negative or above {@link Core#INDEX_MAX}, or {@code endIdx < startIdx}.
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
    * @see Core#midprice
    * @see Core#medprice
    * @see Core#sar
    * @see Core#bbands
    */
   public OutRange ichimoku( int startIdx,
                             int endIdx,
                             float inHigh[],
                             float inLow[],
                             int optInTenkanPeriod,
                             int optInKijunPeriod,
                             int optInSenkouBPeriod,
                             double outTenkanSen[],
                             double outKijunSen[],
                             double outSenkouSpanA[],
                             double outSenkouSpanB[] )
   {
      requireIndexRange("ICHIMOKU", startIdx, endIdx);
      int guardStart = clampedStart("ICHIMOKU", startIdx, ichimokuLookback(optInTenkanPeriod, optInKijunPeriod, optInSenkouBPeriod));
      int guardInLen = endIdx + 1;
      int guardOutLen = guardStart > endIdx ? 0 : endIdx - guardStart + 1;
      requireLength("ICHIMOKU", "inHigh", inHigh, guardInLen);
      requireLength("ICHIMOKU", "inLow", inLow, guardInLen);
      requireLength("ICHIMOKU", "outTenkanSen", outTenkanSen, guardOutLen);
      requireLength("ICHIMOKU", "outKijunSen", outKijunSen, guardOutLen);
      requireLength("ICHIMOKU", "outSenkouSpanA", outSenkouSpanA, guardOutLen);
      requireLength("ICHIMOKU", "outSenkouSpanB", outSenkouSpanB, guardOutLen);
      MInteger outBegIdx = new MInteger();
      MInteger outNBElement = new MInteger();
      RetCode retCode = ichimokuImpl(startIdx, endIdx, inHigh, inLow, optInTenkanPeriod, optInKijunPeriod, optInSenkouBPeriod, outBegIdx, outNBElement, outTenkanSen, outKijunSen, outSenkouSpanA, outSenkouSpanB);
      if( retCode != RetCode.SUCCESS ) {
         throw failure("ICHIMOKU", retCode);
      }
      return new OutRange(outBegIdx.value, outNBElement.value);
   }
