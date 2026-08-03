/* Guarded vs unguarded, measured against the ACTUAL BUILT LIBRARY.
 *
 * The repo generates two entry points per indicator: `TA_MAX` (validates params,
 * then delegates) and `TA_MAX_Unguarded` (skips the validation prologue).  The
 * repo convention is to report them separately, and the main harness cannot: it
 * compiles the input `.c` body, which is the unguarded core.
 *
 * So this links libta-lib.a from whatever candidate is currently installed in the
 * tree and times both entry points on the same corpus, at the same periods, so
 * the guarded delta can be quoted as a real number instead of an argument.
 *
 * Build (see guarded.sh) — needs the tree built by scripts/build.py first.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ta_libc.h"
#include "ta_func_unguarded.h"
#include "bench_corpus.h"

static double now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
}

static volatile double g_sink;

typedef TA_RetCode (*Fn)(int, int, const double *, int, int *, int *, double *);

static double bench(Fn fn, const double *in, int n, int per, double budget, double *out)
{
    int b = 0, nb = 0, it, iters;
    double t0, t1, pilot;
    fn(0, n - 1, in, per, &b, &nb, out);
    g_sink += out[0];
    t0 = now_ns(); fn(0, n - 1, in, per, &b, &nb, out); t1 = now_ns();
    g_sink += out[nb - 1];
    pilot = t1 - t0; if( pilot < 1.0 ) pilot = 1.0;
    iters = (int)(budget / pilot); if( iters < 3 ) iters = 3; if( iters > 20000 ) iters = 20000;
    t0 = now_ns();
    for( it = 0; it < iters; it++ ) { fn(0, n - 1, in, per, &b, &nb, out); g_sink += out[nb - 1]; }
    t1 = now_ns();
    return (t1 - t0) / ((double)iters * (double)nb);
}

int main(int argc, char **argv)
{
    int n = 100000, i, r, reps = 3;
    const char *tag = argv[1] ? argv[1] : "?";
    const char *shapes[] = { "randwalk", "trend-chop-2p", "constant", NULL };
    int pers[] = { 14, 30, 200, 1000, 0 };
    BenchCorpusCfg cfg;
    double *o, *h, *l, *c, *v, *oi, *out;

    for( i = 2; i < argc; i++ )
        if( strncmp(argv[i], "--points=", 9) == 0 ) n = atoi(argv[i] + 9);

    TA_Initialize();
    bench_corpus_defaults(&cfg);
    o = malloc((size_t)n * 8); h = malloc((size_t)n * 8); l = malloc((size_t)n * 8);
    c = malloc((size_t)n * 8); v = malloc((size_t)n * 8); oi = malloc((size_t)n * 8);
    out = malloc((size_t)n * 8);

    printf("#build,func,variant,shape,period,ns_per_bar\n");
    for( i = 0; shapes[i]; i++ )
    {
        int sh = bench_shape_id(shapes[i]);
        int pi;
        cfg.shape = sh;
        bench_corpus_gen(&cfg, n, o, h, l, c, v, oi, NULL);
        for( pi = 0; pers[pi]; pi++ )
        {
            double bg = 1e30, bu = 1e30, t;
            for( r = 0; r < reps; r++ )
            {
                t = bench((Fn)TA_MAX, c, n, pers[pi], 20e6, out);           if( t < bg ) bg = t;
                t = bench((Fn)TA_MAX_Unguarded, c, n, pers[pi], 20e6, out); if( t < bu ) bu = t;
            }
            printf("%s,MAX,guarded,%s,%d,%.4f\n", tag, shapes[i], pers[pi], bg);
            printf("%s,MAX,unguarded,%s,%d,%.4f\n", tag, shapes[i], pers[pi], bu);
            for( r = 0; r < reps; r++ )
            {
                t = bench((Fn)TA_MIDPOINT, c, n, pers[pi], 20e6, out);           if( r == 0 || t < bg ) bg = t;
                t = bench((Fn)TA_MIDPOINT_Unguarded, c, n, pers[pi], 20e6, out); if( r == 0 || t < bu ) bu = t;
            }
            printf("%s,MIDPOINT,guarded,%s,%d,%.4f\n", tag, shapes[i], pers[pi], bg);
            printf("%s,MIDPOINT,unguarded,%s,%d,%.4f\n", tag, shapes[i], pers[pi], bu);
            fflush(stdout);
        }
    }
    TA_Shutdown();
    return 0;
}
