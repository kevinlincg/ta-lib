#!/usr/bin/env python3
"""Generate candidate `<name>.c` bodies for the #147 algorithm evaluation.

Candidates
  C0  baseline: cached extremum + rescan (upstream, reference)
  C1  monotonic deque, malloc'd, capacity = optInTimePeriod
  C2  Van Herk / Gil-Werman, BLOCK-BATCHED form
  C3  Van Herk / Gil-Werman, PER-SAMPLE form
  C4  two-stack queue
  C5  baseline with the rescan tie-break flipped to `>=` / `<=` (scalar only)

Every O(period)-scratch candidate (C1..C4) is emitted in the *fast-path-skip*
form recognised by `streaming::analyze_fastpath_skip`:

    <prologue>
    if( optInTimePeriod <= 100000 )   /* == the whole legal range */
    {
        <new algorithm>               /* batch-only specialisation */
    }
    else
    {
        <baseline automaton>          /* the arm the STREAM transcribes */
    }
    <epilogue>

ta_codegen's streaming synthesizer derives `TA_S_*` by transcribing the batch
body and rejects non-scalar loop state, so a malloc'd deque makes `generate`
exit 1.  The `<= <literal>` predicate is the one shape the generator treats as a
bit-identical batch-only split: only the else arm is streamed.  Both arms are
exact selections over the same window, so they are bit-identical and the stream
stays bit-exact with the batch.  See PROGRESS.md.

Two portability rules, both learned from generator misbehaviour:
  * no int index may go negative.  `if( k < 0 ) k += period;` makes the C->Rust
    translator emit `k += (optInTimePeriod as f64)` (i32 += f64, does not
    compile) and `(j - 1) as i32` underflows a usize-inferred `j`.  All wrap
    arithmetic uses `+ cap - 1` / `>= cap` instead, and backward scans stop at
    `> lowerBound` rather than testing `>= lowerBound`.
  * scratch holds VALUES, never bare indices into the input: input and output
    may alias (#130), so nothing may re-read `inReal[i]` for an `i` already
    written.
"""
import os

HDR_MAX = """/* List of contributors:
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
"""

HDR_MIDPOINT = """/* List of contributors:
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
"""


def ind(body, n):
    pad = " " * n
    return "\n".join(pad + l if l.strip() else l for l in body.strip("\n").split("\n"))


# ===========================================================================
# per-function description
# ===========================================================================
#
# An "extremum channel" is one (array, direction) pair.  MAX has one (inReal,
# max); MIDPOINT has two over the SAME array (inReal, max) and (inReal, min);
# MIDPRICE/WILLR/STOCH have two over DIFFERENT arrays.
#
# ch = (tag, array, better, tiebreak_incoming)
#   better            : ">"  for a maximum channel, "<"  for a minimum channel
#   tiebreak_incoming : ">=" / "<=" (the batch's `else if` arm)


class Func:
    def __init__(self, name, hdr, sig, decls_extra, channels, emit_out, lookback):
        self.name = name
        self.hdr = hdr
        self.sig = sig
        self.decls_extra = decls_extra
        self.channels = channels          # list of dicts
        self.emit_out = emit_out          # C expression written to outReal
        self.lookback = lookback


MAXF = Func(
    name="max",
    hdr=HDR_MAX,
    sig="""TA_RetCode max(int startIdx, int endIdx,
   const double inReal[],
   int optInTimePeriod,
   int *outBegIdx, int *outNBElement,
   double outReal[])""",
    decls_extra="",
    channels=[dict(tag="highest", arr="inReal", val="highest", idx="highestIdx",
                   tmp="tmp", better=">", incoming=">=", pop="<=", init="0.0")],
    emit_out="highest",
    lookback="""int max_lookback(int optInTimePeriod)
{
   return (optInTimePeriod-1);
}""",
)

MIDPOINTF = Func(
    name="midpoint",
    hdr=HDR_MIDPOINT,
    sig="""TA_RetCode midpoint(int startIdx, int endIdx,
   const double inReal[],
   int optInTimePeriod,
   int *outBegIdx, int *outNBElement,
   double outReal[])""",
    decls_extra="",
    channels=[dict(tag="highest", arr="inReal", val="highest", idx="highestIdx",
                   tmp="tmpHigh", better=">", incoming=">=", pop="<=", init="0.0"),
              dict(tag="lowest", arr="inReal", val="lowest", idx="lowestIdx",
                   tmp="tmpLow", better="<", incoming="<=", pop=">=", init="0.0")],
    emit_out="(highest+lowest)/2.0",
    lookback="""int midpoint_lookback(int optInTimePeriod)
{
   return (optInTimePeriod-1);
}""",
)

FUNCS = {"max": MAXF, "midpoint": MIDPOINTF}

PROLOGUE = """
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
"""

EPILOGUE = """
   /* Keep the outBegIdx relative to the
    * caller input before returning.
    */
   *outBegIdx    = startIdx;
   *outNBElement = outIdx;

   return TA_SUCCESS;
}
"""


# ---------------------------------------------------------------------------
# C0 / C5 : the baseline automaton (tie = ">" upstream, ">=" for C5)
# ---------------------------------------------------------------------------
def automaton(f, rescan_tie_strict=True):
    init = "\n".join(
        f"{c['idx']}  = -1;\n{c['val']}     = {c['init']};" for c in f.channels)
    reads = "\n".join(f"{c['tmp']} = {c['arr']}[today];" for c in f.channels)
    blocks = []
    for c in f.channels:
        if rescan_tie_strict:
            tie = c["better"]
        else:
            tie = c["better"] + "="
        blocks.append(f"""if( {c['idx']} < trailingIdx )
{{
   {c['idx']} = trailingIdx;
   {c['val']} = {c['arr']}[{c['idx']}];
   i = {c['idx']};
   TA_UNROLL(4)
   while( ++i<=today )
   {{
      {c['tmp']} = {c['arr']}[i];
      if( {c['tmp']} {tie} {c['val']} )
      {{
         {c['idx']} = i;
         {c['val']} = {c['tmp']};
      }}
   }}
}}
else if( {c['tmp']} {c['incoming']} {c['val']} )
{{
   {c['idx']} = today;
   {c['val']} = {c['tmp']};
}}""")
    return f"""{init}

while( today <= endIdx )
{{
{ind(reads, 3)}

{ind((chr(10)+chr(10)).join(blocks), 3)}

   outReal[outIdx++] = {f.emit_out};
   trailingIdx++;
   today++;
}}"""


# ---------------------------------------------------------------------------
# C6 : same automaton, but the rescan runs BACKWARDS from `today` with the
# comparison left STRICT.  Scanning newest-first means an equal value never
# displaces the newer one already held, so the cached index lands on the NEWEST
# occurrence of the extremum -- the same tie-break C5 buys with `>=`, but
# without losing clang's `maxsd` idiom recognition (C5 measured 10-34% slower on
# tie-free input for exactly that reason: 5x maxsd -> 5x cmpnlesd+blendvpd).
# ---------------------------------------------------------------------------
def automaton_rev(f):
    init = "\n".join(
        f"{c['idx']}  = -1;\n{c['val']}     = {c['init']};" for c in f.channels)
    reads = "\n".join(f"{c['tmp']} = {c['arr']}[today];" for c in f.channels)
    blocks = []
    for c in f.channels:
        blocks.append(f"""if( {c['idx']} < trailingIdx )
{{
   {c['idx']} = today;
   {c['val']} = {c['arr']}[today];
   i = today;
   TA_UNROLL(4)
   while( i > trailingIdx )
   {{
      i--;
      {c['tmp']} = {c['arr']}[i];
      if( {c['tmp']} {c['better']} {c['val']} )
      {{
         {c['idx']} = i;
         {c['val']} = {c['tmp']};
      }}
   }}
}}
else if( {c['tmp']} {c['incoming']} {c['val']} )
{{
   {c['idx']} = today;
   {c['val']} = {c['tmp']};
}}""")
    return f"""{init}

while( today <= endIdx )
{{
{ind(reads, 3)}

{ind((chr(10)+chr(10)).join(blocks), 3)}

   outReal[outIdx++] = {f.emit_out};
   trailingIdx++;
   today++;
}}"""


# ---------------------------------------------------------------------------
# C8 : no cached extremum at all -- rescan the whole window every bar.  Two
# independent comparison chains and no data-dependent branching, which is
# exactly why MIDPRICE already prefers it for period <= 20.  Cost is
# O(period)/bar but input-INDEPENDENT, and it needs no scratch and no codegen
# concession (a T3 bounded rescan window is already a supported stream tier).
# ---------------------------------------------------------------------------
def naive_arm(f):
    init = "\n".join(f"{c['val']} = {c['arr']}[trailingIdx];" for c in f.channels)
    upd = "\n".join(
        f"{c['tmp']} = {c['arr']}[i];\nif( {c['tmp']} {c['better']} {c['val']} ) {c['val']} = {c['tmp']};"
        for c in f.channels)
    return f"""while( today <= endIdx )
{{
{ind(init, 3)}
   i = trailingIdx + 1;
   TA_UNROLL(4)
   while( i <= today )
   {{
{ind(upd, 6)}
      i++;
   }}

   outReal[outIdx++] = {f.emit_out};
   trailingIdx++;
   today++;
}}"""


# ---------------------------------------------------------------------------
# C1 : monotonic deque
# ---------------------------------------------------------------------------
def dq_push(c, cur, at, n):
    v, ix, cnt, hd = f"dqV_{c['tag']}", f"dqI_{c['tag']}", f"cnt_{c['tag']}", f"hd_{c['tag']}"
    return f"""j = {hd} + {cnt};
if( j >= optInTimePeriod )
{{
   j = j - optInTimePeriod;
}}
k = j + optInTimePeriod - 1;
if( k >= optInTimePeriod )
{{
   k = k - optInTimePeriod;
}}
while( {cnt} > 0 && {v}[k] {c['pop']} {cur} )
{{
   {cnt}--;
   j = k;
   k = j + optInTimePeriod - 1;
   if( k >= optInTimePeriod )
   {{
      k = k - optInTimePeriod;
   }}
}}
{v}[j] = {cur};
{ix}[j] = {at};
{cnt}++;"""


def deque_arm(f):
    allocs, frees, inits, prime, step, outs = [], [], [], [], [], []
    for n, c in enumerate(f.channels):
        v, ix, cnt, hd = f"dqV_{c['tag']}", f"dqI_{c['tag']}", f"cnt_{c['tag']}", f"hd_{c['tag']}"
        allocs.append(f"""{v} = malloc((optInTimePeriod) * sizeof(double));
if( !{v} )
   return TA_ALLOC_ERR;
{ix} = malloc((optInTimePeriod) * sizeof(int));
if( !{ix} )
{{
   free({v});
   return TA_ALLOC_ERR;
}}""")
        frees.append(f"free({v});\nfree({ix});")
        inits.append(f"{hd}  = 0;\n{cnt} = 0;")
        prime.append(f"{c['tmp']} = {c['arr']}[i];\n" + dq_push(c, c["tmp"], "i", 0))
        step.append(f"""if( trailingIdx > {ix}[{hd}] )
{{
   {cnt}--;
   {hd}++;
   if( {hd} == optInTimePeriod )
   {{
      {hd} = 0;
   }}
}}

{c['tmp']} = {c['arr']}[today];
{dq_push(c, c['tmp'], 'today', 0)}""")
        outs.append(f"{c['val']} = {v}[{hd}];")
    nl = "\n"
    return f"""/* Monotonic deque per extremum channel: a strictly monotone run of
 * candidate extrema.  Each bar is pushed once and popped once, so the
 * cost is O(1) amortized per bar for every input shape -- there is no
 * rescan and no input-dependent behaviour.  The deques hold COPIES of
 * the input values, so the input and the output may be the same buffer.
 */
{nl.join(allocs)}

{nl.join(inits)}

/* Prime the deques with the bars preceding startIdx. */
i = trailingIdx;
while( i < startIdx )
{{
{ind((nl+nl).join(prime), 3)}
   i++;
}}

while( today <= endIdx )
{{
   /* At most one entry per deque can leave the window per bar. */
{ind((nl+nl).join(step), 3)}

{ind(nl.join(outs), 3)}
   outReal[outIdx++] = {f.emit_out};
   trailingIdx++;
   today++;
}}

{nl.join(frees)}"""


# ---------------------------------------------------------------------------
# C7 : the same monotonic deque as C1, but over a capacity rounded UP to a power
# of two, so the three circular-wrap compare-and-fixup branches per push collapse
# into one AND.  Costs at most 2x the scratch of C1 (still O(period)).  Included
# as a fairness check on C1: reporting "the deque loses" on one implementation
# would be exactly the kind of unverified claim this evaluation exists to avoid.
# ---------------------------------------------------------------------------
def dq2_push(c, cur, at):
    v, ix, cnt, hd = (f"dqV_{c['tag']}", f"dqI_{c['tag']}",
                      f"cnt_{c['tag']}", f"hd_{c['tag']}")
    return f"""j = ({hd} + {cnt}) & dqMask;
k = (j + dqMask) & dqMask;
while( {cnt} > 0 && {v}[k] {c['pop']} {cur} )
{{
   {cnt}--;
   j = k;
   k = (j + dqMask) & dqMask;
}}
{v}[j] = {cur};
{ix}[j] = {at};
{cnt}++;"""


def deque2_arm(f):
    nl = "\n"
    allocs, frees, inits, prime, step, outs = [], [], [], [], [], []
    for c in f.channels:
        v, ix, cnt, hd = (f"dqV_{c['tag']}", f"dqI_{c['tag']}",
                          f"cnt_{c['tag']}", f"hd_{c['tag']}")
        allocs.append(f"""{v} = malloc((dqCap) * sizeof(double));
if( !{v} )
   return TA_ALLOC_ERR;
{ix} = malloc((dqCap) * sizeof(int));
if( !{ix} )
{{
   free({v});
   return TA_ALLOC_ERR;
}}""")
        frees.append(f"free({v});\nfree({ix});")
        inits.append(f"{hd}  = 0;\n{cnt} = 0;")
        prime.append(f"{c['tmp']} = {c['arr']}[i];\n" + dq2_push(c, c["tmp"], "i"))
        step.append(f"""if( trailingIdx > {ix}[{hd}] )
{{
   {cnt}--;
   {hd} = ({hd} + 1) & dqMask;
}}

{c['tmp']} = {c['arr']}[today];
{dq2_push(c, c['tmp'], 'today')}""")
        outs.append(f"{c['val']} = {v}[{hd}];")
    return f"""/* Monotonic deque over a power-of-two capacity.  Same algorithm and
 * same amortized O(1) bound as the plain deque; the ring index wrap is a
 * mask rather than three compare-and-fixup branches.  Holds COPIES, so
 * the input and the output may alias.
 */
dqCap = 1;
while( dqCap < optInTimePeriod )
{{
   dqCap = dqCap + dqCap;
}}
dqMask = dqCap - 1;

{nl.join(allocs)}

{nl.join(inits)}

/* Prime the deques with the bars preceding startIdx. */
i = trailingIdx;
while( i < startIdx )
{{
{ind((nl+nl).join(prime), 3)}
   i++;
}}

while( today <= endIdx )
{{
{ind((nl+nl).join(step), 3)}

{ind(nl.join(outs), 3)}
   outReal[outIdx++] = {f.emit_out};
   trailingIdx++;
   today++;
}}

{nl.join(frees)}"""


# ---------------------------------------------------------------------------
# C3 : Van Herk / Gil-Werman, PER-SAMPLE
# ---------------------------------------------------------------------------
# Blocks of `optInTimePeriod` bars aligned on the FIRST window's trailing edge.
# sufX[m] = extremum of block[m .. p-1] for the block holding trailingIdx.
# preX    = extremum of [blockStart+p .. today] (the next block's prefix).
# window extremum = combine( sufX[trailingIdx-blockStart], preX ), except on the
# bar where trailingIdx == blockStart (window == the whole block, answer sufX[0]).
def vanherk_persample_arm(f):
    nl = "\n"
    allocs, frees, build, pre, comb = [], [], [], [], []
    for c in f.channels:
        s, p = f"suf_{c['tag']}", f"pre_{c['tag']}"
        allocs.append(f"""{s} = malloc((optInTimePeriod) * sizeof(double));
if( !{s} )
   return TA_ALLOC_ERR;""")
        frees.append(f"free({s});")
        build.append(f"""i = today;
{c['val']} = {c['arr']}[i];
{s}[i - blockStart] = {c['val']};
TA_UNROLL(4)
while( i > blockStart )
{{
   i--;
   {c['tmp']} = {c['arr']}[i];
   if( {c['tmp']} {c['better']} {c['val']} )
   {{
      {c['val']} = {c['tmp']};
   }}
   {s}[i - blockStart] = {c['val']};
}}
{c['val']} = {s}[0];""")
        pre.append(f"""{c['tmp']} = {c['arr']}[today];
if( trailingIdx == blockStart + 1 )
{{
   {p} = {c['tmp']};
}}
else if( {c['tmp']} {c['better']} {p} )
{{
   {p} = {c['tmp']};
}}
{c['val']} = {s}[trailingIdx - blockStart];
if( {p} {c['better']} {c['val']} )
{{
   {c['val']} = {p};
}}""")
    return f"""/* Van Herk / Gil-Werman block scan, per-sample form.
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
{nl.join(allocs)}

blockStart = trailingIdx;

while( today <= endIdx )
{{
   if( trailingIdx == blockStart )
   {{
      /* today == blockStart + optInTimePeriod - 1: the window is exactly
       * this block.  Materialise its suffix extrema backwards.
       */
{ind((nl+nl).join(build), 6)}
   }}
   else
   {{
{ind((nl+nl).join(pre), 6)}
   }}

   outReal[outIdx++] = {f.emit_out};

   trailingIdx++;
   if( trailingIdx == blockStart + optInTimePeriod )
   {{
      blockStart = blockStart + optInTimePeriod;
   }}
   today++;
}}

{nl.join(frees)}"""


# ---------------------------------------------------------------------------
# C2 : Van Herk / Gil-Werman, BLOCK-BATCHED
# ---------------------------------------------------------------------------
# Outer loop over blocks; the p outputs of a block are produced by two
# straight-line loops (branch-free, vectorizable).
def vanherk_batched_arm(f):
    nl = "\n"
    allocs, frees, build_suf, build_pre, comb = [], [], [], [], []
    for c in f.channels:
        s, p = f"suf_{c['tag']}", f"pre_{c['tag']}"
        allocs.append(f"""{s} = malloc((optInTimePeriod) * sizeof(double));
if( !{s} )
   return TA_ALLOC_ERR;
{p} = malloc((optInTimePeriod) * sizeof(double));
if( !{p} )
   return TA_ALLOC_ERR;""")
        frees.append(f"free({s});\nfree({p});")
        build_suf.append(f"""i = blockStart + optInTimePeriod - 1;
{c['val']} = {c['arr']}[i];
{s}[optInTimePeriod - 1] = {c['val']};
TA_UNROLL(4)
while( i > blockStart )
{{
   i--;
   {c['tmp']} = {c['arr']}[i];
   if( {c['tmp']} {c['better']} {c['val']} )
   {{
      {c['val']} = {c['tmp']};
   }}
   {s}[i - blockStart] = {c['val']};
}}""")
        build_pre.append(f"""{c['val']} = {c['arr']}[blockStart + optInTimePeriod];
{p}[0] = {c['val']};
i = 1;
TA_UNROLL(4)
while( i < nAvail )
{{
   {c['tmp']} = {c['arr']}[blockStart + optInTimePeriod + i];
   if( {c['tmp']} {c['better']} {c['val']} )
   {{
      {c['val']} = {c['tmp']};
   }}
   {p}[i] = {c['val']};
   i++;
}}""")
        comb.append(f"""{c['val']} = {s}[m];
if( {p}[m - 1] {c['better']} {c['val']} )
{{
   {c['val']} = {p}[m - 1];
}}""")
    firstout = nl.join(f"{c['val']} = suf_{c['tag']}[0];" for c in f.channels)
    return f"""/* Van Herk / Gil-Werman block scan, block-batched form.
 *
 * Same decomposition as the per-sample form, but the p outputs belonging
 * to one block boundary are produced together: one backward pass builds
 * the older block's suffix extrema, one forward pass builds the newer
 * block's prefix extrema, and a third branch-free pass combines them.
 * The three passes are straight-line loops with no data-dependent
 * branching, which is what lets a compiler vectorize them.  All three
 * scratch arrays hold COPIES, so the input and the output may alias.
 */
{nl.join(allocs)}

blockStart = trailingIdx;

while( today <= endIdx )
{{
   /* Suffix extrema of the block [blockStart, blockStart+p-1].  It is
    * fully available: today == blockStart + p - 1 <= endIdx here.
    */
{ind((nl+nl).join(build_suf), 3)}

{ind(firstout, 3)}
   outReal[outIdx++] = {f.emit_out};
   trailingIdx++;
   today++;
   if( today > endIdx )
   {{
      blockStart = blockStart + optInTimePeriod;
   }}
   else
   {{
      /* Prefix extrema of the next block, clamped to what remains. */
      nAvail = endIdx - (blockStart + optInTimePeriod) + 1;
      if( nAvail > optInTimePeriod - 1 )
      {{
         nAvail = optInTimePeriod - 1;
      }}
{ind((nl+nl).join(build_pre), 6)}

      m = 1;
      while( m <= nAvail )
      {{
{ind((nl+nl).join(comb), 9)}
         outReal[outIdx++] = {f.emit_out};
         m++;
      }}
      trailingIdx = trailingIdx + nAvail;
      today = today + nAvail;
      blockStart = blockStart + optInTimePeriod;
   }}
}}

{nl.join(frees)}"""


# ---------------------------------------------------------------------------
# C4 : two-stack queue
# ---------------------------------------------------------------------------
# `back` stack holds raw values plus a running extremum scalar; `front` stack
# holds running extrema.  When the front empties, the back is drained into it
# in reverse order (O(p) once every p bars).
def twostack_arm(f):
    nl = "\n"
    allocs, frees, inits, prime, step = [], [], [], [], []
    for c in f.channels:
        bv, fm = f"bkV_{c['tag']}", f"ftM_{c['tag']}"
        bn, fn, fp = f"bkN_{c['tag']}", f"ftN_{c['tag']}", f"ftP_{c['tag']}"
        bm = f"bkM_{c['tag']}"
        allocs.append(f"""{bv} = malloc((optInTimePeriod) * sizeof(double));
if( !{bv} )
   return TA_ALLOC_ERR;
{fm} = malloc((optInTimePeriod) * sizeof(double));
if( !{fm} )
   return TA_ALLOC_ERR;""")
        frees.append(f"free({bv});\nfree({fm});")
        inits.append(f"{bn} = 0;\n{fn} = 0;\n{fp} = 0;\n{bm} = {c['init']};")
        push = f"""{c['tmp']} = {c['arr']}[ATBAR];
if( {bn} == 0 )
{{
   {bm} = {c['tmp']};
}}
else if( {c['tmp']} {c['better']} {bm} )
{{
   {bm} = {c['tmp']};
}}
{bv}[{bn}] = {c['tmp']};
{bn}++;"""
        prime.append(push.replace("ATBAR", "i"))
        # push today, read the answer, then retire the oldest bar (draining the
        # back stack into the front stack when the front runs out).
        step.append(f"""{push.replace('ATBAR', 'today')}

if( {fp} < {fn} )
{{
   {c['val']} = {fm}[{fp}];
   if( {bm} {c['better']} {c['val']} )
   {{
      {c['val']} = {bm};
   }}
}}
else
{{
   {c['val']} = {bm};
}}""")
    retire = []
    for c in f.channels:
        bv, fm = f"bkV_{c['tag']}", f"ftM_{c['tag']}"
        bn, fn, fp = f"bkN_{c['tag']}", f"ftN_{c['tag']}", f"ftP_{c['tag']}"
        bm = f"bkM_{c['tag']}"
        retire.append(f"""if( {fp} == {fn} )
{{
   /* Front exhausted: drain the back stack into it, annotating each
    * entry with the extremum of itself and every NEWER back entry.
    * One O(p) pass every p bars.
    */
   i = {bn} - 1;
   {c['val']} = {bv}[i];
   {fm}[i] = {c['val']};
   TA_UNROLL(4)
   while( i > 0 )
   {{
      i--;
      {c['tmp']} = {bv}[i];
      if( {c['tmp']} {c['better']} {c['val']} )
      {{
         {c['val']} = {c['tmp']};
      }}
      {fm}[i] = {c['val']};
   }}
   {fn} = {bn};
   {fp} = 0;
   {bn} = 0;
}}
{fp}++;""")
    return f"""/* Two-stack queue: amortized O(1) with no per-element index
 * bookkeeping.  The back stack holds the raw values of the newest run
 * plus a running extremum scalar; the front stack holds, per entry, the
 * extremum of that entry and every newer front entry, so retiring the
 * oldest bar is a pointer bump.  When the front runs out, the back is
 * drained into it in one O(p) pass -- once every p bars, hence O(1)
 * amortized, with an O(p) latency spike on the draining bar.  Both
 * stacks hold COPIES, so the input and the output may alias.
 */
{nl.join(allocs)}

{nl.join(inits)}

/* Prime with the bars preceding startIdx. */
i = trailingIdx;
while( i < startIdx )
{{
{ind((nl+nl).join(prime), 3)}
   i++;
}}

while( today <= endIdx )
{{
{ind((nl+nl).join(step), 3)}

   outReal[outIdx++] = {f.emit_out};

{ind((nl+nl).join(retire), 3)}
   trailingIdx++;
   today++;
}}

{nl.join(frees)}"""


# ---------------------------------------------------------------------------
# assembly
# ---------------------------------------------------------------------------
def decls(f, cand):
    reals = [c["val"] for c in f.channels] + [c["tmp"] for c in f.channels]
    ints = ["outIdx", "nbInitialElementNeeded", "trailingIdx", "today", "i"]
    idxs = [c["idx"] for c in f.channels]
    ptrs = []
    if cand == "C1":
        ints += ["j", "k"]
        for c in f.channels:
            ptrs.append(f"double *dqV_{c['tag']};")
            ptrs.append(f"int *dqI_{c['tag']};")
            ints += [f"hd_{c['tag']}", f"cnt_{c['tag']}"]
    elif cand == "C3":
        ints += ["blockStart"]
        for c in f.channels:
            ptrs.append(f"double *suf_{c['tag']};")
            reals.append(f"pre_{c['tag']}")
    elif cand == "C2":
        ints += ["blockStart", "nAvail", "m"]
        for c in f.channels:
            ptrs.append(f"double *suf_{c['tag']};")
            ptrs.append(f"double *pre_{c['tag']};")
    elif cand == "C7":
        ints += ["j", "k", "dqCap", "dqMask"]
        for c in f.channels:
            ptrs.append(f"double *dqV_{c['tag']};")
            ptrs.append(f"int *dqI_{c['tag']};")
            ints += [f"hd_{c['tag']}", f"cnt_{c['tag']}"]
    elif cand == "C4":
        for c in f.channels:
            ptrs.append(f"double *bkV_{c['tag']};")
            ptrs.append(f"double *ftM_{c['tag']};")
            ints += [f"bkN_{c['tag']}", f"ftN_{c['tag']}", f"ftP_{c['tag']}"]
            reals.append(f"bkM_{c['tag']}")
    out = "   double " + ", ".join(reals) + ";\n"
    for p in ptrs:
        out += "   " + p + "\n"
    out += "   int " + ", ".join(ints + idxs) + ";\n"
    return out


ARMS = {
    "C1": deque_arm,
    "C7": deque2_arm,
    "C2": vanherk_batched_arm,
    "C3": vanherk_persample_arm,
    "C4": twostack_arm,
}


def build(fname, cand):
    f = FUNCS[fname]
    d = decls(f, cand)
    if cand == "C0":
        body = PROLOGUE + "\n" + ind(automaton(f, True), 3) + "\n"
    elif cand == "C5":
        body = PROLOGUE + "\n" + ind(automaton(f, False), 3) + "\n"
    elif cand == "C6":
        body = PROLOGUE + "\n" + ind(automaton_rev(f), 3) + "\n"
    elif cand == "C8":
        body = PROLOGUE + "\n" + ind(naive_arm(f), 3) + "\n"
    else:
        body = (PROLOGUE
                + "\n   if( optInTimePeriod <= 100000 )\n   {\n"
                + ind(ARMS[cand](f), 6)
                + "\n   }\n   else\n   {\n"
                + ind(automaton(f, True), 6)
                + "\n   }\n")
    return f.hdr + "\n" + f.lookback + "\n\n" + f.sig + "\n{\n" + d + body + EPILOGUE


# C0 and C5 are derived from the UPSTREAM input file verbatim, so the reference
# every measurement is relative to is byte-for-byte what ships, and C5 differs
# from it by exactly the two rescan comparison operators.
# Pristine snapshot of the upstream input files (`git show HEAD:...`), NOT the live
# tree: gate.sh installs candidates into the tree, so reading it here would make
# the reference depend on whatever was gated last.
UPSTREAM = os.path.dirname(os.path.abspath(__file__)) + "/upstream"

C5_SUBS = {
    "max": [("if( tmp > highest )", "if( tmp >= highest )")],
    "midpoint": [("if( tmpHigh > highest )", "if( tmpHigh >= highest )"),
                 ("if( tmpLow < lowest )", "if( tmpLow <= lowest )")],
}


def upstream(fname):
    with open(f"{UPSTREAM}/{fname}.c") as fh:
        return fh.read()


def build_c5(fname):
    s = upstream(fname)
    for a, b in C5_SUBS[fname]:
        assert s.count(a) == 1, (fname, a, s.count(a))
        s = s.replace(a, b)
    return s


C9_SUBS = {
    "max": [("      else if( tmp >= highest )", "      if( tmp >= highest )")],
    "midpoint": [("      else if( tmpHigh >= highest )", "      if( tmpHigh >= highest )"),
                 ("      else if( tmpLow <= lowest )", "      if( tmpLow <= lowest )")],
}


def build_c9(fname):
    s = upstream(fname)
    for a, b in C9_SUBS[fname]:
        assert s.count(a) == 1, (fname, repr(a), s.count(a))
        s = s.replace(a, b)
    return s


if __name__ == "__main__":
    out = os.path.dirname(os.path.abspath(__file__)) + "/variants"
    os.makedirs(out, exist_ok=True)
    for fname in FUNCS:
        with open(f"{out}/{fname}.C0.c", "w") as fh:
            fh.write(upstream(fname))
        with open(f"{out}/{fname}.C5.c", "w") as fh:
            fh.write(build_c5(fname))
        with open(f"{out}/{fname}.C9.c", "w") as fh:
            fh.write(build_c9(fname))
        print("wrote C0 (verbatim upstream) and C5 for", fname)
        for scalar in ("C6", "C8"):
            with open(f"{out}/{fname}.{scalar}.c", "w") as fh:
                fh.write(build(fname, scalar))
        for cand in ("C1", "C2", "C3", "C4", "C7"):
            p = f"{out}/{fname}.{cand}.c"
            with open(p, "w") as fh:
                fh.write(build(fname, cand))
            print("wrote", p)
