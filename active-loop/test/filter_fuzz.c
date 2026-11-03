/* active-loop 0.2.8 (al028d): random-walk fuzz of the frame filter against a model, on a VIRTUAL clock so the frame rate is a
 * parameter (emulator 17 frames/s, 125/s, deck 1000/s). Both sides, random SHIFT / DELETE / pad toggles, random stalls
 * (the bus thread not running for 8..40 frame periods), 10 % frame-interval jitter, and a random "empty pads" mask
 * (what the tick publishes while DELETE is held). Fails (exit 1) if:
 *   - a DELETE bit reaches stock outside a legitimate tap replay window;
 *   - a pad rises on the output with SHIFT but without SHIFT having been on for at least lead_frames output frames before (0.2.13:
 *     a 40 ms lead at the deck's rate, 1 frame at the emulator's; it was one frame everywhere before, ~1 ms on the deck);
 *   - a pad that went down under SHIFT (or on an empty pad under DELETE) reaches stock before it is released;
 *   - a clean DELETE hold (no pad, no SHIFT, no stall) released alone does NOT start a tap on the release frame;
 *   - a hold with a stall in it is replayed anyway;
 *   - the filter's frame-interval estimate is not within 0.6..1.2 of the period, or the replay length is not the 40 ms
 *     frame count for that rate (within +15 %).
 * Usage: filter_fuzz [fps] [seed] [1/flip] [frames] */
#define _GNU_SOURCE
#include <time.h>
#include <stdint.h>
static uint64_t VNOW;                                   /* virtual ns */
static inline int fake_cg(struct timespec *t) { t->tv_sec = (time_t)(VNOW / 1000000000ull); t->tv_nsec = (long)(VNOW % 1000000000ull); return 0; }
#define clock_gettime(c, t) fake_cg(t)
#include "../active-loop.c"
#include <stdlib.h>
static unsigned char F[ERP_LEN];
static uint64_t rnd_s = 88172645463325252ull;
static unsigned rnd(void) { rnd_s ^= rnd_s << 13; rnd_s ^= rnd_s >> 7; rnd_s ^= rnd_s << 17; return (unsigned)(rnd_s >> 20); }
int main(int argc, char **argv)
{
    int fps = argc > 1 ? atoi(argv[1]) : 125;
    rnd_s ^= (uint64_t)(argc > 2 ? atoi(argv[2]) : 1) * 0x9E3779B97F4A7C15ull;
    int pflip = argc > 3 ? atoi(argv[3]) : 30;
    long nfr = argc > 4 ? atol(argv[4]) : 2000000;
    const uint64_t P = 1000000000ull / (uint64_t)fps;
    prewarm_filter();
    FS.cfg = CFG_ENABLE | CFG_CLEAR | CFG_HOTCUE;
    long lead_short = 0; unsigned shrun[2] = {0, 0};
    long bad = 0, frames = 0, lead_bad = 0, eat_bad = 0, tapmiss = 0, tapstall = 0, taps = 0, stalls = 0, fi_bad = 0, rp_bad = 0, swallowed = 0;
    int ish[2] = {0}, idl[2] = {0}, ipad[2] = {0};
    int dpad[2] = {0}, dsh[2] = {0}, dstall[2] = {0}; long tapend[2] = {-1, -1};
    int prevdl[2] = {0}, prevsh[2] = {0}, prevpad[2] = {0};
    unsigned eaten[2] = {0}, empm[2] = {0};
    unsigned char pp[2] = {0}, ps[2] = {0};
    long rp = 0;
    long gapf = 1, shfall[2] = {-1000000, -1000000}, quiet[2] = {0, 0}, gap_bad = 0, stuck_bad = 0, tap_held = 0; unsigned char sho_prev[2] = {0, 0};
    for (long fr = -200; fr < nfr; fr++) {
        int warm = fr < 0;
        int s = rnd() & 1;
        if (!warm) {
            int r = (int)(rnd() % (unsigned)pflip);
            if (r == 0) ish[s] ^= 1; else if (r == 1) idl[s] ^= 1; else if (r == 2) ipad[s] = ipad[s] ? 0 : (1 << (rnd() % 8));
            if ((rnd() % 50) == 0) { FS.emp = (rnd() & 0xff) | ((rnd() & 0xff) << 8); if (rnd() & 1) FS.emp = 0; }   /* the tick republishes */
        }
        /* time: normal frames 0.9..1.1 P; a stall now and then (never during the 200 warm-up frames) */
        int stall = 0;
        uint64_t step = P * (900 + rnd() % 201) / 1000;
        if (!warm && (rnd() % 120) == 0) { step = P * (8 + rnd() % 33); stall = 1; stalls++; }
        VNOW += step;
        if (fr == 0) {                                  /* calibrated: the replay length is the 40 ms frame count for this rate */
            uint64_t fi = FS.fi;
            if (fi * 10 < P * 6 || fi * 10 > P * 12) { fi_bad++; printf("fi %llu vs P %llu after warm-up\n", (unsigned long long)fi, (unsigned long long)P); }
            FS.rp_frames = rate_frames(fi, cnt_hz);
            unsigned want = (unsigned)(fps / 25 + (fps % 25 >= 13)); if (want < 6) want = 6; if (want > 200) want = 200;
            /* the estimate sits at 0.9..1.0 of the period, so the frame count is 1.0..1.15 x the nominal one (6 is a floor) */
            if (FS.rp_frames < want || FS.rp_frames > (want * 125 + 99) / 100 + 1) { rp_bad++; printf("rp_frames %u want %u\n", FS.rp_frames, want); }
            rp = FS.rp_frames;
            FS.lead_frames = rate_lead(fi, cnt_hz, LEAD_MS_MIN);           /* 0.2.13: the tick's lead, 40 ms in frames */
            gapf = (long)((uint64_t)cnt_hz * GAP_MS / 1000u / fi); FS.gap_frames = (uint32_t)gapf;   /* 0.2.15: 300 ms in frames */
            { double ms = FS.lead_frames * (double)P / 1e6;                  /* >= 1 frame; at the deck rate it must really be ~40 ms */
              if (FS.lead_frames < 1 || (fps >= 125 && (ms < 35.0 || ms > 50.0))) { rp_bad++; printf("lead_frames %u = %.1f ms at %d fps\n", FS.lead_frames, ms, fps); } }
        }
        memset(F, 0, sizeof F);
        for (int k = 0; k < 2; k++) {
            if (ish[k]) F[SO[k].sh_o] |= 1u << SO[k].sh_b;
            if (idl[k]) F[SO[k].del_o] |= 1u << SO[k].del_b;
            F[SO[k].pad_o] |= ipad[k];
        }
        unsigned in_pad[2] = { (unsigned)ipad[0], (unsigned)ipad[1] };
        unsigned mask_now = FS.emp;
        int sh_before[2] = { FS.sh_on[0], FS.sh_on[1] };
        on_erp(0, F, ERP_LEN, 0); frames++;
        for (int k = 0; k < 2; k++) {
            /* model: DELETE hold */
            if (idl[k] && !prevdl[k]) { dpad[k] = ipad[k] != 0; dsh[k] = ish[k]; dstall[k] = 0; }
            if (idl[k] && ipad[k]) dpad[k] = 1;
            if (idl[k] && ish[k]) dsh[k] = 1;
            if ((idl[k] || prevdl[k]) && stall) dstall[k] = 1;      /* the DELETE-down frame and the release frame count too */
            int released = !idl[k] && prevdl[k];
            int expect_tap = released && !dpad[k] && !dsh[k] && !ish[k] && !dstall[k];
            if (expect_tap) { tapend[k] = fr + rp + 2; taps++; }
            if (released && !expect_tap && !ish[k] && !dpad[k] && !dsh[k] && dstall[k]) tapstall++;
            /* model: pads that must stay eaten until released */
            unsigned rose = in_pad[k] & ~(unsigned)prevpad[k];
            if (ish[k]) eaten[k] |= rose;
            else if (idl[k] && ((mask_now >> (8 * k)) & rose & ~eaten[k])) { eaten[k] |= (mask_now >> (8 * k)) & rose; swallowed++; }
            eaten[k] &= in_pad[k];
            int out = (F[SO[k].del_o] >> SO[k].del_b) & 1;
            unsigned char np = F[SO[k].pad_o]; int so = (F[SO[k].sh_o] >> SO[k].sh_b) & 1;
            if ((np & ~pp[k]) && so && !ps[k]) { lead_bad++; if (lead_bad < 5) printf("lead bad frame %ld side %d\n", fr, k); }
            if ((np & ~pp[k]) && so && shrun[k] < FS.lead_frames) { lead_short++; if (lead_short < 5) printf("lead short: pad after %u SHIFT frames (< %u) frame %ld side %d\n", shrun[k], FS.lead_frames, fr, k); }
            shrun[k] = so ? shrun[k] + 1 : 0;
            if (np & eaten[k] & ~(unsigned)FS.rp[k]) {   /* a bit the SHIFT+pad replay is driving is not a leak */ eat_bad++; if (eat_bad < 5) printf("eaten pad leaked: frame %ld side %d out %02x eaten %02x ish=%d idl=%d\n", fr, k, np, eaten[k], ish[k], idl[k]); }
            pp[k] = np; ps[k] = (unsigned char)so;
            if (out && !(fr <= tapend[k])) { bad++; if (bad < 8) printf("bad: frame %ld side %d idl=%d ish=%d ipad=%d dpad=%d dsh=%d dstall=%d\n", fr, k, idl[k], ish[k], ipad[k], dpad[k], dsh[k], dstall[k]); }
            /* 0.2.15: the synthetic SHIFT never comes down again within 300 ms of its last release, and is never left down */
            if (!ish[k] && !prevsh[k]) {
                if (so && !sho_prev[k] && fr - shfall[k] < gapf - 1) { gap_bad++; if (gap_bad < 5) printf("SHIFT down again %ld frames after its release (gap %ld) frame %ld side %d\n", fr - shfall[k], gapf, fr, k); }
                if (!so && sho_prev[k]) shfall[k] = fr;
                quiet[k] = (!idl[k] && !ipad[k] && !stall) ? quiet[k] + 1 : 0;
                if (quiet[k] > 2 * gapf + (long)rp + 60 && so) { stuck_bad++; if (stuck_bad < 5) printf("SHIFT stuck down with everything up: frame %ld side %d\n", fr, k); }
            } else quiet[k] = 0;
            sho_prev[k] = (unsigned char)so;
            if (expect_tap && !out && sh_before[k]) { tap_held++; }       /* 0.2.15: our SHIFT is still down: a DELETE tap now would be SHIFT+DELETE, so it is not replayed */
            else if (expect_tap && !out) { tapmiss++; if (tapmiss < 8) printf("tap not started: frame %ld side %d\n", fr, k); }
            if (dstall[k] && released && out && fr > tapend[k]) { tapmiss++; }
            prevdl[k] = idl[k]; prevsh[k] = ish[k]; prevpad[k] = (int)in_pad[k];
        }
    }
    printf("fps %d: frames %ld taps %ld stalls %ld stalled-holds %ld swallowed %ld | bad %ld lead_bad %ld lead_short %ld eaten_bad %ld tapmiss %ld fi_bad %ld rp_bad %ld | gap_bad %ld stuck_bad %ld taps held back by our SHIFT %ld\n",
           fps, frames, taps, stalls, tapstall, swallowed, bad, lead_bad, lead_short, eat_bad, tapmiss, fi_bad, rp_bad, gap_bad, stuck_bad, tap_held);
    return (bad || lead_bad || lead_short || eat_bad || tapmiss || fi_bad || rp_bad || gap_bad || stuck_bad || !taps || !stalls || !tapstall) ? 1 : 0;
}
