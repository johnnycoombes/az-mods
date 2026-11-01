/* active-loop 0.2.13 regression: DELETE + hot-cue-loop pad at the DECK's frame rate (~1000 ERP frames/s).
 *
 * Johnny's 0.2.12 deck report: holding DELETE and pressing a loop pad removed nothing. Every emulator test had passed, because the
 * emulator serves ~17 frames/s: the filter's "SHIFT leads the pad by one frame" was 60+ ms there and ~1 ms on the deck. This test
 * drives the real filter (on_erp) on a virtual 1 ms-per-frame clock with a human's DELETE + pad and judges the OUTPUT the way stock
 * would see it, with a model of "what a hand does": a stock SHIFT+pad has SHIFT down for at least LEAD_MIN_MS before the pad goes
 * down, and the pad stays down at least PRESS_MIN_MS. Also: DELETE never reaches stock, the pad goes down exactly once per
 * DELETE+pad, a tap shorter than the lead is still delivered, and a plain pad pressed just after the DELETE+pad is held back
 * until our SHIFT is gone (else stock would read it as SHIFT+pad = delete another hot cue).
 *
 * Build against another source to see the old behaviour:  cc -DAL_SRC='"/path/active-loop-0.2.12.c"' ... (0.2.12: FAILS at 1000 fps). */
#define _GNU_SOURCE
#include <time.h>
#include <stdint.h>
#include <stdio.h>
static uint64_t VNOW;
static inline int fake_cg(struct timespec *t) { t->tv_sec = (time_t)(VNOW / 1000000000ull); t->tv_nsec = (long)(VNOW % 1000000000ull); return 0; }
#define clock_gettime(c, t) fake_cg(t)
#ifndef AL_SRC
#define AL_SRC "../active-loop.c"
#endif
#include AL_SRC
#include <assert.h>
#include <stdlib.h>
#define LEAD_MIN_MS  30     /* what a hand gives stock: SHIFT well before the pad (a human is 100+ ms) */
#define PRESS_MIN_MS 30     /* the shortest pad press stock can be expected to take */
static unsigned char F[ERP_LEN];
static int FPS;
static uint64_t P;
static unsigned char in_sh, in_dl, in_pad;           /* side A inputs */
static long fnow;                                    /* frames since start */
/* the stock-side view of side A (the output of the filter) */
static struct { long sh_up, pad_up, pad_dn; int pads, dls; long lead_ms, press_ms; int lead_ok, press_ok; int shift_with_pad; } M;
static int pad_prev, sh_prev;
static void frame(void)
{
    memset(F, 0, sizeof F);
    if (in_sh) F[A_SH_O] |= 1u << A_SH_B;
    if (in_dl) F[A_DEL_O] |= 1u << A_DEL_B;
    F[A_PAD_O] = in_pad;
    VNOW += P;
    on_erp(AZ_BUS_ERP_IN, F, ERP_LEN, NULL);
    fnow++;
    int sh = (F[A_SH_O] >> A_SH_B) & 1, pad = F[A_PAD_O] != 0, dl = (F[A_DEL_O] >> A_DEL_B) & 1;
    if (dl) M.dls++;
    if (sh && !sh_prev) M.sh_up = fnow;
    if (pad && !pad_prev) {
        M.pads++; M.pad_up = fnow;
        if (sh) { M.lead_ms = (fnow - M.sh_up) * 1000 / FPS; M.lead_ok = M.lead_ms >= LEAD_MIN_MS; }
        else M.lead_ok = 0;
        M.shift_with_pad = sh;
    }
    if (!pad && pad_prev) { M.press_ms = (fnow - M.pad_up) * 1000 / FPS; M.press_ok = M.press_ms >= PRESS_MIN_MS; }
    pad_prev = pad; sh_prev = sh;
}
static void run_ms(int ms) { long n = (long)ms * FPS / 1000; while (n-- > 0) frame(); }
static void clear_m(void) { memset(&M, 0, sizeof M); pad_prev = sh_prev = 0; }
/* the tick's calibration, as poll_filter does it */
static void calibrate(void)
{
    FS.rp_frames = rate_frames(FS.fi, cnt_hz);
#ifdef LEAD_MS_MIN
    FS.lead_frames = rate_lead(FS.fi, cnt_hz, LEAD_MS_MIN);
#endif
}
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("  [%s]\n", #c); } } while (0)
int main(int argc, char **argv)
{
    FPS = argc > 1 ? atoi(argv[1]) : 1000;
    if (FPS < 100) { printf("deletepad_deck_test: skipped at %d frames/s (a model of the DECK's rate; the emulator's 17/s is covered by filter_test and filter_fuzz)\n", FPS); return 0; }
    P = 1000000000ull / (uint64_t)FPS;
#ifdef LEAD_MS_MIN
    assert(rate_lead(1000000, 1000000000ull, 40) == 40 && rate_lead(58823529, 1000000000ull, 40) == 1 && rate_lead(8000000, 1000000000ull, 40) == 5 &&
           rate_lead(1000000, 1000000000ull, 160) == 120 && rate_lead(0, 1000000000ull, 40) == 1);
#endif
    prewarm_filter();
    FS.cfg = CFG_ENABLE | CFG_CLEAR | CFG_HOTCUE;
    FS.emp = 0;
    run_ms(2500 > 40 * 1000 / FPS ? 2500 : 40 * 1000 / FPS);          /* idle: the filter measures the frame interval */
    calibrate();
    printf("deletepad_deck_test at %d frames/s: fi=%u ticks, replay %u frames, lead %u frames\n", FPS, FS.fi, FS.rp_frames,
#ifdef LEAD_MS_MIN
           FS.lead_frames);
#else
           1u);
#endif
    /* 1. a hand: DELETE down, 150 ms later the pad down for 90 ms, pad up, DELETE up 60 ms later */
    clear_m(); in_dl = 1; run_ms(150); in_pad = 4; run_ms(90); in_pad = 0; run_ms(60); in_dl = 0; run_ms(300);
    CHECK(M.pads == 1, "the pad went down %d times at stock (want exactly 1)", M.pads);
    CHECK(M.dls == 0, "DELETE reached stock %d frames", M.dls);
    CHECK(M.shift_with_pad, "the pad went down without SHIFT");
    CHECK(M.lead_ok, "SHIFT led the pad by %ld ms (want >= %d ms: ~1 frame is %d ms at this rate)", M.lead_ms, LEAD_MIN_MS, (int)(1000 / FPS));
    CHECK(M.press_ok, "the pad stayed down %ld ms (want >= %d)", M.press_ms, PRESS_MIN_MS);
    printf("  hand 150/90 ms: SHIFT led by %ld ms, pad down %ld ms, pads %d, DELETE frames %d\n", M.lead_ms, M.press_ms, M.pads, M.dls);
    /* 2. a quick tap, shorter than the lead: still delivered as a whole press */
    clear_m(); in_dl = 1; run_ms(100); in_pad = 8; run_ms(15); in_pad = 0; run_ms(60); in_dl = 0; run_ms(300);
    CHECK(M.pads == 1 && M.dls == 0, "15 ms tap: pads %d dls %d (want 1, 0)", M.pads, M.dls);
    CHECK(M.lead_ok && M.press_ok, "15 ms tap: lead %ld ms, press %ld ms", M.lead_ms, M.press_ms);
    printf("  15 ms tap: SHIFT led by %ld ms, pad down %ld ms, pads %d\n", M.lead_ms, M.press_ms, M.pads);
    /* 3. DELETE released before the pad is up (pad held on): same, no DELETE leak, no second press at stock */
    clear_m(); in_dl = 1; run_ms(120); in_pad = 16; run_ms(40); in_dl = 0; run_ms(200); in_pad = 0; run_ms(300);
    CHECK(M.pads == 1 && M.dls == 0, "DELETE up first: pads %d dls %d (want 1, 0)", M.pads, M.dls);
    /* 4. DELETE alone is still a tap replay (plain DELETE reaches stock) */
    clear_m(); in_dl = 1; run_ms(100); in_dl = 0; run_ms(400);
    CHECK(M.dls > 0 && M.pads == 0, "DELETE alone: dls %d pads %d (want a tap, no pad)", M.dls, M.pads);
    /* 5. a plain pad pressed right after the DELETE+pad (DELETE already up) must not meet our SHIFT */
    clear_m(); in_dl = 1; run_ms(100); in_pad = 4; run_ms(50); in_pad = 0; in_dl = 0; run_ms(10); in_pad = 1; run_ms(200); in_pad = 0; run_ms(300);
    CHECK(M.dls == 0, "DELETE leaked (%d)", M.dls);
    {   /* walk it again, looking at the pad that is NOT pad 3 (bit 4): did bit 1 ever go down while SHIFT was up? */
        clear_m(); int bad = 0, pl = 0;
        in_dl = 1; run_ms(100); in_pad = 4; run_ms(50); in_pad = 0; in_dl = 0; run_ms(10); in_pad = 1;
        for (int i = 0; i < 400 * FPS / 1000; i++) { frame(); int sh = (F[A_SH_O] >> A_SH_B) & 1; if ((F[A_PAD_O] & 1) && sh) bad++; if (F[A_PAD_O] & 1) pl++; }
        in_pad = 0; run_ms(300);
        CHECK(bad == 0, "a plain pad 1 reached stock under SHIFT on %d frames (= SHIFT+pad: it would delete that hot cue)", bad);
        CHECK(pl > 0 || FPS < 125, "the plain pad 1 was never delivered at all");
    }
    printf("deletepad_deck_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
