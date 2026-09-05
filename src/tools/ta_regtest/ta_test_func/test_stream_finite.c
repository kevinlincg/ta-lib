/* TA-LIB Copyright (c) 1999-2026, Mario Fortier
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 * - Neither name of author nor the names of its contributors
 *   may be used to endorse or promote products derived from this
 *   software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
 * OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE,
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* List of contributors:
 *
 *  Initial  Name/description
 *  -------------------------------------------------------------------
 *  MF       Mario Fortier
 *  CC       Claude Code (AI assistant)
 *
 * Change history:
 *
 *  081626 MF,CC  First version. The streaming tier's non-finite input
 *                rejection.
 *  083026 MF,CC  Rule U3 asserted absolutely, not as a tier equivalence.
 */

/* Description:
 *
 * Non-finite rejection is a property of SINGLE VALUES, never of input arrays.
 *
 * An input ARRAY is never scanned, in either tier: keeping one free of NaN and
 * +/-Inf is the caller's responsibility, and passing a non-finite one is
 * undefined behaviour (docs/error-handling-spec.md, "Non-finite input"). A scan
 * is a whole extra pass over caller memory the main loop is about to walk again,
 * and folding it into that loop instead would buy a worse contract: a rejection
 * partway through a fill, output half written.
 *
 * A SINGLE VALUE is always checked, because it is one comparison, and it matters
 * more in the streaming tier because a handle RETAINS state. Batch is handed a
 * series, computes and forgets, so a NaN reaches only the outputs depending on
 * that bar; a handle carries recursive accumulators, and one non-finite bar
 * poisons every value it will ever produce, long after the feed recovers.
 *
 * What this pins, per function:
 *
 *   (a) Update and Peek reject a non-finite bar value in ANY input slot with
 *       TA_BAD_PARAM.
 *   (b) The handle is UNCHANGED by a rejected call -- what makes the rejection
 *       useful rather than merely safe. Two streams opened on the same history,
 *       one offered the bad bar first, must agree BIT FOR BIT on the next good
 *       bar; a rejection that half-advanced the state passes (a) and fails here.
 *   (c) A real optional parameter that is NaN is rejected too, which is NOT
 *       redundant with the batch range check: `NaN < min` and `NaN > max` are
 *       both false, so a plain range test admits NaN. The streaming tier spells
 *       the same two comparisons inverted, `!(x >= min && x <= max)`.
 *   (d) The numbers themselves. (b) is an EQUIVALENCE, so it cannot see a change
 *       that moves both sides. (d) offers one bad bar to one handle and demands
 *       the exact range: BadParam, begIdx put, count exactly one higher, output
 *       untouched; then a good bar, which must still produce a value; and the
 *       mirror in Peek, which advances nothing either way.
 *
 * Coverage is by STREAM TIER, not by function count. The check is emitted from
 * one place per language, but into six different code paths in c_stream.rs, so
 * these seven functions are chosen to reach every one:
 *
 *   SMA       loop tier             (emit_update / emit_peek_from)
 *   MINUS_DI  dual-mode tier        (emit_peek_dual)
 *   MA        dispatch tier         (its own Update/Peek loop, and the identity
 *                                    arm that never reaches a sub-stream)
 *   MAVP      period-bank tier      (its own Update/Peek)
 *   BBANDS    composed tier         (its own inline Peek; also the real
 *                                    optional parameters for (c))
 *   STOCH     composed, multi-output, sub-feeding-sub
 *   CDLDOJI   integer output, four price inputs
 *
 * The equivalent per-language checks live in each binding's own suite:
 * StreamApiTest (C#), StreamSmokeTest (Java), and the crate's stream_finite
 * tests (Rust).
 */

#include <stdio.h>
#include <math.h>
#include <string.h>

#include "ta_test_priv.h"
#include "ta_test_func.h"
#include "ta_libc.h"

#define SF_BARS 120        /* history; comfortably past every lookback here */
#define SF_NBAD 3          /* NaN, +Inf, -Inf */

/* Counters. Incremented AT each assertion, never derived from a loop bound:
 * a count computed from the trip count stays healthy while the assertions
 * inside are deleted. */
static int sfBarRejects;    /* (a) */
static int sfStateHolds;    /* (b) */
static int sfParamRejects;  /* (c) */

static double sfOpen[SF_BARS], sfHigh[SF_BARS], sfLow[SF_BARS], sfClose[SF_BARS];

static const double sfBad[SF_NBAD] = { (double)NAN, (double)INFINITY, -(double)INFINITY };

/* A gently drifting OHLC series. Values are irrelevant to what is asserted --
 * only that every function here produces output on it. */
static void sf_build_series( void )
{
   int i;
   for( i = 0; i < SF_BARS; i++ )
   {
      double base = 100.0 + 8.0 * sin( i * 0.11 ) + 0.03 * i;
      sfOpen[i]  = base;
      sfHigh[i]  = base + 1.25 + 0.5 * cos( i * 0.37 );
      sfLow[i]   = base - 1.25 - 0.5 * cos( i * 0.23 );
      sfClose[i] = base + 0.4 * sin( i * 0.71 );
      if( sfHigh[i] < sfOpen[i] )  sfHigh[i] = sfOpen[i];
      if( sfLow[i]  > sfClose[i] ) sfLow[i]  = sfClose[i];
   }
}

#define SF_BAR_MUST_REJECT( fname, what, rc )                                 \
   do {                                                                       \
      if( (rc) != TA_BAD_PARAM )                                              \
      {                                                                       \
         printf( "  %s: %s accepted a non-finite bar (retCode %d)\n",         \
                 fname, what, (int)(rc) );                                    \
         return TA_STREAM_FINITE_BAR_ACCEPTED;                                \
      }                                                                       \
      sfBarRejects++;                                                         \
   } while( 0 )

#define SF_STATE_MUST_HOLD( fname, a, b )                                     \
   do {                                                                       \
      if( memcmp( &(a), &(b), sizeof(a) ) != 0 )                              \
      {                                                                       \
         printf( "  %s: a rejected bar moved the handle (%.17g vs %.17g)\n",  \
                 fname, (double)(a), (double)(b) );                           \
         return TA_STREAM_FINITE_STATE_MOVED;                                 \
      }                                                                       \
      sfStateHolds++;                                                         \
   } while( 0 )

#define SF_PARAM_MUST_REJECT( fname, rc )                                     \
   do {                                                                       \
      if( (rc) != TA_BAD_PARAM )                                              \
      {                                                                       \
         printf( "  %s: open accepted a NaN real parameter (retCode %d)\n",   \
                 fname, (int)(rc) );                                          \
         return TA_STREAM_FINITE_PARAM_ACCEPTED;                              \
      }                                                                       \
      sfParamRejects++;                                                       \
   } while( 0 )

/* ---- SMA: loop tier, one real input, one output ------------------------- */
static ErrorNumber sf_sma( void )
{
   int b, warm = 40;

   for( b = 0; b < SF_NBAD; b++ )
   {
      {
         TA_SMA_Stream *sa = NULL, *sb = NULL;
         double va = 0.0, vb = 0.0;
         if( TA_SMA_Open( &sa, sfClose, warm, 10, &va ) != TA_SUCCESS ||
             TA_SMA_Open( &sb, sfClose, warm, 10, &vb ) != TA_SUCCESS )
            return TA_STREAM_FINITE_SETUP_FAILED;
         SF_BAR_MUST_REJECT( "SMA", "update", TA_SMA_Update( sa, sfBad[b], &va ) );
         SF_BAR_MUST_REJECT( "SMA", "peek",   TA_SMA_Peek( sa, sfBad[b], &va ) );
         /* Same good bar on both; only `sa` was offered the bad one. */
         TA_SMA_Update( sa, sfClose[warm], &va );
         TA_SMA_Update( sb, sfClose[warm], &vb );
         SF_STATE_MUST_HOLD( "SMA", va, vb );
         TA_SMA_Close( sa );
         TA_SMA_Close( sb );
      }
   }
   return TA_TEST_PASS;
}

/* ---- MINUS_DI: dual-mode tier, three price inputs ----------------------- */
static ErrorNumber sf_minus_di( void )
{
   int b, warm = 40;

   for( b = 0; b < SF_NBAD; b++ )
   {
      {
         TA_MINUS_DI_Stream *sa = NULL, *sb = NULL;
         double va = 0.0, vb = 0.0;
         if( TA_MINUS_DI_Open( &sa, sfHigh, sfLow, sfClose, warm, 14, &va ) != TA_SUCCESS ||
             TA_MINUS_DI_Open( &sb, sfHigh, sfLow, sfClose, warm, 14, &vb ) != TA_SUCCESS )
            return TA_STREAM_FINITE_SETUP_FAILED;
         /* One slot at a time, so a check that only looked at the first input
          * cannot pass. */
         SF_BAR_MUST_REJECT( "MINUS_DI", "update(high)",
            TA_MINUS_DI_Update( sa, sfBad[b], sfLow[warm], sfClose[warm], &va ) );
         SF_BAR_MUST_REJECT( "MINUS_DI", "update(low)",
            TA_MINUS_DI_Update( sa, sfHigh[warm], sfBad[b], sfClose[warm], &va ) );
         SF_BAR_MUST_REJECT( "MINUS_DI", "update(close)",
            TA_MINUS_DI_Update( sa, sfHigh[warm], sfLow[warm], sfBad[b], &va ) );
         SF_BAR_MUST_REJECT( "MINUS_DI", "peek(high)",
            TA_MINUS_DI_Peek( sa, sfBad[b], sfLow[warm], sfClose[warm], &va ) );
         TA_MINUS_DI_Update( sa, sfHigh[warm], sfLow[warm], sfClose[warm], &va );
         TA_MINUS_DI_Update( sb, sfHigh[warm], sfLow[warm], sfClose[warm], &vb );
         SF_STATE_MUST_HOLD( "MINUS_DI", va, vb );
         TA_MINUS_DI_Close( sa );
         TA_MINUS_DI_Close( sb );
      }
   }
   return TA_TEST_PASS;
}

/* ---- MA: dispatch tier, including the identity (period 1) arm ----------- */
static ErrorNumber sf_ma( void )
{
   int b, warm = 40;

   for( b = 0; b < SF_NBAD; b++ )
   {

      {
         TA_MA_Stream *sa = NULL, *sb = NULL;
         double va = 0.0, vb = 0.0;
         if( TA_MA_Open( &sa, sfClose, warm, 10, TA_MAType_EMA, &va ) != TA_SUCCESS ||
             TA_MA_Open( &sb, sfClose, warm, 10, TA_MAType_EMA, &vb ) != TA_SUCCESS )
            return TA_STREAM_FINITE_SETUP_FAILED;
         SF_BAR_MUST_REJECT( "MA", "update", TA_MA_Update( sa, sfBad[b], &va ) );
         SF_BAR_MUST_REJECT( "MA", "peek",   TA_MA_Peek( sa, sfBad[b], &va ) );
         TA_MA_Update( sa, sfClose[warm], &va );
         TA_MA_Update( sb, sfClose[warm], &vb );
         SF_STATE_MUST_HOLD( "MA", va, vb );
         TA_MA_Close( sa );
         TA_MA_Close( sb );
      }
      {
         /* Period 1 is the identity arm: it copies the bar straight to the
          * output and never reaches a sub-stream, so a check delegated to the
          * sub would miss it entirely. */
         TA_MA_Stream *si = NULL;
         double vi = 0.0;
         if( TA_MA_Open( &si, sfClose, warm, 1, TA_MAType_SMA, &vi ) != TA_SUCCESS )
            return TA_STREAM_FINITE_SETUP_FAILED;
         SF_BAR_MUST_REJECT( "MA(identity)", "update", TA_MA_Update( si, sfBad[b], &vi ) );
         SF_BAR_MUST_REJECT( "MA(identity)", "peek",   TA_MA_Peek( si, sfBad[b], &vi ) );
         TA_MA_Close( si );
      }
   }
   return TA_TEST_PASS;
}

/* ---- MAVP: period-bank tier, two input series ---------------------------- */
static ErrorNumber sf_mavp( void )
{
   int b, i, warm = 40;
   static double periods[SF_BARS];

   for( i = 0; i < SF_BARS; i++ )
      periods[i] = 5.0 + (double)( i % 11 );

   for( b = 0; b < SF_NBAD; b++ )
   {

      {
         TA_MAVP_Stream *sa = NULL, *sb = NULL;
         double va = 0.0, vb = 0.0;
         if( TA_MAVP_Open( &sa, sfClose, periods, warm, 2, 30, TA_MAType_SMA, &va ) != TA_SUCCESS ||
             TA_MAVP_Open( &sb, sfClose, periods, warm, 2, 30, TA_MAType_SMA, &vb ) != TA_SUCCESS )
            return TA_STREAM_FINITE_SETUP_FAILED;
         SF_BAR_MUST_REJECT( "MAVP", "update(real)",
            TA_MAVP_Update( sa, sfBad[b], periods[warm], &va ) );
         /* The one that matters most here: converting a non-finite double to
          * int is undefined behaviour, and this is the only streaming input
          * that reaches such a conversion. */
         SF_BAR_MUST_REJECT( "MAVP", "update(period)",
            TA_MAVP_Update( sa, sfClose[warm], sfBad[b], &va ) );
         SF_BAR_MUST_REJECT( "MAVP", "peek(period)",
            TA_MAVP_Peek( sa, sfClose[warm], sfBad[b], &va ) );
         TA_MAVP_Update( sa, sfClose[warm], periods[warm], &va );
         TA_MAVP_Update( sb, sfClose[warm], periods[warm], &vb );
         SF_STATE_MUST_HOLD( "MAVP", va, vb );
         TA_MAVP_Close( sa );
         TA_MAVP_Close( sb );
      }
   }
   return TA_TEST_PASS;
}

/* ---- BBANDS: composed tier, three outputs, real optional params --------- */
static ErrorNumber sf_bbands( void )
{
   int b, warm = 40;
   double d0 = 0.0, d1 = 0.0, d2 = 0.0;

   for( b = 0; b < SF_NBAD; b++ )
   {

      {
         TA_BBANDS_Stream *sa = NULL, *sb = NULL;
         double a0 = 0.0, a1 = 0.0, a2 = 0.0, b0 = 0.0, b1 = 0.0, b2 = 0.0;
         if( TA_BBANDS_Open( &sa, sfClose, warm, 20, 2.0, 2.0, TA_MAType_SMA, &a0, &a1, &a2 ) != TA_SUCCESS ||
             TA_BBANDS_Open( &sb, sfClose, warm, 20, 2.0, 2.0, TA_MAType_SMA, &b0, &b1, &b2 ) != TA_SUCCESS )
            return TA_STREAM_FINITE_SETUP_FAILED;
         SF_BAR_MUST_REJECT( "BBANDS", "update", TA_BBANDS_Update( sa, sfBad[b], &a0, &a1, &a2 ) );
         SF_BAR_MUST_REJECT( "BBANDS", "peek",   TA_BBANDS_Peek( sa, sfBad[b], &a0, &a1, &a2 ) );
         TA_BBANDS_Update( sa, sfClose[warm], &a0, &a1, &a2 );
         TA_BBANDS_Update( sb, sfClose[warm], &b0, &b1, &b2 );
         SF_STATE_MUST_HOLD( "BBANDS.upper",  a0, b0 );
         SF_STATE_MUST_HOLD( "BBANDS.middle", a1, b1 );
         SF_STATE_MUST_HOLD( "BBANDS.lower",  a2, b2 );
         TA_BBANDS_Close( sa );
         TA_BBANDS_Close( sb );
      }
   }

   /* (c) A NaN real parameter. The batch tier's `x < min || x > max` admits it
    * -- both comparisons are false for NaN -- so this is a genuine difference,
    * not a restatement of the range check. Only NaN is tested: an infinity is
    * already outside every declared bound and both spellings reject it. */
   {
      TA_BBANDS_Stream *st = NULL;
      SF_PARAM_MUST_REJECT( "BBANDS(nbDevUp)",
         TA_BBANDS_Open( &st, sfClose, SF_BARS, 20, sfBad[0], 2.0, TA_MAType_SMA, &d0, &d1, &d2 ) );
      if( st ) { TA_BBANDS_Close( st ); return TA_STREAM_FINITE_PARAM_ACCEPTED; }
      SF_PARAM_MUST_REJECT( "BBANDS(nbDevDn)",
         TA_BBANDS_Open( &st, sfClose, SF_BARS, 20, 2.0, sfBad[0], TA_MAType_SMA, &d0, &d1, &d2 ) );
      if( st ) { TA_BBANDS_Close( st ); return TA_STREAM_FINITE_PARAM_ACCEPTED; }
   }
   return TA_TEST_PASS;
}

/* ---- STOCH: composed, multi-output, one sub feeding the next ------------ */
static ErrorNumber sf_stoch( void )
{
   int b, warm = 60;

   for( b = 0; b < SF_NBAD; b++ )
   {

      {
         TA_STOCH_Stream *sa = NULL, *sb = NULL;
         double a0 = 0.0, a1 = 0.0, b0 = 0.0, b1 = 0.0;
         if( TA_STOCH_Open( &sa, sfHigh, sfLow, sfClose, warm, 5, 3, TA_MAType_SMA, 3, TA_MAType_SMA, &a0, &a1 ) != TA_SUCCESS ||
             TA_STOCH_Open( &sb, sfHigh, sfLow, sfClose, warm, 5, 3, TA_MAType_SMA, 3, TA_MAType_SMA, &b0, &b1 ) != TA_SUCCESS )
            return TA_STREAM_FINITE_SETUP_FAILED;
         SF_BAR_MUST_REJECT( "STOCH", "update",
            TA_STOCH_Update( sa, sfBad[b], sfLow[warm], sfClose[warm], &a0, &a1 ) );
         SF_BAR_MUST_REJECT( "STOCH", "peek",
            TA_STOCH_Peek( sa, sfHigh[warm], sfBad[b], sfClose[warm], &a0, &a1 ) );
         TA_STOCH_Update( sa, sfHigh[warm], sfLow[warm], sfClose[warm], &a0, &a1 );
         TA_STOCH_Update( sb, sfHigh[warm], sfLow[warm], sfClose[warm], &b0, &b1 );
         SF_STATE_MUST_HOLD( "STOCH.slowK", a0, b0 );
         SF_STATE_MUST_HOLD( "STOCH.slowD", a1, b1 );
         TA_STOCH_Close( sa );
         TA_STOCH_Close( sb );
      }
   }
   return TA_TEST_PASS;
}

/* ---- CDLDOJI: integer output, four price inputs ------------------------- */
static ErrorNumber sf_cdldoji( void )
{
   int b, warm = 40;

   for( b = 0; b < SF_NBAD; b++ )
   {
      {
         TA_CDLDOJI_Stream *sa = NULL, *sb = NULL;
         int ia = 0, ib = 0;
         if( TA_CDLDOJI_Open( &sa, sfOpen, sfHigh, sfLow, sfClose, warm, &ia ) != TA_SUCCESS ||
             TA_CDLDOJI_Open( &sb, sfOpen, sfHigh, sfLow, sfClose, warm, &ib ) != TA_SUCCESS )
            return TA_STREAM_FINITE_SETUP_FAILED;
         SF_BAR_MUST_REJECT( "CDLDOJI", "update(open)",
            TA_CDLDOJI_Update( sa, sfBad[b], sfHigh[warm], sfLow[warm], sfClose[warm], &ia ) );
         SF_BAR_MUST_REJECT( "CDLDOJI", "peek(close)",
            TA_CDLDOJI_Peek( sa, sfOpen[warm], sfHigh[warm], sfLow[warm], sfBad[b], &ia ) );
         TA_CDLDOJI_Update( sa, sfOpen[warm], sfHigh[warm], sfLow[warm], sfClose[warm], &ia );
         TA_CDLDOJI_Update( sb, sfOpen[warm], sfHigh[warm], sfLow[warm], sfClose[warm], &ib );
         if( ia != ib )
         {
            printf( "  CDLDOJI: a rejected bar moved the handle (%d vs %d)\n", ia, ib );
            return TA_STREAM_FINITE_STATE_MOVED;
         }
         sfStateHolds++;
         TA_CDLDOJI_Close( sa );
         TA_CDLDOJI_Close( sb );
      }
   }
   return TA_TEST_PASS;
}

/* ---- (d) rule U3 stated ABSOLUTELY: what ONE rejected Update costs ------ */
/*
 * Everything above compares one tier against another: (b) holds two handles
 * side by side. That equivalence is SYMMETRIC, so it cannot see a change that
 * moves both sides equally -- deleting the advance from the emitted reject arm
 * moves BOTH handles at once and leaves the whole suite green in every
 * language. The rule then
 * rests on the generator's source-text gate alone, and nothing running proves
 * it.
 *
 * So this leg compares against no control at all. It reads the range, offers
 * exactly one bad bar, and demands the exact numbers: TA_BAD_PARAM, begIdx
 * unmoved, count exactly one higher -- the bar happened, so it is counted --
 * and the caller's output slot untouched.
 *
 * Both halves are asserted on the SAME call, deliberately. A change that
 * stepped the state and skipped the count, or counted and stepped, satisfies
 * either half alone; only the pair pins "counted but not committed".
 *
 * Then a good bar, which must still produce a value and advance by one:
 * refusing a bar is better than computing on it only if the handle survives
 * the refusal.
 *
 * The mirror is Peek, which advances NOTHING -- rejected or not. It is the
 * half most likely to regress silently, because a Peek that started counting
 * would break no value anywhere.
 */

/* Counters, one per property, each incremented AT its assertion. */
static int sfAdvRejects;    /* a rejected Update: BadParam and exactly +1 */
static int sfAdvHolds;      /* a slot the rejected call left alone */
static int sfAdvResumes;    /* the next good bar: Success and exactly +1 */
static int sfAdvValues;     /* a slot that good bar filled */
static int sfAdvPeekStills; /* a Peek, good or rejected, moved nothing */
static int sfAdvValueHolds; /* Value across the rejection: same bits */
static int sfAdvValueTracks;/* Value after the next good bar: that bar's value */

/* An output slot is seeded with this, never with zero: a rejected call that
 * left the slot alone and one that wrote a plausible zero are the same reading
 * against a zero seed. */
#define SF_ADV_CANARY   (-1.2345678901234e300)
#define SF_ADV_CANARY_I (-987654321)

#define SF_ADV_READ( fname, h, bv, nv )                                       \
   do {                                                                       \
      if( TA_StreamOutRange( (h), &(bv), &(nv) ) != TA_SUCCESS )              \
      {                                                                       \
         printf( "  %s: TA_StreamOutRange failed\n", fname );                 \
         return TA_STREAM_ADVANCE_SETUP_FAILED;                               \
      }                                                                       \
   } while( 0 )

#define SF_ADV_REJECT( fname, h, call )                                       \
   do {                                                                       \
      int b0_, n0_, b1_, n1_;                                                 \
      TA_RetCode rc_;                                                         \
      SF_ADV_READ( fname, (h), b0_, n0_ );                                    \
      rc_ = (call);                                                           \
      if( rc_ != TA_BAD_PARAM )                                               \
      {                                                                       \
         printf( "  %s: Update accepted a non-finite bar (retCode %d)\n",     \
                 fname, (int)rc_ );                                           \
         return TA_STREAM_ADVANCE_NOT_REJECTED;                               \
      }                                                                       \
      SF_ADV_READ( fname, (h), b1_, n1_ );                                    \
      if( b1_ != b0_ || n1_ != n0_ + 1 )                                      \
      {                                                                       \
         printf( "  %s: a rejected Update left (%d,%d), expected (%d,%d)\n",  \
                 fname, b1_, n1_, b0_, n0_ + 1 );                             \
         return TA_STREAM_ADVANCE_WRONG_COUNT;                                \
      }                                                                       \
      sfAdvRejects++;                                                         \
   } while( 0 )

#define SF_ADV_RESUME( fname, h, call )                                       \
   do {                                                                       \
      int b0_, n0_, b1_, n1_;                                                 \
      TA_RetCode rc_;                                                         \
      SF_ADV_READ( fname, (h), b0_, n0_ );                                    \
      rc_ = (call);                                                           \
      if( rc_ != TA_SUCCESS )                                                 \
      {                                                                       \
         printf( "  %s: the good bar after a rejection failed (retCode %d)\n",\
                 fname, (int)rc_ );                                           \
         return TA_STREAM_ADVANCE_NOT_RESUMED;                                \
      }                                                                       \
      SF_ADV_READ( fname, (h), b1_, n1_ );                                    \
      if( b1_ != b0_ || n1_ != n0_ + 1 )                                      \
      {                                                                       \
         printf( "  %s: a committed Update left (%d,%d), expected (%d,%d)\n", \
                 fname, b1_, n1_, b0_, n0_ + 1 );                             \
         return TA_STREAM_ADVANCE_WRONG_COUNT;                                \
      }                                                                       \
      sfAdvResumes++;                                                         \
   } while( 0 )

/* Peek advances NOTHING -- rejected or not. The retCode is asserted in the
 * same macro so a Peek that silently accepted the bad bar cannot pass on
 * "it moved nothing". */
#define SF_ADV_PEEK( fname, h, call, want )                                   \
   do {                                                                       \
      int b0_, n0_, b1_, n1_;                                                 \
      TA_RetCode rc_;                                                         \
      SF_ADV_READ( fname, (h), b0_, n0_ );                                    \
      rc_ = (call);                                                           \
      if( rc_ != (want) )                                                     \
      {                                                                       \
         printf( "  %s: Peek answered %d, expected %d\n",                     \
                 fname, (int)rc_, (int)(want) );                              \
         return TA_STREAM_ADVANCE_NOT_REJECTED;                               \
      }                                                                       \
      SF_ADV_READ( fname, (h), b1_, n1_ );                                    \
      if( b1_ != b0_ || n1_ != n0_ )                                          \
      {                                                                       \
         printf( "  %s: Peek moved the range (%d,%d) -> (%d,%d)\n",           \
                 fname, b0_, n0_, b1_, n1_ );                                 \
         return TA_STREAM_ADVANCE_PEEK_MOVED;                                 \
      }                                                                       \
      sfAdvPeekStills++;                                                      \
   } while( 0 )

#define SF_ADV_HELD( fname, x )                                               \
   do {                                                                       \
      if( (x) != SF_ADV_CANARY )                                               \
      {                                                                       \
         printf( "  %s: a rejected call wrote %.17g into the output\n",       \
                 fname, (double)(x) );                                        \
         return TA_STREAM_ADVANCE_VALUE_MOVED;                                \
      }                                                                       \
      sfAdvHolds++;                                                           \
   } while( 0 )

#define SF_ADV_HELD_I( fname, x )                                             \
   do {                                                                       \
      if( (x) != SF_ADV_CANARY_I )                                             \
      {                                                                       \
         printf( "  %s: a rejected call wrote %d into the output\n",          \
                 fname, (int)(x) );                                           \
         return TA_STREAM_ADVANCE_VALUE_MOVED;                                \
      }                                                                       \
      sfAdvHolds++;                                                           \
   } while( 0 )

/* The handle is still usable: the good bar produced something, so the
 * "untouched" assertions above are not passing because the function stopped
 * writing at all. */
#define SF_ADV_PRODUCED( fname, x )                                           \
   do {                                                                       \
      if( (x) == SF_ADV_CANARY || !isfinite( (double)(x) ) )               \
      {                                                                       \
         printf( "  %s: no value after the rejected bar\n", fname );          \
         return TA_STREAM_ADVANCE_NO_VALUE;                                   \
      }                                                                       \
      sfAdvValues++;                                                          \
   } while( 0 )

/* SF_ADV_HELD proves only that the rejected call left the CALLER's
 * out-parameter alone -- the call convention. Value is the only path to the
 * held output, so it is read across the rejection too. Held bits alone would
 * pass for an accessor that answers a constant, hence the tracking half. */
#define SF_ADV_VALUE( fname, call )                                           \
   do {                                                                       \
      if( (call) != TA_SUCCESS )                                              \
      {                                                                       \
         printf( "  %s: Value failed\n", fname );                             \
         return TA_STREAM_ADVANCE_SETUP_FAILED;                               \
      }                                                                       \
   } while( 0 )

#define SF_ADV_VALUE_HELD( fname, pre, post )                                 \
   do {                                                                       \
      if( memcmp( &(pre), &(post), sizeof(pre) ) != 0 )                       \
      {                                                                       \
         printf( "  %s: Value answered %.17g after a rejected Update, "       \
                 "held %.17g\n", fname, (double)(post), (double)(pre) );      \
         return TA_STREAM_ADVANCE_VALUE_NOT_HELD;                             \
      }                                                                       \
      sfAdvValueHolds++;                                                      \
   } while( 0 )

#define SF_ADV_VALUE_TRACKS( fname, produced, post )                          \
   do {                                                                       \
      if( memcmp( &(produced), &(post), sizeof(produced) ) != 0 )             \
      {                                                                       \
         printf( "  %s: Value answered %.17g, the bar produced %.17g\n",      \
                 fname, (double)(post), (double)(produced) );                 \
         return TA_STREAM_ADVANCE_VALUE_NOT_HELD;                             \
      }                                                                       \
      sfAdvValueTracks++;                                                     \
   } while( 0 )

#define SF_ADV_VALUE_HELD_I( fname, pre, post )                               \
   do {                                                                       \
      if( (pre) != (post) )                                                   \
      {                                                                       \
         printf( "  %s: Value answered %d after a rejected Update, held %d\n",\
                 fname, (int)(post), (int)(pre) );                            \
         return TA_STREAM_ADVANCE_VALUE_NOT_HELD;                             \
      }                                                                       \
      sfAdvValueHolds++;                                                      \
   } while( 0 )

#define SF_ADV_VALUE_TRACKS_I( fname, produced, post )                        \
   do {                                                                       \
      if( (produced) != (post) )                                              \
      {                                                                       \
         printf( "  %s: Value answered %d, the bar produced %d\n",            \
                 fname, (int)(post), (int)(produced) );                       \
         return TA_STREAM_ADVANCE_VALUE_NOT_HELD;                             \
      }                                                                       \
      sfAdvValueTracks++;                                                     \
   } while( 0 )

#define SF_ADV_PRODUCED_I( fname, x )                                         \
   do {                                                                       \
      if( (x) == SF_ADV_CANARY_I )                                             \
      {                                                                       \
         printf( "  %s: no value after the rejected bar\n", fname );          \
         return TA_STREAM_ADVANCE_NO_VALUE;                                   \
      }                                                                       \
      sfAdvValues++;                                                          \
   } while( 0 )

static ErrorNumber sf_advance( void )
{
   int b, k, warm = 60;
   static double periods[SF_BARS];

   for( k = 0; k < SF_BARS; k++ )
      periods[k] = 5.0 + (double)( k % 11 );

   for( b = 0; b < SF_NBAD; b++ )
   {
      /* Loop tier. */
      {
         TA_SMA_Stream *s = NULL;
         double seed = 0.0, v = SF_ADV_CANARY;
         double vp = 0.0, vq = 0.0;
         if( TA_SMA_Open( &s, sfClose, warm, 10, &seed ) != TA_SUCCESS )
            return TA_STREAM_ADVANCE_SETUP_FAILED;
         SF_ADV_PEEK( "SMA(bad)", s, TA_SMA_Peek( s, sfBad[b], &v ), TA_BAD_PARAM );
         SF_ADV_HELD( "SMA(peek bad)", v );
         SF_ADV_PEEK( "SMA(good)", s, TA_SMA_Peek( s, sfClose[warm], &seed ), TA_SUCCESS );
         SF_ADV_VALUE( "SMA", TA_SMA_Value( s, &vp ) );
         SF_ADV_REJECT( "SMA", s, TA_SMA_Update( s, sfBad[b], &v ) );
         SF_ADV_HELD( "SMA", v );
         SF_ADV_VALUE( "SMA", TA_SMA_Value( s, &vq ) );
         SF_ADV_VALUE_HELD( "SMA", vp, vq );
         SF_ADV_RESUME( "SMA", s, TA_SMA_Update( s, sfClose[warm], &v ) );
         SF_ADV_PRODUCED( "SMA", v );
         SF_ADV_VALUE( "SMA", TA_SMA_Value( s, &vq ) );
         SF_ADV_VALUE_TRACKS( "SMA", v, vq );
         TA_SMA_Close( s );
      }
      /* Dual-mode tier, three price inputs. */
      {
         TA_MINUS_DI_Stream *s = NULL;
         double seed = 0.0, v = SF_ADV_CANARY;
         double vp = 0.0, vq = 0.0;
         if( TA_MINUS_DI_Open( &s, sfHigh, sfLow, sfClose, warm, 14, &seed ) != TA_SUCCESS )
            return TA_STREAM_ADVANCE_SETUP_FAILED;
         SF_ADV_PEEK( "MINUS_DI(bad)", s,
            TA_MINUS_DI_Peek( s, sfHigh[warm], sfBad[b], sfClose[warm], &v ), TA_BAD_PARAM );
         SF_ADV_HELD( "MINUS_DI(peek bad)", v );
         SF_ADV_PEEK( "MINUS_DI(good)", s,
            TA_MINUS_DI_Peek( s, sfHigh[warm], sfLow[warm], sfClose[warm], &seed ), TA_SUCCESS );
         SF_ADV_VALUE( "MINUS_DI", TA_MINUS_DI_Value( s, &vp ) );
         SF_ADV_REJECT( "MINUS_DI", s,
            TA_MINUS_DI_Update( s, sfHigh[warm], sfLow[warm], sfBad[b], &v ) );
         SF_ADV_HELD( "MINUS_DI", v );
         SF_ADV_VALUE( "MINUS_DI", TA_MINUS_DI_Value( s, &vq ) );
         SF_ADV_VALUE_HELD( "MINUS_DI", vp, vq );
         SF_ADV_RESUME( "MINUS_DI", s,
            TA_MINUS_DI_Update( s, sfHigh[warm], sfLow[warm], sfClose[warm], &v ) );
         SF_ADV_PRODUCED( "MINUS_DI", v );
         SF_ADV_VALUE( "MINUS_DI", TA_MINUS_DI_Value( s, &vq ) );
         SF_ADV_VALUE_TRACKS( "MINUS_DI", v, vq );
         TA_MINUS_DI_Close( s );
      }
      /* Dispatch tier, both arms: period 1 is the identity loop, which never
       * reaches a sub-stream and carries its own copy of the advance. */
      {
         int p;
         const int mp[2] = { 1, 10 };
         for( p = 0; p < 2; p++ )
         {
            TA_MA_Stream *s = NULL;
            double seed = 0.0, v = SF_ADV_CANARY;
            double vp = 0.0, vq = 0.0;
            if( TA_MA_Open( &s, sfClose, warm, mp[p], TA_MAType_SMA, &seed ) != TA_SUCCESS )
               return TA_STREAM_ADVANCE_SETUP_FAILED;
            SF_ADV_PEEK( "MA(bad)", s, TA_MA_Peek( s, sfBad[b], &v ), TA_BAD_PARAM );
            SF_ADV_HELD( "MA(peek bad)", v );
            SF_ADV_PEEK( "MA(good)", s, TA_MA_Peek( s, sfClose[warm], &seed ), TA_SUCCESS );
            SF_ADV_VALUE( "MA", TA_MA_Value( s, &vp ) );
            SF_ADV_REJECT( "MA", s, TA_MA_Update( s, sfBad[b], &v ) );
            SF_ADV_HELD( "MA", v );
            SF_ADV_VALUE( "MA", TA_MA_Value( s, &vq ) );
            SF_ADV_VALUE_HELD( "MA", vp, vq );
            SF_ADV_RESUME( "MA", s, TA_MA_Update( s, sfClose[warm], &v ) );
            SF_ADV_PRODUCED( "MA", v );
            SF_ADV_VALUE( "MA", TA_MA_Value( s, &vq ) );
            SF_ADV_VALUE_TRACKS( "MA", v, vq );
            TA_MA_Close( s );
         }
      }
      /* Period-bank tier. The poisoned slot is the PERIOD, the one that
       * reaches an (int) cast. */
      {
         TA_MAVP_Stream *s = NULL;
         double seed = 0.0, v = SF_ADV_CANARY;
         double vp = 0.0, vq = 0.0;
         if( TA_MAVP_Open( &s, sfClose, periods, warm, 2, 30, TA_MAType_SMA, &seed ) != TA_SUCCESS )
            return TA_STREAM_ADVANCE_SETUP_FAILED;
         SF_ADV_PEEK( "MAVP(bad)", s,
            TA_MAVP_Peek( s, sfClose[warm], sfBad[b], &v ), TA_BAD_PARAM );
         SF_ADV_HELD( "MAVP(peek bad)", v );
         SF_ADV_PEEK( "MAVP(good)", s,
            TA_MAVP_Peek( s, sfClose[warm], periods[warm], &seed ), TA_SUCCESS );
         SF_ADV_VALUE( "MAVP", TA_MAVP_Value( s, &vp ) );
         SF_ADV_REJECT( "MAVP", s, TA_MAVP_Update( s, sfClose[warm], sfBad[b], &v ) );
         SF_ADV_HELD( "MAVP", v );
         SF_ADV_VALUE( "MAVP", TA_MAVP_Value( s, &vq ) );
         SF_ADV_VALUE_HELD( "MAVP", vp, vq );
         SF_ADV_RESUME( "MAVP", s, TA_MAVP_Update( s, sfClose[warm], periods[warm], &v ) );
         SF_ADV_PRODUCED( "MAVP", v );
         SF_ADV_VALUE( "MAVP", TA_MAVP_Value( s, &vq ) );
         SF_ADV_VALUE_TRACKS( "MAVP", v, vq );
         TA_MAVP_Close( s );
      }
      /* Composed tier, three outputs: the rejection must leave all three. */
      {
         TA_BBANDS_Stream *s = NULL;
         double s0 = 0.0, s1 = 0.0, s2 = 0.0;
         double u = SF_ADV_CANARY, m = SF_ADV_CANARY, l = SF_ADV_CANARY;
         double up = 0.0, mp = 0.0, lp = 0.0, uq = 0.0, mq = 0.0, lq = 0.0;
         if( TA_BBANDS_Open( &s, sfClose, warm, 20, 2.0, 2.0, TA_MAType_SMA, &s0, &s1, &s2 ) != TA_SUCCESS )
            return TA_STREAM_ADVANCE_SETUP_FAILED;
         SF_ADV_PEEK( "BBANDS(bad)", s,
            TA_BBANDS_Peek( s, sfBad[b], &u, &m, &l ), TA_BAD_PARAM );
         SF_ADV_HELD( "BBANDS.upper(peek bad)",  u );
         SF_ADV_HELD( "BBANDS.middle(peek bad)", m );
         SF_ADV_HELD( "BBANDS.lower(peek bad)",  l );
         SF_ADV_PEEK( "BBANDS(good)", s,
            TA_BBANDS_Peek( s, sfClose[warm], &s0, &s1, &s2 ), TA_SUCCESS );
         SF_ADV_VALUE( "BBANDS", TA_BBANDS_Value( s, &up, &mp, &lp ) );
         SF_ADV_REJECT( "BBANDS", s, TA_BBANDS_Update( s, sfBad[b], &u, &m, &l ) );
         SF_ADV_HELD( "BBANDS.upper",  u );
         SF_ADV_HELD( "BBANDS.middle", m );
         SF_ADV_HELD( "BBANDS.lower",  l );
         SF_ADV_VALUE( "BBANDS", TA_BBANDS_Value( s, &uq, &mq, &lq ) );
         SF_ADV_VALUE_HELD( "BBANDS.upper",  up, uq );
         SF_ADV_VALUE_HELD( "BBANDS.middle", mp, mq );
         SF_ADV_VALUE_HELD( "BBANDS.lower",  lp, lq );
         SF_ADV_RESUME( "BBANDS", s, TA_BBANDS_Update( s, sfClose[warm], &u, &m, &l ) );
         SF_ADV_PRODUCED( "BBANDS.upper",  u );
         SF_ADV_PRODUCED( "BBANDS.middle", m );
         SF_ADV_PRODUCED( "BBANDS.lower",  l );
         SF_ADV_VALUE( "BBANDS", TA_BBANDS_Value( s, &uq, &mq, &lq ) );
         SF_ADV_VALUE_TRACKS( "BBANDS.upper",  u, uq );
         SF_ADV_VALUE_TRACKS( "BBANDS.middle", m, mq );
         SF_ADV_VALUE_TRACKS( "BBANDS.lower",  l, lq );
         TA_BBANDS_Close( s );
      }
      /* Composed, multi-output, one sub feeding the next. */
      {
         TA_STOCH_Stream *s = NULL;
         double s0 = 0.0, s1 = 0.0;
         double kv = SF_ADV_CANARY, dv = SF_ADV_CANARY;
         double kp = 0.0, dp = 0.0, kq = 0.0, dq = 0.0;
         if( TA_STOCH_Open( &s, sfHigh, sfLow, sfClose, warm, 5, 3, TA_MAType_SMA, 3, TA_MAType_SMA, &s0, &s1 ) != TA_SUCCESS )
            return TA_STREAM_ADVANCE_SETUP_FAILED;
         SF_ADV_PEEK( "STOCH(bad)", s,
            TA_STOCH_Peek( s, sfHigh[warm], sfBad[b], sfClose[warm], &kv, &dv ), TA_BAD_PARAM );
         SF_ADV_HELD( "STOCH.slowK(peek bad)", kv );
         SF_ADV_HELD( "STOCH.slowD(peek bad)", dv );
         SF_ADV_PEEK( "STOCH(good)", s,
            TA_STOCH_Peek( s, sfHigh[warm], sfLow[warm], sfClose[warm], &s0, &s1 ), TA_SUCCESS );
         SF_ADV_VALUE( "STOCH", TA_STOCH_Value( s, &kp, &dp ) );
         SF_ADV_REJECT( "STOCH", s,
            TA_STOCH_Update( s, sfBad[b], sfLow[warm], sfClose[warm], &kv, &dv ) );
         SF_ADV_HELD( "STOCH.slowK", kv );
         SF_ADV_HELD( "STOCH.slowD", dv );
         SF_ADV_VALUE( "STOCH", TA_STOCH_Value( s, &kq, &dq ) );
         SF_ADV_VALUE_HELD( "STOCH.slowK", kp, kq );
         SF_ADV_VALUE_HELD( "STOCH.slowD", dp, dq );
         SF_ADV_RESUME( "STOCH", s,
            TA_STOCH_Update( s, sfHigh[warm], sfLow[warm], sfClose[warm], &kv, &dv ) );
         SF_ADV_PRODUCED( "STOCH.slowK", kv );
         SF_ADV_PRODUCED( "STOCH.slowD", dv );
         SF_ADV_VALUE( "STOCH", TA_STOCH_Value( s, &kq, &dq ) );
         SF_ADV_VALUE_TRACKS( "STOCH.slowK", kv, kq );
         SF_ADV_VALUE_TRACKS( "STOCH.slowD", dv, dq );
         TA_STOCH_Close( s );
      }
      /* Integer output over four price inputs. */
      {
         TA_CDLDOJI_Stream *s = NULL;
         int seed = 0, v = SF_ADV_CANARY_I;
         int vp = 0, vq = 0;
         if( TA_CDLDOJI_Open( &s, sfOpen, sfHigh, sfLow, sfClose, warm, &seed ) != TA_SUCCESS )
            return TA_STREAM_ADVANCE_SETUP_FAILED;
         SF_ADV_PEEK( "CDLDOJI(bad)", s,
            TA_CDLDOJI_Peek( s, sfOpen[warm], sfHigh[warm], sfLow[warm], sfBad[b], &v ), TA_BAD_PARAM );
         SF_ADV_HELD_I( "CDLDOJI(peek bad)", v );
         SF_ADV_PEEK( "CDLDOJI(good)", s,
            TA_CDLDOJI_Peek( s, sfOpen[warm], sfHigh[warm], sfLow[warm], sfClose[warm], &seed ), TA_SUCCESS );
         SF_ADV_VALUE( "CDLDOJI", TA_CDLDOJI_Value( s, &vp ) );
         SF_ADV_REJECT( "CDLDOJI", s,
            TA_CDLDOJI_Update( s, sfOpen[warm], sfHigh[warm], sfBad[b], sfClose[warm], &v ) );
         SF_ADV_HELD_I( "CDLDOJI", v );
         SF_ADV_VALUE( "CDLDOJI", TA_CDLDOJI_Value( s, &vq ) );
         SF_ADV_VALUE_HELD_I( "CDLDOJI", vp, vq );
         /* A doji, so the good bar's value is nonzero and distinguishable from
          * "the slot was never written". */
         SF_ADV_RESUME( "CDLDOJI", s,
            TA_CDLDOJI_Update( s, sfClose[warm], sfHigh[warm], sfLow[warm], sfClose[warm], &v ) );
         SF_ADV_PRODUCED_I( "CDLDOJI", v );
         SF_ADV_VALUE( "CDLDOJI", TA_CDLDOJI_Value( s, &vq ) );
         SF_ADV_VALUE_TRACKS_I( "CDLDOJI", v, vq );
         TA_CDLDOJI_Close( s );
      }
   }
   return TA_TEST_PASS;
}

ErrorNumber test_func_stream_finite( TA_History *history )
{
   ErrorNumber errNb;

   /* The reference history is unused: this needs a series long enough for
    * every tier's warm-up, and values it can poison in place. */
   (void)history;

   sf_build_series();
   sfBarRejects = sfStateHolds = sfParamRejects = 0;
   sfAdvRejects = sfAdvHolds = sfAdvResumes = sfAdvValues = sfAdvPeekStills = 0;
   sfAdvValueHolds = sfAdvValueTracks = 0;

   if( ( errNb = sf_sma()       ) != TA_TEST_PASS ) return errNb;
   if( ( errNb = sf_minus_di()  ) != TA_TEST_PASS ) return errNb;
   if( ( errNb = sf_ma()        ) != TA_TEST_PASS ) return errNb;
   if( ( errNb = sf_mavp()      ) != TA_TEST_PASS ) return errNb;
   if( ( errNb = sf_bbands()    ) != TA_TEST_PASS ) return errNb;
   if( ( errNb = sf_stoch()     ) != TA_TEST_PASS ) return errNb;
   if( ( errNb = sf_cdldoji()   ) != TA_TEST_PASS ) return errNb;

   if( ( errNb = sf_advance()    ) != TA_TEST_PASS ) return errNb;

   /* Non-vacuity. The floors are literal, not derived from the loops above: a
    * count computed from the trip count moves with it, and would let half the
    * assertions be deleted while still "passing its floor". */
   if( sfBarRejects < 57 || sfStateHolds < 30 || sfParamRejects < 2 )
   {
      printf( "  Failed: the gate ran fewer checks than it was written with\n" );
      return TA_STREAM_FINITE_VACUOUS;
   }
   if( sfAdvRejects < 24 || sfAdvHolds < 66 || sfAdvResumes < 24 ||
       sfAdvValues < 33 || sfAdvPeekStills < 48 ||
       sfAdvValueHolds < 33 || sfAdvValueTracks < 33 )
   {
      printf( "  Failed: the rejected-Update advance gate ran fewer checks "
              "than it was written with\n" );
      return TA_STREAM_ADVANCE_VACUOUS;
   }

   return TA_TEST_PASS;
}
