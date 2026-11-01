/* 0.2.8 frame-filter timing microbenchmark. The core switches a filter off after 8 calls over
 * 200 us (wall clock, cumulative), and EP147's bus thread calls it ~1000 times a second.
 * What can make a call slow: translation of cold code under qemu (TCG), page faults, locks,
 * clock reads, loops. This prints, per path: the first (cold) call, the warm average and the
 * worst warm call, in microseconds on the virtual counter. Run it on arm64 (the SDK container
 * under qemu-user: `docker run --platform=linux/arm64 ... filter_bench`) for the cold numbers;
 * on the host it measures the warm cost only. `filter_bench prewarm` runs the mod's init prewarm
 * first and exits 1 if a warm path exceeds 20 us average or any path (first call included) exceeds
 * BUDGET_US = 100 us (half the core's budget); without the argument it is informational. */
#include "../active-loop.c"
#include <assert.h>
#define BUDGET_US 100.0
static unsigned char F[ERP_LEN];
static int ish[2], imm[2], idl[2], ipad[2];
static uint64_t frq;
static double us(uint64_t t) { return (double)t * 1e6 / (double)frq; }
static uint64_t step(void)
{
    memset(F, 0, sizeof F);
    for (int s = 0; s < 2; s++) {
        if (ish[s]) F[SO[s].sh_o]  |= (unsigned char)(1u << SO[s].sh_b);
        if (imm[s]) F[SO[s].mem_o] |= (unsigned char)(1u << SO[s].mem_b);
        if (idl[s]) F[SO[s].del_o] |= (unsigned char)(1u << SO[s].del_b);
        F[SO[s].pad_o] |= (unsigned char)ipad[s];
    }
    uint64_t a = cnt_now();
    on_erp(AZ_BUS_ERP_IN, F, ERP_LEN, NULL);
    return cnt_now() - a;
}
static void clear_in(void) { memset(ish,0,sizeof ish); memset(imm,0,sizeof imm); memset(idl,0,sizeof idl); memset(ipad,0,sizeof ipad); }
static int bad;
static void report(const char *name, uint64_t cold, double warm_avg, double warm_max)
{
    int over = us(cold) > BUDGET_US || warm_max > BUDGET_US * 5 || warm_avg > 20.0;
    printf("%-34s cold %8.2f us   warm avg %7.3f us   warm worst %8.2f us%s\n", name, us(cold), warm_avg, warm_max, over ? "   <-- OVER" : "");
    if (us(cold) > BUDGET_US || warm_avg > 20.0) bad = 1;
}
/* run `n` frames of the current input state; returns the first call's ticks (cold), fills avg/worst of the rest */
static uint64_t run_path(int n, double *avg, double *worst)
{
    uint64_t first = step(), sum = 0, w = 0;
    for (int i = 1; i < n; i++) { uint64_t t = step(); sum += t; if (t > w) w = t; }
    *avg = n > 1 ? us(sum) / (n - 1) : 0; *worst = us(w);
    return first;
}
int main(int argc, char **argv)
{
    (void)argv;
    int enforce = argc > 1;   /* `filter_bench prewarm`: run prewarm_filter() first and enforce the budget (what the deck runs) */
#if defined(__aarch64__) && defined(__linux__)
    frq = cnt_frq(); if (!frq) frq = 1000000000ull;
#else
    frq = 1000000000ull;
#endif
    /* without arguments NO prewarm_filter(): the first call of each path is what a translation-cache flush
     * (qemu) or a page-out (deck) would make slow; informational. With an argument the mod's init
     * prewarm runs first and the budget is enforced. */
    if (enforce) prewarm_filter();
    FS.cfg = CFG_ENABLE | CFG_CLEAR | CFG_HOTCUE;
    FS.rp_frames = 40;
    double a, w; uint64_t c;
    c = run_path(2000, &a, &w); report("idle (fast path)", c, a, w);
    clear_in(); ipad[0] = 1; c = run_path(2000, &a, &w); report("plain pad held", c, a, w);
    clear_in(); step();
    ish[0] = 1; c = run_path(2000, &a, &w); report("SHIFT held", c, a, w);
    imm[0] = 1; c = run_path(2000, &a, &w); report("SHIFT+MEMORY held", c, a, w);
    idl[0] = 1; imm[0] = 0; c = run_path(2000, &a, &w); report("SHIFT+DELETE held", c, a, w);
    clear_in(); step(); ish[1] = 1; ipad[1] = 4; c = run_path(2000, &a, &w); report("SHIFT+pad held (side B)", c, a, w);
    clear_in(); step();
    idl[0] = 1; c = run_path(2000, &a, &w); report("DELETE held alone", c, a, w);
    ipad[0] = 8; c = run_path(2000, &a, &w); report("DELETE+pad held (rewrite)", c, a, w);
    clear_in(); c = run_path(2000, &a, &w); report("DELETE released after pad", c, a, w);
    clear_in(); step(); idl[1] = 1; step(); clear_in(); c = run_path(60, &a, &w); report("DELETE tap replay (40 frames)", c, a, w);
    for (int i = 0; i < 100; i++) step();
    __atomic_store_n(&FS.req, RQ_VALID | 0x04u, __ATOMIC_RELAXED);
    c = run_path(60, &a, &w); report("SHIFT+pad replay request", c, a, w);
    clear_in(); step(); ish[0] = 1; step(); ipad[0] = 2; step(); ish[0] = 0;
    c = run_path(2000, &a, &w); report("pad held after SHIFT up (eaten)", c, a, w);
    clear_in(); step(); FS.emp = 0x08u; idl[0] = 1; step(); ipad[0] = 8;
    c = run_path(2000, &a, &w); report("DELETE+pad on an EMPTY pad", c, a, w);
    clear_in(); step(); FS.emp = 0;
    /* a long mixed burst: worst call over 200k frames of changing input */
    uint64_t worst = 0, sum = 0; const int R = 200000;
    for (int i = 0; i < R; i++) {
        clear_in(); ish[0] = (i >> 3) & 1; idl[i & 1] = (i >> 4) & 1; ipad[0] = (i >> 2) & 7; imm[1] = (i >> 5) & 1;
        uint64_t t = step(); sum += t; if (t > worst) worst = t;
    }
    printf("%-34s avg %7.3f us   worst %8.2f us over %d frames\n", "mixed burst", us(sum) / R, us(worst), R);
    if (us(worst) > BUDGET_US * 5) bad = 1;
    printf("filter_bench (%s): %s\n", enforce ? "after prewarm_filter, budget enforced" : "cold, informational", bad ? "OVER BUDGET" : "ok");
    return enforce ? bad : 0;
}
