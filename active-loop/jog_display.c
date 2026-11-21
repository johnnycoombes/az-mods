/* SPDX-License-Identifier: GPL-3.0-only */
/* Active Loop jog display (0.2.19): format, bar.beat, throttle/restore plan, 181×181 render.
 * Included from active-loop.c (one translation unit). Pure helpers are unit-tested. */
#include "jog_font.h"
#include "jog_jpeg.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

enum { JOG_W = 181, JOG_H = 181, JOG_Q = 60 };
enum { JOGSHOW_OFF = 0, JOGSHOW_TIME = 1, JOGSHOW_BAR = 2, JOGSHOW_BOTH = 3 };
static const char *const JOGSHOW_LAB[] = { "Off", "Time", "Bar", "Time + Bar" };
/* Off -> Time -> Bar -> Time + Bar -> Off: the SDK knob cycle for a 4-way CHOICE
 * ((v+1) % n_choices), and the order a tap should take if the row is a checkbox. */
static int __attribute__((unused)) jog_show_cycle(int v)
{
    if (v < JOGSHOW_OFF || v > JOGSHOW_BOTH) v = JOGSHOW_BOTH;
    return (v + 1) % 4;
}
static const char *jog_show_label(int v)
{
    return (v >= JOGSHOW_OFF && v <= JOGSHOW_BOTH) ? JOGSHOW_LAB[v] : "?";
}
enum { JOG_ACT_NONE = 0, JOG_ACT_SEND = 1, JOG_ACT_RESTORE = 2, JOG_ACT_WAIT = 3 };
#define JOG_WAIT_ART_NS   2500000000ull
#define JOG_MIN_PERIOD_NS 1000000000ull
#define JOG_JPEG_CAP      0x2b400
#define JOG_TEXT_N        80

/* ---- time / remain / bar text (matches the deck and jogprobe 0.0.3) -------------------- */
static int jog_fmt_time(int64_t ms, char *big, size_t nb, char *small, size_t ns)
{
    if (ms < 0) { snprintf(big, nb, "--:--"); snprintf(small, ns, ".---"); return -1; }
    int64_t m = ms / 60000, s = (ms / 1000) % 60, f = ms % 1000;
    if (m > 999) m = 999;
    snprintf(big, nb, "%02lld:%02lld", (long long)m, (long long)s);
    snprintf(small, ns, ".%03lld", (long long)f);
    return 0;
}
/* TIME = in point; REMAIN = length − in (unknown length or in past the end = unknown) */
static int64_t jog_value_ms(int remain, int64_t in_ms, int64_t len_ms)
{
    if (in_ms < 0) return -1;
    if (!remain) return in_ms;
    if (len_ms <= 0 || len_ms < in_ms) return -1;
    return len_ms - in_ms;
}
struct jog_gcell { int64_t pos; double bpm; };
static int __attribute__((unused)) jog_beat_floor(const struct jog_gcell *c, int n, int64_t p)
{
    if (!c || n <= 0) return -2;
    if (p < c[0].pos) return -1;
    int lo = 0, hi = n - 1;
    while (lo < hi) { int mid = lo + (hi - lo + 1) / 2; if (c[mid].pos <= p) lo = mid; else hi = mid - 1; }
    return lo;
}
static int __attribute__((unused)) jog_beat_near(const struct jog_gcell *c, int n, int64_t p)
{
    int i = jog_beat_floor(c, n, p);
    if (i == -2) return -2;
    if (i == -1) return (n >= 2 && 2 * (c[0].pos - p) <= (c[1].pos - c[0].pos)) ? 0 : -1;
    if (i + 1 < n && 2 * (p - c[i].pos) > (c[i + 1].pos - c[i].pos)) return i + 1;
    return i;
}
/* 0 = bar,beat set (1-based; first downbeat is 1.1). Pickups: 0.4 0.3 … then -1.4. -1 = no bar. */
static int jog_bar_beat(const int32_t *db, int nd, int n, int i, int *bar, int *beat)
{
    if (!db || nd <= 0 || i < 0 || i >= n) return -1;
    if (i < db[0]) {
        int back = db[0] - i - 1;
        *bar = -(back / 4);
        *beat = 4 - back % 4;
        return 0;
    }
    int lo = 0, hi = nd - 1;
    while (lo < hi) { int mid = lo + (hi - lo + 1) / 2; if (db[mid] <= i) lo = mid; else hi = mid - 1; }
    *bar = lo + 1;
    *beat = i - db[lo] + 1;
    return 0;
}
static int jog_fmt_bar(int ok, int bar, int beat, char *b, size_t nb)
{
    if (ok != 0) { snprintf(b, nb, "--.-"); return -1; }
    snprintf(b, nb, "%d.%d", bar, beat);
    return 0;
}
static long __attribute__((unused)) jog_grid_rate(const struct jog_gcell *c, int k)
{
    static const long rates[] = { 44100, 48000, 88200, 96000 };
    if (!c || k < 2) return 0;
    double secs = 0.0;
    for (int i = 0; i < k - 1; i++) {
        if (!(c[i].bpm >= 20.0 && c[i].bpm <= 999.0)) return 0;
        secs += 60.0 / c[i].bpm;
    }
    if (c[k - 1].pos <= c[0].pos || secs <= 0.0) return 0;
    double est = (double)(c[k - 1].pos - c[0].pos) / secs;
    for (size_t r = 0; r < sizeof rates / sizeof rates[0]; r++)
        if (est > (double)rates[r] * 0.99 && est < (double)rates[r] * 1.01) return rates[r];
    return 0;
}
static int __attribute__((unused)) jog_db_valid(const int32_t *db, int nd, int n)
{
    if (nd < 0 || nd > n) return 0;
    for (int k = 0; k < nd; k++) {
        if (db[k] < 0 || db[k] >= n) return 0;
        if (k && db[k] <= db[k - 1]) return 0;
    }
    return 1;
}
/* One comparison key / log text for a send. Bar mode with no grid falls back to Time. */
static int jog_compose_text(int mode, int remain, int64_t in_ms, int64_t len_ms,
                            int have_bar, int bar, int beat, char *out, size_t n)
{
    char big[16], small[8], bs[32];
    int use = mode;
    if (use == JOGSHOW_BAR && !have_bar) use = JOGSHOW_TIME;
    if (use == JOGSHOW_BOTH && !have_bar) use = JOGSHOW_TIME;
    int64_t v = jog_value_ms(remain, in_ms, len_ms);
    jog_fmt_time(v, big, sizeof big, small, sizeof small);
    if (have_bar) jog_fmt_bar(0, bar, beat, bs, sizeof bs);
    else snprintf(bs, sizeof bs, "--.-");
    const char *tr = remain ? "REMAIN" : "TIME";
    if (use == JOGSHOW_BAR)
        return snprintf(out, n, "BAR %s", bs);
    if (use == JOGSHOW_BOTH)
        return snprintf(out, n, "BAR %s %s %s%s", bs, tr, big, small);
    return snprintf(out, n, "%s %s%s", tr, big, small);
}
static int __attribute__((unused)) jog_pieces(int jpeg)
{
    if (jpeg < 0) return 0;
    return (jpeg + 3 + 57) / 58;
}

/* ---- throttle / restore plan (pure) ---------------------------------------------------- */
typedef struct {
    int showing;                 /* 1 = last successful action was a loop-image send */
    char last[JOG_TEXT_N];       /* last sent text, or "" after a restore / never */
    uint64_t last_ns;            /* time of last send or restore */
    uint64_t load_ns;            /* this collector's last load */
    uint64_t art_ns;             /* EP147 artwork send after that load */
    int load_seen;
    int art_seen;
} jog_st;
typedef struct {
    int enabled;                 /* Active Loop row on */
    int show_mode;               /* JOGSHOW_* */
    int custom;                  /* ImageOnJogDisplay == 1: never override */
    int have_loop;               /* a current Active Loop to show */
    char text[JOG_TEXT_N];
} jog_want;

/* Ready for the first loop-image send after a load: art send + 2.5 s, or load + 2.5 s
 * if no artwork send was seen (a no-artwork track). */
static int jog_first_ready(const jog_st *st, uint64_t now)
{
    if (st->art_seen)
        return now >= st->art_ns + JOG_WAIT_ART_NS;
    if (st->load_seen)
        return now >= st->load_ns + JOG_WAIT_ART_NS;
    return 0;
}
static int jog_rate_ok(const jog_st *st, uint64_t now)
{
    if (!st->last_ns) return 1;
    return now >= st->last_ns + JOG_MIN_PERIOD_NS;
}
/* Decide SEND / RESTORE / WAIT / NONE. Does not mutate st. */
static int jog_decide(const jog_st *st, const jog_want *w, uint64_t now)
{
    if (!st || !w) return JOG_ACT_NONE;
    if (w->custom)
        return JOG_ACT_NONE;     /* stock Custom path; EP147 owns the image */
    int want_loop = w->enabled && w->show_mode != JOGSHOW_OFF && w->have_loop && w->text[0];
    if (want_loop) {
        if (st->showing && strcmp(st->last, w->text) == 0) return JOG_ACT_NONE;
        if (!jog_first_ready(st, now) || !jog_rate_ok(st, now)) return JOG_ACT_WAIT;
        return JOG_ACT_SEND;
    }
    /* stock artwork */
    if (!st->showing) return JOG_ACT_NONE;
    if (!jog_rate_ok(st, now)) return JOG_ACT_WAIT;
    return JOG_ACT_RESTORE;
}
static void __attribute__((unused)) jog_apply_send(jog_st *st, const char *text, uint64_t now)
{
    st->showing = 1;
    snprintf(st->last, sizeof st->last, "%s", text ? text : "");
    st->last_ns = now;
}
static void __attribute__((unused)) jog_apply_restore(jog_st *st, uint64_t now)
{
    st->showing = 0;
    st->last[0] = 0;
    st->last_ns = now;
}
static void __attribute__((unused)) jog_note_load(jog_st *st, uint64_t now)
{
    st->load_ns = now;
    st->load_seen = 1;
    st->art_ns = 0;
    st->art_seen = 0;
    /* last send was for the previous track; the new artwork (or a 2.5 s wait) must land first */
}
static void __attribute__((unused)) jog_note_art(jog_st *st, uint64_t now)
{
    st->art_ns = now;
    st->art_seen = 1;
    if (!st->load_ns) st->load_ns = now;
}

/* ---- 181×181 renderer (layout from make_bar_mocks.py, red caption) --------------------- */
#define JOG_RED_R 230
#define JOG_RED_G 30
#define JOG_RED_B 30
#define JOG_DIM   90

static void jog_blend(uint8_t *im, int x, int y, int r, int g, int b, unsigned a)
{
    if ((unsigned)x >= JOG_W || (unsigned)y >= JOG_H || !a) return;
    uint8_t *p = im + (y * JOG_W + x) * 3;
    if (a >= 255) { p[0]=(uint8_t)r; p[1]=(uint8_t)g; p[2]=(uint8_t)b; return; }
    p[0] = (uint8_t)((p[0] * (255 - a) + r * a + 127) / 255);
    p[1] = (uint8_t)((p[1] * (255 - a) + g * a + 127) / 255);
    p[2] = (uint8_t)((p[2] * (255 - a) + b * a + 127) / 255);
}
static int jog_text_w(int fi, const char *s)
{
    const struct jf_font *f = &jf_fonts[fi];
    int w = 0;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        unsigned i = (c < 128) ? jf_index[c] : 255;
        if (i >= JF_NCHAR) continue;
        w += f->g[i].adv;
    }
    return w;
}
static void jog_draw_text(uint8_t *im, int x, int y, int fi, const char *s, int r, int g, int b)
{
    const struct jf_font *f = &jf_fonts[fi];
    int cx = x;
    for (; *s; s++) {
        unsigned c = (unsigned char)*s;
        unsigned i = (c < 128) ? jf_index[c] : 255;
        if (i >= JF_NCHAR) continue;
        const struct jf_glyph *gl = &f->g[i];
        const uint8_t *src = f->bits + gl->off;
        for (int yy = 0; yy < gl->h; yy++)
            for (int xx = 0; xx < gl->w; xx++)
                jog_blend(im, cx + gl->x + xx, y + gl->y + yy, r, g, b, src[yy * gl->w + xx]);
        cx += gl->adv;
    }
}
static void jog_ctext(uint8_t *im, int y, int fi, const char *s, int r, int g, int b)
{
    int w = jog_text_w(fi, s);
    jog_draw_text(im, (JOG_W - w) / 2, y, fi, s, r, g, b);
}
/* REMAIN / TIME label, active word white, the other dim — as on the main screen. */
static void jog_tr_label(uint8_t *im, int y, int remain, int fi)
{
    const char *a = "REMAIN", *b = "TIME";
    int wa = jog_text_w(fi, a), wb = jog_text_w(fi, b), gap = 10;
    int x = (JOG_W - (wa + gap + wb)) / 2;
    int ar = remain ? 255 : JOG_DIM, ag = ar, ab = ar;
    int br = remain ? JOG_DIM : 255, bg = br, bb = br;
    jog_draw_text(im, x, y, fi, a, ar, ag, ab);
    jog_draw_text(im, x + wa + gap, y, fi, b, br, bg, bb);
}
/* Big run + smaller run, baseline-aligned (ascent of each font). */
static void jog_big_small(uint8_t *im, int y, const char *big, const char *small, int fbi, int fsi)
{
    int wbg = jog_text_w(fbi, big), wsm = jog_text_w(fsi, small);
    int x = (JOG_W - (wbg + wsm)) / 2;
    int asc_b = jf_fonts[fbi].ascent, asc_s = jf_fonts[fsi].ascent;
    jog_draw_text(im, x, y, fbi, big, 255, 255, 255);
    jog_draw_text(im, x + wbg, y + (asc_b - asc_s), fsi, small, 255, 255, 255);
}
/* Thin red ring, matching mock ellipse (3,3,S-4,S-4) width 3. */
static void jog_ring(uint8_t *im)
{
    const float cx = (JOG_W - 1) * 0.5f, cy = (JOG_H - 1) * 0.5f;
    const float r0 = (JOG_W - 7) * 0.5f;     /* inner of the 3 px stroke ≈ 87 */
    const float r1 = r0 + 3.0f;
    for (int y = 0; y < JOG_H; y++) {
        for (int x = 0; x < JOG_W; x++) {
            float dx = x - cx, dy = y - cy;
            float d = dx * dx + dy * dy;
            /* soft edges at r0 and r1 */
            float a0 = r0 - 0.5f, a1 = r1 + 0.5f;
            if (d < a0 * a0 || d > a1 * a1) continue;
            float dist = 0.0f;
            /* cheap sqrt via two Newton steps from a guess */
            float g = (r0 + r1) * 0.5f;
            g = 0.5f * (g + d / g);
            g = 0.5f * (g + d / g);
            dist = g;
            float a = 1.0f;
            if (dist < r0) a = dist - (r0 - 1.0f);
            else if (dist > r1) a = (r1 + 1.0f) - dist;
            if (a <= 0) continue;
            if (a > 1) a = 1;
            jog_blend(im, x, y, JOG_RED_R, JOG_RED_G, JOG_RED_B, (unsigned)(a * 255.0f + 0.5f));
        }
    }
}
static void jog_render_rgb(uint8_t *im, int mode, int remain, int64_t in_ms, int64_t len_ms,
                           int have_bar, int bar, int beat)
{
    memset(im, 0, (size_t)JOG_W * JOG_H * 3);
    int use = mode;
    if ((use == JOGSHOW_BAR || use == JOGSHOW_BOTH) && !have_bar) use = JOGSHOW_TIME;
    char big[16], small[8], bbar[24], bbeat[16];
    int64_t v = jog_value_ms(remain, in_ms, len_ms);
    jog_fmt_time(v, big, sizeof big, small, sizeof small);
    if (have_bar) {
        snprintf(bbar, sizeof bbar, "%d", bar);
        snprintf(bbeat, sizeof bbeat, ".%d", beat);
    }
    if (use == JOGSHOW_BAR) {
        jog_ctext(im, 34, JF_CAP, "ACTIVE LOOP", JOG_RED_R, JOG_RED_G, JOG_RED_B);
        jog_ctext(im, 56, JF_LAB12, "BAR", 255, 255, 255);
        jog_big_small(im, 76, bbar, bbeat, JF_M44, JF_M30);
    } else if (use == JOGSHOW_BOTH) {
        jog_ctext(im, 28, JF_CAP, "ACTIVE LOOP", JOG_RED_R, JOG_RED_G, JOG_RED_B);
        jog_ctext(im, 46, JF_LAB11, "BAR", 255, 255, 255);
        jog_big_small(im, 59, bbar, bbeat, JF_M34, JF_M24);
        jog_tr_label(im, 103, remain, JF_LAB11);
        jog_big_small(im, 118, big, small, JF_M22, JF_M14);
    } else {
        jog_ctext(im, 34, JF_CAP, "ACTIVE LOOP", JOG_RED_R, JOG_RED_G, JOG_RED_B);
        jog_tr_label(im, 56, remain, JF_LAB12);
        jog_big_small(im, 82, big, small, JF_M34, JF_M17);
    }
    jog_ring(im);
}
/* Returns JPEG size, or -1. out_cap must be at least JOG_JPEG_CAP. */
static int jog_render_jpeg(int mode, int remain, int64_t in_ms, int64_t len_ms,
                           int have_bar, int bar, int beat, uint8_t *jpg, int cap)
{
    static uint8_t rgb[JOG_W * JOG_H * 3];
    jog_render_rgb(rgb, mode, remain, in_ms, len_ms, have_bar, bar, beat);
    return jog_jpeg_encode(rgb, JOG_W, JOG_H, JOG_Q, jpg, cap);
}
