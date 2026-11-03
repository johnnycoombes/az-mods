/* active-loop 0.2.15: the filter's synthetic SHIFT (DELETE+pad -> SHIFT, then SHIFT+pad) is ONE hold per DELETE hold and ALWAYS comes up.
 * Johnny's report (0.2.14 = 0.2.13 filter): stop playback, hold DELETE, press a pad: the deck stays in SHIFT mode afterwards.
 * 0.2.13 sent one SHIFT pulse per pad (three pads = three SHIFT taps 60 ms apart: a SHIFT double-tap the deck can read as a lock).
 * Drives the real filter (on_erp) at the deck's ~1000 frames/s and judges the OUTPUT stock sees on side A, over scenarios that
 * include several pads under one DELETE hold, an EMPTY pad, a bus stall in mid-replay and the row being switched off in mid-replay.
 * Hard checks: per DELETE hold stock sees EXACTLY ONE SHIFT press and ONE release however many pads; SHIFT leads the first pad by
 * >= 40 ms and is down for every pad; every pad goes down exactly once; DELETE never reaches stock (row on); no SHIFT edge within 300 ms
 * of the previous one (down time >= 300 ms, up time >= 300 ms before the next hold); after everything is released SHIFT is up within
 * 1 s and stays up. */
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
#include <stdlib.h>
static unsigned char F[ERP_LEN];
static int FPS = 1000;
static uint64_t P;
static unsigned char in_sh, in_dl, in_pad;
static long fnow;
static int out_sh, out_dl, out_pad;
static int edges, press_n, rel_n, min_edge_ms, min_up_ms, sh_prev_out, pad_prev_out, pad_n, pad_noshift, lead_ms_first, pad_prev_bits;
static long last_edge, sh_down_at; static int sh_dn_ms_min;
static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("  FAIL: " __VA_ARGS__); printf("  [%s]\n", #c); } } while (0)
static int dl_leak;
static void frame(void)
{
    memset(F, 0, sizeof F);
    if (in_sh) F[A_SH_O] |= 1u << A_SH_B;
    if (in_dl) F[A_DEL_O] |= 1u << A_DEL_B;
    F[A_PAD_O] = in_pad;
    VNOW += P;
    on_erp(AZ_BUS_ERP_IN, F, ERP_LEN, NULL);
    fnow++;
    out_sh = (F[A_SH_O] >> A_SH_B) & 1; out_dl = (F[A_DEL_O] >> A_DEL_B) & 1; out_pad = F[A_PAD_O];
    if (out_dl) dl_leak++;
    if (out_sh != sh_prev_out) {
        if (last_edge) { int g = (int)((fnow - last_edge) * 1000 / FPS); if (g < min_edge_ms) min_edge_ms = g; if (out_sh && g < min_up_ms) min_up_ms = g; }
        last_edge = fnow; edges++;
        if (out_sh) { press_n++; sh_down_at = fnow; } else { rel_n++; }
    }
    { int nb = out_pad & ~pad_prev_bits;                       /* pad bits that went down at stock this frame */
      for (int b = 0; b < 8; b++) if (nb & (1 << b)) { pad_n++; if (!out_sh) pad_noshift++; else if (lead_ms_first < 0) lead_ms_first = (int)((fnow - sh_down_at) * 1000 / FPS); } }
    pad_prev_bits = out_pad; sh_prev_out = out_sh;
}
static void run_ms(int ms) { long n = (long)ms * FPS / 1000; while (n-- > 0) frame(); }
static void begin(void) { edges = press_n = rel_n = pad_n = pad_noshift = 0; min_edge_ms = min_up_ms = 100000; last_edge = 0; sh_prev_out = pad_prev_out = pad_prev_bits = 0; dl_leak = 0; lead_ms_first = -1; }
/* all real buttons up: SHIFT must be up at stock within 1 s and STAY up for the next 600 ms */
static void settle(const char *name, int want_holds, int want_pads)
{
    in_sh = in_dl = 0; in_pad = 0;
    run_ms(1000);
    int stuck = 0;
    for (int i = 0; i < 600 * FPS / 1000; i++) { frame(); if (out_sh) stuck++; }
    CHECK(stuck == 0, "%s: SHIFT still down at stock %d frames after everything was released", name, stuck);
    CHECK(dl_leak == 0, "%s: DELETE reached stock (%d frames)", name, dl_leak);
    CHECK(press_n == want_holds && rel_n == want_holds, "%s: %d SHIFT presses / %d releases at stock (want exactly %d / %d)", name, press_n, rel_n, want_holds, want_holds);
    CHECK(pad_n == want_pads, "%s: %d pad presses at stock (want %d)", name, pad_n, want_pads);
    CHECK(pad_noshift == 0, "%s: %d pad presses reached stock without SHIFT down", name, pad_noshift);
    CHECK(edges < 2 || min_edge_ms >= 300, "%s: two SHIFT edges only %d ms apart (want >= 300)", name, min_edge_ms);
    CHECK(want_pads == 0 || lead_ms_first >= 38, "%s: SHIFT led the first pad by %d ms (want >= 40)", name, lead_ms_first);
    printf("  %-34s SHIFT presses %d, releases %d, pads %d, shortest time between SHIFT edges %s%d ms, first-pad lead %d ms\n", name, press_n, rel_n, pad_n,
           edges < 2 ? "-" : "", edges < 2 ? 0 : min_edge_ms, lead_ms_first);
}
int main(void)
{
    P = 1000000000ull / (uint64_t)FPS;
    prewarm_filter();
    FS.cfg = CFG_ENABLE | CFG_CLEAR | CFG_HOTCUE;
    FS.emp = 0;
    run_ms(2500);
    FS.rp_frames = rate_frames(FS.fi, cnt_hz);
    FS.lead_frames = rate_lead(FS.fi, cnt_hz, 40);
    FS.gap_frames = (uint32_t)((uint64_t)cnt_hz * GAP_MS / 1000u / FS.fi);
    printf("shift_release_test at %d frames/s: replay %u frames, lead %u frames, gap %u frames\n", FPS, FS.rp_frames, FS.lead_frames, FS.gap_frames);
    /* A. one pad under DELETE (stopped deck: the filter does not know or care) */
    begin(); in_dl = 1; run_ms(150); in_pad = 4; run_ms(90); in_pad = 0; run_ms(60); in_dl = 0; settle("A one pad", 1, 1);
    /* B. three pads one after the other under one DELETE hold: ONE SHIFT press, ONE release */
    begin(); in_dl = 1; run_ms(150);
    for (int k = 0; k < 3; k++) { in_pad = (unsigned char)(1u << k); run_ms(80); in_pad = 0; run_ms(60); }
    in_dl = 0; settle("B three pads, one hold", 1, 3);
    /* B2. five quick pads (30 ms presses, 20 ms apart) under one hold */
    begin(); in_dl = 1; run_ms(100);
    for (int k = 0; k < 5; k++) { in_pad = (unsigned char)(1u << k); run_ms(30); in_pad = 0; run_ms(20); }
    in_dl = 0; settle("B2 five quick pads", 1, 5);
    /* B3. two pads pressed together, then a third while the first two are still down */
    begin(); in_dl = 1; run_ms(100); in_pad = 3; run_ms(50); in_pad = 7; run_ms(80); in_pad = 0; in_dl = 0; settle("B3 chord then third pad", 1, 3);
    /* C. pad pressed and DELETE released before the pad comes up */
    begin(); in_dl = 1; run_ms(120); in_pad = 16; run_ms(40); in_dl = 0; run_ms(200); in_pad = 0; settle("C DELETE up first", 1, 1);
    /* D. two DELETE holds only 150 ms apart: SHIFT is still down (300 ms minimum), so the second hold JOINS it: still one press, one release */
    begin(); in_dl = 1; run_ms(100); in_pad = 1; run_ms(60); in_pad = 0; in_dl = 0; run_ms(150); in_dl = 1; run_ms(60); in_pad = 2; run_ms(60); in_pad = 0; in_dl = 0; settle("D two holds 150 ms apart", 1, 2);
    /* D2. two holds 450 ms apart: two SHIFT holds; the second waits until 300 ms after the release (its pad is eaten meanwhile, delivered once) */
    begin(); in_dl = 1; run_ms(100); in_pad = 1; run_ms(60); in_pad = 0; in_dl = 0; run_ms(450); in_dl = 1; run_ms(60); in_pad = 2; run_ms(90); in_pad = 0; in_dl = 0; run_ms(300);
    CHECK(min_up_ms >= 300, "D2: SHIFT came down again only %d ms after a release", min_up_ms);
    settle("D2 two holds 450 ms apart", 2, 2);
    /* E. EMPTY pad under DELETE (tick published emp): no SHIFT at all */
    begin(); FS.emp = 0x04; in_dl = 1; run_ms(100); in_pad = 4; run_ms(90); in_pad = 0; in_dl = 0; settle("E empty pad", 0, 0); FS.emp = 0;
    /* F. a bus stall (no frames for 20 ms) in the middle of the hold: SHIFT is released at once, the pad is not leaked, DELETE never reaches stock */
    begin(); in_dl = 1; run_ms(100); in_pad = 2; run_ms(60); VNOW += 25000000ull; run_ms(50); in_pad = 0; in_dl = 0;
    in_sh = in_dl = 0; run_ms(1000); { int stuck = 0; for (int i = 0; i < 600; i++) { frame(); if (out_sh) stuck++; }
      CHECK(stuck == 0, "F stall mid-hold: SHIFT stuck %d frames", stuck); CHECK(dl_leak == 0, "F stall: DELETE leaked %d", dl_leak);
      CHECK(press_n == rel_n, "F stall: %d presses vs %d releases", press_n, rel_n);
      printf("  %-34s SHIFT presses %d, releases %d (a stall aborts the hold)\n", "F stall mid-hold", press_n, rel_n); }
    /* G. a second hold right after an aborted one: the gap rule still holds */
    begin(); in_dl = 1; run_ms(100); in_pad = 2; run_ms(60); VNOW += 25000000ull; run_ms(10); in_pad = 0; run_ms(10); in_pad = 4; run_ms(100); in_pad = 0; in_dl = 0;
    in_sh = 0; run_ms(1000); { int stuck = 0; for (int i = 0; i < 600; i++) { frame(); if (out_sh) stuck++; }
      CHECK(stuck == 0, "G stall then pad: SHIFT stuck %d frames", stuck); CHECK(press_n == rel_n, "G: %d presses vs %d releases", press_n, rel_n);
      CHECK(min_up_ms >= 300, "G: SHIFT came down again only %d ms after a release (want >= 300)", min_up_ms);
      printf("  %-34s SHIFT presses %d, releases %d, shortest time SHIFT stayed up %d ms\n", "G stall then another pad", press_n, rel_n, min_up_ms); }
    /* H. the row is switched off while a hold is open: SHIFT is released at once and stays up (DELETE reaches stock by design) */
    begin(); in_dl = 1; run_ms(100); in_pad = 1; run_ms(20); FS.cfg = 0; run_ms(30); in_pad = 0; in_dl = 0; dl_leak = 0;
    { in_sh = 0; run_ms(1000); int stuck = 0; for (int i = 0; i < 600; i++) { frame(); if (out_sh) stuck++; }
      CHECK(stuck == 0, "H row off mid-hold: SHIFT stuck %d frames", stuck); CHECK(press_n == rel_n, "H: %d presses vs %d releases", press_n, rel_n);
      printf("  %-34s SHIFT presses %d, releases %d\n", "H row off mid-hold", press_n, rel_n); }
    FS.cfg = CFG_ENABLE | CFG_CLEAR | CFG_HOTCUE;
    /* I. the 0.2.13 escalation is gone: a pad that stock never deletes does not lengthen the lead (lead_ms fixed) */
    CHECK(lead_ms == 40, "lead_ms is %u (want a fixed 40)", lead_ms);
    printf("shift_release_test: %s\n", fails ? "FAILED" : "ok");
    return fails ? 1 : 0;
}
