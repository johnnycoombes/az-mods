/* 0.2.2: engage-check hot path cost + rare-path gating, and toast coalescing (host). */
#include "../active-loop.c"
#include <assert.h>
static int orig_n;
static void fake_orig(void *h, void *p, void *n, unsigned j) { (void)h; (void)p; (void)n; (void)j; orig_n++; }
static uint8_t H[512] __attribute__((aligned(16))), P[128] __attribute__((aligned(16))), N[128] __attribute__((aligned(16)));
int main(void){
    al_live = 1;   /* 0.2.6: hooks are pass-through until init() completes */
    orig_echeck = fake_orig;
    for (int d = 0; d < NDECK; d++) { pos_now[d] = POS_UNSET; arm_in[d] = INT64_MAX; }
    hold_pub[1] = H;
    cfg_bits = CFG_ENABLE | CFG_AUTOADV;            /* REARM off: rare path never derefs */
    assert(!pos_seen(1));
    uint64_t worst = 0, t0 = now_ns();
    const int R = 2000000;
    for (int i = 0; i < R; i++) {
        *(int64_t *)(P + 24) = i; *(int64_t *)(N + 24) = i + 1;
        uint64_t a = now_ns(); my_echeck(H, P, N, 0); uint64_t dt = now_ns() - a;
        if (i > 100 && dt > worst) worst = dt;
    }
    double avg = (now_ns() - t0) / (double)R;
    printf("echeck hot path: avg %.0f ns/call (incl. 2 clock reads + stub), worst %llu ns\n", avg, (unsigned long long)worst);
    assert(orig_n == R && pos_now[1] == R && pos_seen(1) && hmax_ec_rare == 0);
    assert(src_ok[1] == 1);                          /* 0.2.3: source key captured once */
    (void)take0(&hmax_ec_own); assert(hmax_ec_own == 0);
    /* crossing the arm point enters the rare path once */
    arm_in[1] = 5000000;
    *(int64_t *)(P + 24) = 4999999; *(int64_t *)(N + 24) = 5000001; my_echeck(H, P, N, 0);
    *(int64_t *)(P + 24) = 5000001; *(int64_t *)(N + 24) = 5000002; my_echeck(H, P, N, 0);
    printf("rare path entered: %s\n", hmax_ec_rare ? "yes" : "no");
    assert(hmax_ec_rare != 0);
    /* unknown holder: untouched */
    my_echeck(P, P, N, 0); assert(pos_now[0] == POS_UNSET);
    /* toasts: newest replaces pending; stale ready for deck 1 dropped when deck 2 loads */
    cfg_bits |= CFG_TOAST;
    do_toast_ms("Active Loop ready (deck 1)", 2500, 0);
    do_toast_ms("pick A", 1500, -1); do_toast_ms("pick B", 1500, -1);
    assert(toast_pending && !strcmp(toast_text, "pick B"));
    do_toast_ms("Active Loop ready (deck 1)", 2500, 0); ready_tick[0][0] = 99;
    ready_drop_others(1);
    assert(!toast_pending && ready_tick[0][0] == 0);
    printf("toast coalesce/drop: ok\n");
    return avg < 50000 ? 0 : 1;
}
