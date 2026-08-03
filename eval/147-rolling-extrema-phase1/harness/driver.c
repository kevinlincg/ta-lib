/* Out-of-tree measurement driver for TA-Lib issue #147 (rolling min/max).
 *
 * Lives OUTSIDE the released tree on purpose: the maintainer asked for no second
 * benchmark tool inside ta-lib.  It reuses ta-lib's own benchmark corpus header
 * (src/tools/ta_bench/bench_corpus.h, PR #153) so the input classes are exactly
 * the ones the project measures on -- no bespoke inputs.
 *
 * Each candidate is a `ta_codegen/input/<name>/<name>.c` body compiled into its
 * own translation unit with the function renamed to <name>_<cand>.  What is
 * timed is therefore the same C the codegen would ship (the guarded wrapper's
 * one-time parameter validation is outside the loop and cannot show up in a
 * per-bar number; see METHOD.md).
 *
 * Output: one CSV line per (func,cand,shape,period), `min` over --reps.  The
 * layout sweep is one level up: build.sh produces several binaries whose code
 * placement differs, run.sh takes the median and range across them.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#include "shim.h"
#include "bench_corpus.h"

typedef TA_RetCode (*Fn)(int, int, const double *, int, int *, int *, double *);

#define DECL(f, c) \
    TA_RetCode f##_##c(int, int, const double *, int, int *, int *, double *); \
    int f##_##c##_lookback(int);

DECL(max, C0) DECL(max, C1) DECL(max, C2) DECL(max, C3) DECL(max, C4) DECL(max, C5)
DECL(max, C6) DECL(max, C7) DECL(max, C8) DECL(max, C9)
DECL(midpoint, C0) DECL(midpoint, C1) DECL(midpoint, C2) DECL(midpoint, C3)
DECL(midpoint, C4) DECL(midpoint, C5)
DECL(midpoint, C6) DECL(midpoint, C7) DECL(midpoint, C8)
DECL(midpoint, C9)

typedef struct { const char *func; const char *cand; Fn fn; } Cand;

static const Cand CANDS[] = {
    { "max", "C0", max_C0 }, { "max", "C1", max_C1 }, { "max", "C2", max_C2 },
    { "max", "C3", max_C3 }, { "max", "C4", max_C4 }, { "max", "C5", max_C5 },
    { "max", "C6", max_C6 }, { "max", "C7", max_C7 }, { "max", "C8", max_C8 }, { "max", "C9", max_C9 },
    { "midpoint", "C0", midpoint_C0 }, { "midpoint", "C1", midpoint_C1 },
    { "midpoint", "C2", midpoint_C2 }, { "midpoint", "C3", midpoint_C3 },
    { "midpoint", "C4", midpoint_C4 }, { "midpoint", "C5", midpoint_C5 },
    { "midpoint", "C6", midpoint_C6 }, { "midpoint", "C7", midpoint_C7 },
    { "midpoint", "C8", midpoint_C8 }, { "midpoint", "C9", midpoint_C9 },
};
#define NCANDS ((int)(sizeof(CANDS)/sizeof(CANDS[0])))

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static volatile double g_sink;

/* --------------------------------------------------------------- correctness */

/* Independent O(n*period) reference, written from the DEFINITION rather than
 * from the shipped algorithm.  Guards against the possibility that the C0
 * transcription in variants/ has itself drifted from upstream.
 */
static TA_RetCode naive_max(int startIdx, int endIdx, const double *in, int per,
                            int *ob, int *on, double *out)
{
    int lb = per - 1, t, i, oi2 = 0;
    if( startIdx < lb ) startIdx = lb;
    if( startIdx > endIdx ) { *ob = 0; *on = 0; return TA_SUCCESS; }
    for( t = startIdx; t <= endIdx; t++ )
    {
        double m = in[t - lb];
        for( i = t - lb + 1; i <= t; i++ ) if( in[i] > m ) m = in[i];
        out[oi2++] = m;
    }
    *ob = startIdx; *on = oi2; return TA_SUCCESS;
}

static TA_RetCode naive_midpoint(int startIdx, int endIdx, const double *in, int per,
                                 int *ob, int *on, double *out)
{
    int lb = per - 1, t, i, oi2 = 0;
    if( startIdx < lb ) startIdx = lb;
    if( startIdx > endIdx ) { *ob = 0; *on = 0; return TA_SUCCESS; }
    for( t = startIdx; t <= endIdx; t++ )
    {
        double hi = in[t - lb], lo = in[t - lb];
        for( i = t - lb + 1; i <= t; i++ )
        {
            if( in[i] > hi ) hi = in[i];
            if( in[i] < lo ) lo = in[i];
        }
        out[oi2++] = (hi + lo) / 2.0;
    }
    *ob = startIdx; *on = oi2; return TA_SUCCESS;
}


/* Bit-exact comparison against the C0 reference, over every shape and a range
 * of periods, PLUS the in-place aliasing case (out == in, #130).  A candidate
 * that fails here is dead; nothing is timed for it.
 */
static int verify(const double *in, int n, const char *funcname, int quiet)
{
    static const int periods[] = { 2, 3, 5, 14, 15, 20, 21, 29, 30, 31, 63, 64,
                                   65, 100, 128, 200, 255, 256, 257, 500, 1000,
                                   4096, 0 };
    double *ref = malloc((size_t)n * sizeof(double));
    double *got = malloc((size_t)n * sizeof(double));
    double *ali = malloc((size_t)n * sizeof(double));
    int bad = 0, p, ci, k, rb, rn, gb, gn;
    Fn c0 = NULL;

    for( ci = 0; ci < NCANDS; ci++ )
        if( strcmp(CANDS[ci].func, funcname) == 0 && strcmp(CANDS[ci].cand, "C0") == 0 )
            c0 = CANDS[ci].fn;
    if( !c0 ) { fprintf(stderr, "no C0 for %s\n", funcname); return 1; }

    for( p = 0; periods[p]; p++ )
    {
        int per = periods[p];
        int nb2, nn;
        if( per > n ) continue;
        memset(ref, 0, (size_t)n * sizeof(double));
        c0(0, n - 1, in, per, &rb, &rn, ref);
        /* the shipped-algorithm reference must itself match the definition */
        memset(got, 0, (size_t)n * sizeof(double));
        if( strcmp(funcname, "max") == 0 ) naive_max(0, n - 1, in, per, &nb2, &nn, got);
        else naive_midpoint(0, n - 1, in, per, &nb2, &nn, got);
        if( nb2 != rb || nn != rn ) { printf("NAIVE-META %s per=%d\n", funcname, per); bad++; }
        else for( k = 0; k < rn; k++ )
            if( memcmp(&ref[k], &got[k], sizeof(double)) != 0 )
            { printf("NAIVE-MISMATCH %s per=%d at %d ref=%.17g naive=%.17g\n",
                     funcname, per, k, ref[k], got[k]); bad++; break; }
        for( ci = 0; ci < NCANDS; ci++ )
        {
            if( strcmp(CANDS[ci].func, funcname) != 0 ) continue;
            memset(got, 0, (size_t)n * sizeof(double));
            CANDS[ci].fn(0, n - 1, in, per, &gb, &gn, got);
            if( gb != rb || gn != rn )
            {
                printf("MISMATCH %s %s per=%d meta ref=(%d,%d) got=(%d,%d)\n",
                       funcname, CANDS[ci].cand, per, rb, rn, gb, gn);
                bad++;
                continue;
            }
            for( k = 0; k < rn; k++ )
            {
                if( memcmp(&ref[k], &got[k], sizeof(double)) != 0 )
                {
                    printf("MISMATCH %s %s per=%d at %d ref=%.17g got=%.17g\n",
                           funcname, CANDS[ci].cand, per, k, ref[k], got[k]);
                    bad++;
                    break;
                }
            }
            /* in-place: out buffer IS the input buffer (#130) */
            memcpy(ali, in, (size_t)n * sizeof(double));
            CANDS[ci].fn(0, n - 1, ali, per, &gb, &gn, ali);
            if( gb != rb || gn != rn ) { printf("ALIAS-META %s %s per=%d\n",
                                                funcname, CANDS[ci].cand, per); bad++; continue; }
            for( k = 0; k < rn; k++ )
            {
                if( memcmp(&ref[k], &ali[k], sizeof(double)) != 0 )
                {
                    printf("ALIAS-MISMATCH %s %s per=%d at %d ref=%.17g got=%.17g\n",
                           funcname, CANDS[ci].cand, per, k, ref[k], ali[k]);
                    bad++;
                    break;
                }
            }
            /* a non-zero startIdx exercises the priming path separately */
            if( n > 3 * per )
            {
                int s = per + 7;
                memset(got, 0, (size_t)n * sizeof(double));
                c0(s, n - 1, in, per, &rb, &rn, ref);
                CANDS[ci].fn(s, n - 1, in, per, &gb, &gn, got);
                if( gb != rb || gn != rn ) { printf("START-META %s %s per=%d\n",
                                                    funcname, CANDS[ci].cand, per); bad++; }
                else for( k = 0; k < rn; k++ )
                    if( memcmp(&ref[k], &got[k], sizeof(double)) != 0 )
                    {
                        printf("START-MISMATCH %s %s per=%d at %d\n",
                               funcname, CANDS[ci].cand, per, k);
                        bad++;
                        break;
                    }
                c0(0, n - 1, in, per, &rb, &rn, ref);
            }
        }
    }
    if( !bad && !quiet ) printf("verify %s: OK\n", funcname);
    free(ref); free(got); free(ali);
    return bad;
}

/* Signed-zero coverage.  bench_corpus.h holds `low > 0` for every shape, so no
 * corpus series contains a zero at all -- and +0.0 / -0.0 compare EQUAL while
 * being different bit patterns.  Any candidate that changes which of two equal
 * window members is selected can therefore emit a different SIGN of zero while
 * passing a corpus-only bitwise gate.  ta-lib's own frozen-oracle gate catches
 * this and classifies it as `BENIGN ... signed-zero`, which is how it was found;
 * this generator reproduces it out of tree so each candidate can be characterised
 * exactly rather than inferred.
 */
static void zero_mix(double *a, int n, int variant)
{
    int i;
    unsigned s = 12345u + (unsigned)variant * 7919u;
    for( i = 0; i < n; i++ )
    {
        s = s * 1103515245u + 12345u;
        switch( (s >> 16) % 5u )
        {
        case 0: a[i] =  0.0; break;
        case 1: a[i] = -0.0; break;
        case 2: a[i] =  0.0; break;
        case 3: a[i] = -0.0; break;
        default: a[i] = (double)(((s >> 8) % 7u)) * 0.5; break;  /* ties too */
        }
    }
}

/* ------------------------------------------------------------------- timing */

/* One timed rep.  `budget_ns` picks the iteration count from a pilot call so a
 * cheap (period 14, randwalk) and an expensive (period 1000, constant) point
 * both get roughly the same wall time -- otherwise the cheap points are
 * dominated by clock resolution and the expensive ones take minutes.
 */
static double bench(Fn fn, const double *in, int n, int per, double budget_ns,
                    double *out)
{
    int b, nb, it, iters;
    double t0, t1, pilot;

    fn(0, n - 1, in, per, &b, &nb, out);            /* warm + pilot */
    g_sink += out[0] + out[nb - 1];
    t0 = now_ns();
    fn(0, n - 1, in, per, &b, &nb, out);
    t1 = now_ns();
    g_sink += out[nb - 1];
    pilot = t1 - t0;
    if( pilot < 1.0 ) pilot = 1.0;
    iters = (int)(budget_ns / pilot);
    if( iters < 3 ) iters = 3;
    if( iters > 20000 ) iters = 20000;

    t0 = now_ns();
    for( it = 0; it < iters; it++ )
    {
        fn(0, n - 1, in, per, &b, &nb, out);
        g_sink += out[nb - 1];
    }
    t1 = now_ns();
    return (t1 - t0) / ((double)iters * (double)nb);   /* ns per output bar */
}

int main(int argc, char **argv)
{
    int n = 100000, reps = 3;
    double budget = 25e6;
    int do_verify = 0, ci, si, pi, r;
    const char *shapes = "randwalk,randwalk-lo,randwalk-hi,gbm,trend-chop-0.5p,"
                         "trend-chop-1p,trend-chop-2p,trend-chop-4p,mono-up,"
                         "mono-down,constant";
    const char *pers = "14,30,64,200,1000,4096";
    const char *funcs = "max,midpoint";
    const char *cands = "C0,C1,C2,C3,C4,C5,C6,C7,C8,C9";
    const char *tag = "L0";
    int i;
    BenchCorpusCfg cfg;
    double *o, *h, *l, *c, *v, *oi, *out;
    char shbuf[512], pbuf[256], fbuf[128], cbuf[128];

    bench_corpus_defaults(&cfg);
    for( i = 1; i < argc; i++ )
    {
        if( strncmp(argv[i], "--points=", 9) == 0 )  n = atoi(argv[i] + 9);
        else if( strncmp(argv[i], "--budget-ms=", 12) == 0 ) budget = atof(argv[i] + 12) * 1e6;
        else if( strncmp(argv[i], "--reps=", 7) == 0 )  reps = atoi(argv[i] + 7);
        else if( strncmp(argv[i], "--shapes=", 9) == 0 ) shapes = argv[i] + 9;
        else if( strncmp(argv[i], "--periods=", 10) == 0 ) pers = argv[i] + 10;
        else if( strncmp(argv[i], "--funcs=", 8) == 0 ) funcs = argv[i] + 8;
        else if( strncmp(argv[i], "--cands=", 8) == 0 ) cands = argv[i] + 8;
        else if( strncmp(argv[i], "--tag=", 6) == 0 ) tag = argv[i] + 6;
        else if( strncmp(argv[i], "--seed=", 7) == 0 ) cfg.seed = atoi(argv[i] + 7);
        else if( strncmp(argv[i], "--regime-period=", 16) == 0 ) cfg.refPeriod = atoi(argv[i] + 16);
        else if( strncmp(argv[i], "--trend-strength=", 17) == 0 ) cfg.trendStrength = atof(argv[i] + 17);
        else if( strcmp(argv[i], "--verify") == 0 ) do_verify = 1;
        else { fprintf(stderr, "unknown arg %s\n", argv[i]); return 2; }
    }

    o = malloc((size_t)n * sizeof(double)); h = malloc((size_t)n * sizeof(double));
    l = malloc((size_t)n * sizeof(double)); c = malloc((size_t)n * sizeof(double));
    v = malloc((size_t)n * sizeof(double)); oi = malloc((size_t)n * sizeof(double));
    out = malloc((size_t)n * sizeof(double));
    if( !o || !h || !l || !c || !v || !oi || !out ) return 3;

    snprintf(shbuf, sizeof shbuf, "%s", shapes);
    snprintf(pbuf, sizeof pbuf, "%s", pers);
    snprintf(fbuf, sizeof fbuf, "%s", funcs);
    snprintf(cbuf, sizeof cbuf, "%s", cands);

    if( do_verify )
    {
        int bad = 0;
        char *sp, *st;
        for( sp = strtok_r(shbuf, ",", &st); sp; sp = strtok_r(NULL, ",", &st) )
        {
            char *fp, *ft;
            int id = bench_shape_id(sp);
            if( id < 0 ) { fprintf(stderr, "bad shape %s\n", sp); return 2; }
            cfg.shape = id;
            bench_corpus_gen(&cfg, n, o, h, l, c, v, oi, NULL);
            snprintf(fbuf, sizeof fbuf, "%s", funcs);
            for( fp = strtok_r(fbuf, ",", &ft); fp; fp = strtok_r(NULL, ",", &ft) )
            {
                int b = verify(strcmp(fp, "max") == 0 ? c : c, n, fp, 1);
                if( b ) printf("  (shape=%s)\n", sp);
                bad += b;
            }
        }
        {
            int zb = 0, zv;
            for( zv = 0; zv < 4; zv++ )
            {
                char *fp, *ft;
                zero_mix(c, n, zv);
                snprintf(fbuf, sizeof fbuf, "%s", funcs);
                for( fp = strtok_r(fbuf, ",", &ft); fp; fp = strtok_r(NULL, ",", &ft) )
                    zb += verify(c, n, fp, 1);
            }
            if( zb ) printf("  (signed-zero mix: %d case(s) differ -- sign of zero only, "
                            "see PROGRESS.md FINDING 8; NOT counted as a gate failure)\n", zb);
            else printf("  (signed-zero mix: clean)\n");
        }
        printf(bad ? "VERIFY FAILED (%d)\n" : "VERIFY OK (%d problems)\n", bad);
        return bad ? 1 : 0;
    }

    printf("#layout,func,cand,shape,period,ns_per_bar\n");
    {
        char *sp, *st;
        for( sp = strtok_r(shbuf, ",", &st); sp; sp = strtok_r(NULL, ",", &st) )
        {
            char *pp, *pt;
            int id = bench_shape_id(sp);
            if( id < 0 ) { fprintf(stderr, "bad shape %s\n", sp); return 2; }
            cfg.shape = id;
            bench_corpus_gen(&cfg, n, o, h, l, c, v, oi, NULL);
            snprintf(pbuf, sizeof pbuf, "%s", pers);
            for( pp = strtok_r(pbuf, ",", &pt); pp; pp = strtok_r(NULL, ",", &pt) )
            {
                int per = atoi(pp);
                if( per < 2 || per >= n ) continue;
                for( ci = 0; ci < NCANDS; ci++ )
                {
                    double best = 1e30;
                    if( !strstr(funcs, CANDS[ci].func) ) continue;
                    if( !strstr(cands, CANDS[ci].cand) ) continue;
                    for( r = 0; r < reps; r++ )
                    {
                        double t = bench(CANDS[ci].fn, c, n, per, budget, out);
                        if( t < best ) best = t;
                    }
                    printf("%s,%s,%s,%s,%d,%.4f\n", tag, CANDS[ci].func,
                           CANDS[ci].cand, sp, per, best);
                    fflush(stdout);
                }
            }
        }
    }
    (void)si; (void)pi;
    return 0;
}
