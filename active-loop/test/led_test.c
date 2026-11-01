/* 0.2.12 (was 0.2.11): the breathing armed pad (host). Checks: the level curve (range, floor, smooth steps, period), on_led writes only the
 * ARMED pad's red channel from the published byte, saved-not-armed pads stay FF0000, HOT CUE gating, dark pads stay dark, stock
 * dim cell keeps 12, pulse off = constant 255, other bytes untouched, and a printed table of the curve. */
#include "../active-loop.c"
#include <assert.h>
static void setf(unsigned char *f, int s, int k, int r, int g, int b)
{ unsigned char *c = f + LED_PAD_OFF + 4 * k + s * LED_SIDE; c[0] = r; c[1] = g; c[2] = b; c[3] = 0; }
int main(void)
{
    /* 1. the curve (0.2.12: 4 s cycle, floor 0): the real function and the real clock, 10 ms samples over 4.4 s (> one period) */
    int lo = 255, hi = 0, steps = 0, maxstep = 0; double maxrate = 0, tprev = 0; uint32_t prev = 0; int first = 1; int n = 0, z0 = 0, z2 = 0;
    for (int i = 0; i < 440; i++) {
        led_level_update(CFG_PULSE);
        uint32_t v = __atomic_load_n(&led_lvl, __ATOMIC_RELAXED);
        if (v < (uint32_t)lo) lo = (int)v; if (v > (uint32_t)hi) hi = (int)v;
        struct timespec tn; clock_gettime(CLOCK_MONOTONIC, &tn); double tms = tn.tv_sec * 1e3 + tn.tv_nsec / 1e6;
        if (!first) { int d = (int)v - (int)prev; if (d < 0) d = -d; if (d > maxstep) maxstep = d; if (d) steps++;
                      double dt = tms - tprev; if (dt < 5) dt = 5; if (d / dt > maxrate) maxrate = d / dt; }   /* step per ms, so a stalled sleep (tsan, load) cannot fake a jump */
        tprev = tms;
        first = 0; prev = v; n++;
        struct timespec ts = { 0, 10 * 1000 * 1000 }; nanosleep(&ts, NULL);
    }
    printf("curve over %d samples (10 ms): min %d max %d, largest step %d (%.2f/ms), changes %d\n", n, lo, hi, maxstep, maxrate, steps);
    /* dwell: exact over one period (4000 phases of the same formula), not from timed samples (sleep jitter) */
    { int d0 = 0, d2 = 0; for (int i = 0; i < 4000; i++) { float w = 0.5f - 0.5f * cosf(6.2831853f * (i / 4000.0f)), p = powf(w, BREATH_SHAPE);
        int v = (int)(powf(p, 2.2f) * 255.0f + 0.5f); d0 += v == 0; d2 += v <= 2; } z0 = d0 / 40; z2 = d2 / 40; }
    printf("dwell per cycle: byte==0 %d%% (%.2f s), byte<=2 %d%% (%.2f s)\n", z0, z0 * 0.04, z2, z2 * 0.04);
    assert(lo == 0);                       /* reaches full off */
    assert(hi >= 250);                     /* reaches full red */
    assert(maxrate <= 1.2);                /* smooth: at most ~12 of 255 per 10 ms (about 5% of full) */
    assert(z0 <= 9);             /* brief at full off: ~6% of a cycle (0.24 s), no long dark dwell */
    assert(z2 <= 14);            /* and nearly dark (<=2/255) about 11% */
    /* the shape by phase, from the same formula */
    printf("level by phase (tenths 0..10): ");
    int tab[11];
    for (int i = 0; i <= 10; i++) {
        float ph = i / 10.0f, w = 0.5f - 0.5f * cosf(6.2831853f * ph), p = powf(w, BREATH_SHAPE);
        int v = (int)(powf(p, 2.2f) * 255.0f + 0.5f); tab[i] = v < 0 ? 0 : v; printf("%d ", tab[i]);
    }
    printf("\n");
    assert(tab[0] == 0 && tab[10] == 0 && tab[5] == 255);
    for (int i = 1; i <= 5; i++) assert(tab[i] > tab[i - 1] && tab[10 - i] == tab[i]);   /* monotonic up, symmetric down */
    assert(BREATH_PERIOD_NS == 4000000000ull);
    /* pulse off = constant 255 */
    led_level_update(0); assert(__atomic_load_n(&led_lvl, __ATOMIC_RELAXED) == 255);

    /* 2. on_led */
    unsigned char f[128], g[128];
    memset(f, 0, sizeof f);
    for (int s = 0; s < 2; s++) { unsigned char *mb = f + LED_MODE_OFF + s * LED_SIDE; mb[0] = 0x98; mb[1] = 0xff; mb[2] = 0x9a; }  /* HOT CUE lit (full) */
    for (int k = 0; k < 8; k++) setf(f, 0, k, 0x00, 0xff, 0x30);                     /* all pads lit green on side 1 */
    setf(f, 0, 6, 0, 0, 0);                                                            /* pad 7 dark */
    setf(f, 0, 7, 0x00, 0x05, 0x02);                                                   /* pad 8 stock-dim cell */
    memcpy(g, f, sizeof f);
    __atomic_store_n(&led_pub[0], (1u << 1) | (1u << 3) | (1u << 6) | (1u << 7), __ATOMIC_RELAXED);   /* pads 2,4,7,8 red */
    __atomic_store_n(&led_arm[0], (1u << 1) | (1u << 6) | (1u << 7), __ATOMIC_RELAXED);               /* pad 2,7,8 armed (only 2 is a candidate: 4 is saved-only) */
    __atomic_store_n(&led_lvl, 77, __ATOMIC_RELAXED);
    assert(on_led(AZ_BUS_ERP_LED, f, 0x78) == AZ_FRAME_OK);
    unsigned char *c;
    c = f + LED_PAD_OFF + 4 * 1; assert(c[0] == 77 && c[1] == 0 && c[2] == 0);        /* armed: the breath byte */
    c = f + LED_PAD_OFF + 4 * 3; assert(c[0] == 0xff && c[1] == 0 && c[2] == 0);      /* saved, not armed: solid red */
    c = f + LED_PAD_OFF + 4 * 6; assert(!c[0] && !c[1] && !c[2]);                     /* dark stays dark even if armed */
    c = f + LED_PAD_OFF + 4 * 7; assert(c[0] == 12 && c[1] == 0 && c[2] == 0);        /* stock dim cell: 12, not modulated */
    for (int k = 0; k < 8; k++) if (k != 1 && k != 3 && k != 6 && k != 7) assert(!memcmp(f + LED_PAD_OFF + 4 * k, g + LED_PAD_OFF + 4 * k, 4));
    assert(!memcmp(f, g, LED_PAD_OFF));                                                /* nothing before the pads changed */
    assert(!memcmp(f + LED_PAD_OFF + 32, g + LED_PAD_OFF + 32, 0x78 - LED_PAD_OFF - 32));   /* nor side 2 / the rest */
    /* HOT CUE gating: mode button dim (another mode) -> nothing painted at all */
    memcpy(f, g, sizeof f); { unsigned char *mb = f + LED_MODE_OFF; mb[0] = 0x0c; mb[1] = 0x16; mb[2] = 0x0d; }
    memcpy(g, f, sizeof f); on_led(AZ_BUS_ERP_LED, f, 0x78); assert(!memcmp(f, g, sizeof f));
    /* lime secondary page -> nothing painted */
    memcpy(f, g, sizeof f); { unsigned char *mb = f + LED_MODE_OFF; mb[0] = 0x9b; mb[1] = 0xff; mb[2] = 0; }
    memcpy(g, f, sizeof f); on_led(AZ_BUS_ERP_LED, f, 0x78); assert(!memcmp(f, g, sizeof f));
    /* no armed bits and lvl low: saved pads still FF0000 */
    memset(f, 0, sizeof f); { unsigned char *mb = f + LED_MODE_OFF; mb[0] = 0x98; mb[1] = 0xff; mb[2] = 0x9a; }
    setf(f, 0, 2, 0, 0xff, 0x30);
    __atomic_store_n(&led_pub[0], 1u << 2, __ATOMIC_RELAXED); __atomic_store_n(&led_arm[0], 0u, __ATOMIC_RELAXED); __atomic_store_n(&led_lvl, 20, __ATOMIC_RELAXED);
    on_led(AZ_BUS_ERP_LED, f, 0x78); c = f + LED_PAD_OFF + 8; assert(c[0] == 0xff && !c[1] && !c[2]);
    /* side 2 uses the same byte */
    memset(f, 0, sizeof f); { unsigned char *mb = f + LED_MODE_OFF + LED_SIDE; mb[0] = 0x98; mb[1] = 0xff; mb[2] = 0x9a; }
    setf(f, 1, 0, 0, 0xff, 0x30);
    __atomic_store_n(&led_pub[0], 0u, __ATOMIC_RELAXED); __atomic_store_n(&led_pub[1], 1u, __ATOMIC_RELAXED); __atomic_store_n(&led_arm[1], 1u, __ATOMIC_RELAXED); __atomic_store_n(&led_lvl, 33, __ATOMIC_RELAXED);
    on_led(AZ_BUS_ERP_LED, f, 0x78); c = f + LED_PAD_OFF + LED_SIDE; assert(c[0] == 33 && !c[1] && !c[2]);
    /* the bottom of the breath: level 0 = the armed pad's whole cell is 00 00 00 (and its 4th byte stays 0) */
    memset(f, 0, sizeof f); { unsigned char *mb = f + LED_MODE_OFF; mb[0] = 0x98; mb[1] = 0xff; mb[2] = 0x9a; }
    setf(f, 0, 4, 0, 0xff, 0x30);
    __atomic_store_n(&led_pub[0], 1u << 4, __ATOMIC_RELAXED); __atomic_store_n(&led_arm[0], 1u << 4, __ATOMIC_RELAXED); __atomic_store_n(&led_pub[1], 0u, __ATOMIC_RELAXED);
    __atomic_store_n(&led_lvl, 0, __ATOMIC_RELAXED);
    on_led(AZ_BUS_ERP_LED, f, 0x78); c = f + LED_PAD_OFF + 16; assert(!c[0] && !c[1] && !c[2] && !c[3]);
    printf("led_test: ok\n");
    return 0;
}
