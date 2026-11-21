/* SPDX-License-Identifier: GPL-3.0-only */
/* 0.2.19: jog display hooks + tick. Included from active-loop.c just before my_tick. */
#include "jog_display.c"

#define VA_ART_LAMBDA  0x21672f8u
#define VA_HIDART      0x22f90b8u
#define VA_ENCODE      0x22f8e88u
#define VA_TRS_ISREM   0x212f1a0u
#define VA_TIMEWIDGET  0x1d62b58u
#define VA_SETART      0x22f7b08u
#define VA_DROPQ       0x22f58e8u
#define VA_MB_SETSIZE  0x24780b0u
#define VA_IMG_CTOR    0x251e780u
#define VT_ARTCOLL     0x31a9fe0u
#define VT_TRS         0x31985c8u
#define VT_LOADSTATE   0x318ec68u
#define VT_USO_GRID    0x2a9cfa8u
#define VT_GRIDCONT    0x2ae4df8u
#define VT_TRACKQ      0x3203ee8u
#define MEOW_XOCR      0x52434f58u
#define AC_DECK        0xd4u
#define AC_VIEW        0xd8u
#define AC_IMG         0xf8u
#define AC_LOADC       0x100u
#define AC_IMGSET      0x118u
#define TRS_REM        0xb0u
#define TRS_DECK       0x128u
#define TW_LS          0x130u
#define TW_INFO        0x150u
#define TI_LENGTH      0xe8u
#define LS_USO         0x180u
#define LS_DECK        0x47cu
#define USO_CONT       0x28u
#define USO_OFF        0x30u
#define USO_RATE       0x40u
#define GC_CELLS       0x28u
#define GC_N           0x30u
#define GC_DB          0x38u
#define GC_ND          0x40u
#define JOG_IMG_MAX    8
#define JOG_LAMBDA_INS0 0xa9b97bfdu
#define JOG_LAMBDA_INS1 0x910003fdu
#define JOG_LAMBDA_INS2 0xa90153f3u
#define JOG_LAMBDA_INS3 0xaa0003f3u
#define JOG_HIDART_INS0 0xa9bc7bfdu
#define JOG_HIDART_INS1 0x910003fdu
#define JOG_HIDART_INS2 0xa9025bf5u
#define JOG_HIDART_INS3 0xaa0003f5u
#define JOG_ENCODE_INS0 0xa9b47bfdu
#define JOG_ENCODE_INS1 0xaa0003e2u
#define JOG_ENCODE_INS2 0x52800024u
#define JOG_ENCODE_INS3 0x910003fdu
#define JOG_ISREM_INS0  0xa9bd7bfdu
#define JOG_ISREM_INS1  0x52800021u
#define JOG_ISREM_INS2  0x910003fdu
#define JOG_ISREM_INS3  0xf9000bf3u
#define JOG_WIDGET_INS0 0xa9b87bfdu
#define JOG_WIDGET_INS1 0x910003fdu
#define JOG_WIDGET_INS2 0xa90153f3u
#define JOG_WIDGET_INS3 0xaa0003f3u

static int h_jogshow = -1;
static const char *const JOGSHOW_CH[] = { "Off", "Time", "Bar", "Time + Bar" };

typedef void  (*jog_lambda_fn)(void *);
typedef void *(*jog_hidart_fn)(void *, int, int, void *);
typedef void *(*jog_encode_fn)(void *, void *);
typedef int   (*jog_isrem_fn)(void *);
typedef void  (*jog_widget_fn)(void *);
typedef void  (*jog_drop_fn)(void *);
typedef void  (*jog_setart_fn)(void *, const void *);
typedef void  (*jog_mbset_fn)(void *, unsigned long, int);
typedef void  (*jog_imgctor_fn)(void *, int, int, int, int);

static __attribute__((unused)) jog_lambda_fn orig_jog_lambda;
static __attribute__((unused)) jog_hidart_fn orig_jog_hidart;
static __attribute__((unused)) jog_encode_fn orig_jog_encode;
static __attribute__((unused)) jog_isrem_fn  orig_jog_isrem;
static __attribute__((unused)) jog_widget_fn orig_jog_widget;
static int jog_hooks_ok;                 /* lambda + encode claimed: can send */
static int jog_have_isrem, jog_have_widget, jog_have_hidart;

static uintptr_t jog_coll[NDECK];        /* ArtWorkCollect* per deck 0..3 */
static uintptr_t jog_trs_of[NDECK];
static uintptr_t jog_tw_of[NDECK];
static uintptr_t jog_ls_of[NDECK];
static int64_t   jog_len_ms[NDECK];
static uint8_t   jog_rem[NDECK];
static uint8_t   jog_rem_ok[NDECK];
static uint16_t  jog_loadc[NDECK];
static jog_st    jog_state[NDECK];
static char      jog_text[NDECK][JOG_TEXT_N];
static uint8_t   jog_jpg[NDECK][JOG_JPEG_CAP];
static int       jog_jpg_n[NDECK];
static char      jog_jpg_for[NDECK][JOG_TEXT_N];
static int       jog_pending[NDECK];     /* JOG_ACT_SEND / RESTORE / 0 */
static uint32_t  jog_seq[NDECK];
static uint32_t  __attribute__((unused)) jog_sent[NDECK];
static __attribute__((unused)) uint8_t   jog_blank[32];
static __attribute__((unused)) int       jog_blank_ok;

/* encode-hook substitution (collector thread, set around setArtwork) */
static __attribute__((unused)) volatile int       jog_subst;
static __attribute__((unused)) const uint8_t     *jog_subst_jpg;
static __attribute__((unused)) int                jog_subst_n;
static __attribute__((unused)) volatile uintptr_t jog_cur_coll;

static int jog_show_mode(void)
{
    if (!C || !AZ_HAS(C, row_value) || h_jogshow < 0) return JOGSHOW_BOTH;
    int v = C->row_value(h_jogshow);
    if (v < JOGSHOW_OFF || v > JOGSHOW_BOTH) return JOGSHOW_BOTH;
    return v;
}
static int jog_rd64(uintptr_t a, uint64_t *v)
{
    if (!a || (a & 7u) || safe_read(a, v, 8) != 0) return -1;
    return 0;
}
static int jog_rd32(uintptr_t a, uint32_t *v)
{
    if (!a || (a & 3u) || safe_read(a, v, 4) != 0) return -1;
    return 0;
}
static int jog_rd8(uintptr_t a, uint8_t *v)
{
    if (!a || safe_read(a, v, 1) != 0) return -1;
    return 0;
}
static int jog_vt_ok(uintptr_t o, uintptr_t vt)
{
    uint64_t v = 0;
    return o && jog_rd64(o, &v) == 0 && v == vt;
}
static int jog_meow_ok(uintptr_t o, uintptr_t vt)
{
    uint32_t g = 0;
    return jog_vt_ok(o, vt) && jog_rd32(o + 0x10, &g) == 0 && g == MEOW_XOCR;
}

/* Bar.beat of in_ms on this deck's grid (LoadState from the time widget +0x130, USO at +0x180).
 * Returns 0 and sets bar,beat, or -1 (no bar: Time layout is used). */
static int jog_grid_bar(int d, int64_t in_ms, int *bar, int *beat)
{
    if (d < 0 || d >= NDECK || in_ms < 0) return -1;
    uintptr_t ls = __atomic_load_n(&jog_ls_of[d], __ATOMIC_ACQUIRE);
    if (!jog_vt_ok(ls, VT_LOADSTATE)) return -1;
    uint64_t uso = 0, cont = 0, off = 0, cells = 0, dbp = 0;
    uint32_t n = 0, nd = 0, rate_f = 0;
    if (jog_rd64(ls + LS_USO, &uso) || !jog_meow_ok((uintptr_t)uso, VT_USO_GRID)) return -1;
    if (jog_rd64(uso + USO_CONT, &cont) || jog_rd64(uso + USO_OFF, &off) || jog_rd32(uso + USO_RATE, &rate_f)) return -1;
    if (!jog_meow_ok((uintptr_t)cont, VT_GRIDCONT)) return -1;
    if (jog_rd64(cont + GC_CELLS, &cells) || jog_rd32(cont + GC_N, &n) ||
        jog_rd64(cont + GC_DB, &dbp) || jog_rd32(cont + GC_ND, &nd)) return -1;
    if (n < 2 || n > (1u << 16) || nd < 1 || nd > n || !cells || (cells & 7u) || !dbp || (dbp & 3u)) return -1;
    if (nd == n) return -1;                 /* PC-link style: no bar (jogprobe / RESEARCH 4.3) */
    /* rate: prefer USO+0x40 when it is a known rate, else estimate from the first beats */
    long rate = 0;
    if (rate_f == 44100u || rate_f == 48000u || rate_f == 88200u || rate_f == 96000u) rate = (long)rate_f;
    if (!rate) {
        struct jog_gcell ch[8];
        int k = n < 8 ? (int)n : 8;
        if (safe_read((uintptr_t)cells, ch, (size_t)k * 16u) != 0) return -1;
        rate = jog_grid_rate(ch, k);
    }
    if (!rate) return -1;
    int64_t p = (int64_t)((double)in_ms * (double)rate / 1000.0) - (int64_t)off;
    /* nearest cell, via safe_read binary search (no full copy) */
    uint64_t v0 = 0, v1 = 0;
    if (jog_rd64((uintptr_t)cells, &v0)) return -1;
    int i;
    if (p < (int64_t)v0) {
        if (n < 2 || jog_rd64((uintptr_t)cells + 16, &v1)) return -1;
        i = (2 * ((int64_t)v0 - p) <= ((int64_t)v1 - (int64_t)v0)) ? 0 : -1;
    } else {
        int lo = 0, hi = (int)n - 1;
        while (lo < hi) {
            int mid = lo + (hi - lo + 1) / 2;
            uint64_t vm = 0;
            if (jog_rd64((uintptr_t)cells + (uintptr_t)mid * 16u, &vm)) return -1;
            if ((int64_t)vm <= p) lo = mid; else hi = mid - 1;
        }
        i = lo;
        if (i + 1 < (int)n) {
            uint64_t a = 0, b = 0;
            if (jog_rd64((uintptr_t)cells + (uintptr_t)i * 16u, &a) ||
                jog_rd64((uintptr_t)cells + (uintptr_t)(i + 1) * 16u, &b)) return -1;
            if (2 * (p - (int64_t)a) > ((int64_t)b - (int64_t)a)) i = i + 1;
        }
    }
    if (i < 0) return -1;
    int32_t d0 = 0;
    if (safe_read((uintptr_t)dbp, &d0, 4) != 0) return -1;
    /* downbeat list: binary search, or the pickup formula */
    if (i < d0) {
        /* need d0 only */
        int32_t db0 = d0;
        return jog_bar_beat(&db0, 1, (int)n, i, bar, beat) == 0 ? 0 : -1;
    }
    int lo = 0, hi = (int)nd - 1;
    while (lo < hi) {
        int mid = lo + (hi - lo + 1) / 2;
        int32_t dm = 0;
        if (safe_read((uintptr_t)dbp + (uintptr_t)mid * 4u, &dm, 4) != 0) return -1;
        if (dm <= i) lo = mid; else hi = mid - 1;
    }
    int32_t dlo = 0;
    if (safe_read((uintptr_t)dbp + (uintptr_t)lo * 4u, &dlo, 4) != 0) return -1;
    *bar = lo + 1;
    *beat = i - dlo + 1;
    if (*beat < 1) return -1;
    /* re-check USO still points at the same Content */
    uint64_t c2 = 0;
    if (jog_rd64(uso + USO_CONT, &c2) || c2 != cont) return -1;
    return 0;
}

static void __attribute__((unused)) jog_adopt_coll(uintptr_t coll)
{
    if (!jog_vt_ok(coll, VT_ARTCOLL)) return;
    uint32_t deck = 0;
    if (jog_rd32(coll + AC_DECK, &deck) || deck > 3) return;
    __atomic_store_n(&jog_coll[deck], coll, __ATOMIC_RELEASE);
}
static void __attribute__((unused)) jog_adopt_trs(uintptr_t trs)
{
    if (!jog_vt_ok(trs, VT_TRS)) return;
    uint32_t deck = 0;
    if (jog_rd32(trs + TRS_DECK, &deck) || deck > 3) return;
    __atomic_store_n(&jog_trs_of[deck], trs, __ATOMIC_RELEASE);
}
static void __attribute__((unused)) jog_adopt_tw(uintptr_t w)
{
    if (!w || (w & 7u)) return;
    uint64_t trs = 0, ls = 0, info = 0, len = 0;
    uint32_t deck = 0;
    if (jog_rd64(w + TW_LS, &ls) == 0 && jog_vt_ok((uintptr_t)ls, VT_LOADSTATE) &&
        jog_rd32((uintptr_t)ls + LS_DECK, &deck) == 0 && deck <= 3) {
        __atomic_store_n(&jog_ls_of[deck], (uintptr_t)ls, __ATOMIC_RELEASE);
        __atomic_store_n(&jog_tw_of[deck], w, __ATOMIC_RELEASE);
        if (jog_rd64(w + TW_INFO, &info) == 0 && info &&
            jog_rd64((uintptr_t)info + TI_LENGTH, &len) == 0 &&
            len != 0 && len != (uint64_t)INT64_MAX && (int64_t)len > 0 && (int64_t)len < 24 * 3600 * 1000ll)
            __atomic_store_n(&jog_len_ms[deck], (int64_t)len, __ATOMIC_RELAXED);
    } else if (jog_rd64(w + 0x140, &trs) == 0 && jog_vt_ok((uintptr_t)trs, VT_TRS) &&
               jog_rd32((uintptr_t)trs + TRS_DECK, &deck) == 0 && deck <= 3) {
        __atomic_store_n(&jog_tw_of[deck], w, __ATOMIC_RELEASE);
        jog_adopt_trs((uintptr_t)trs);
        if (jog_rd64(w + TW_INFO, &info) == 0 && info &&
            jog_rd64((uintptr_t)info + TI_LENGTH, &len) == 0 &&
            len != 0 && len != (uint64_t)INT64_MAX && (int64_t)len > 0 && (int64_t)len < 24 * 3600 * 1000ll)
            __atomic_store_n(&jog_len_ms[deck], (int64_t)len, __ATOMIC_RELAXED);
    }
}
static void jog_poll_len_rem(int d)
{
    uintptr_t trs = __atomic_load_n(&jog_trs_of[d], __ATOMIC_ACQUIRE);
    uint8_t rem = 0;
    if (trs && jog_rd8(trs + TRS_REM, &rem) == 0) {
        jog_rem[d] = rem ? 1 : 0;
        jog_rem_ok[d] = 1;
    }
    uintptr_t w = __atomic_load_n(&jog_tw_of[d], __ATOMIC_ACQUIRE);
    uint64_t info = 0, len = 0;
    if (w && jog_rd64(w + TW_INFO, &info) == 0 && info &&
        jog_rd64((uintptr_t)info + TI_LENGTH, &len) == 0 &&
        len != (uint64_t)INT64_MAX && (int64_t)len > 0 && (int64_t)len < 24 * 3600 * 1000ll)
        jog_len_ms[d] = (int64_t)len;
}

#if defined(__aarch64__) && !defined(AZ_HARNESS)
static void jog_ensure_blank(void)
{
    if (jog_blank_ok) return;
    memset(jog_blank, 0, sizeof jog_blank);
    jog_imgctor_fn ctor = (jog_imgctor_fn)VA_IMG_CTOR;
    ctor(jog_blank, 2, 1, 1, 1);
    jog_blank_ok = 1;
}
static void *jog_img_ptr(uintptr_t coll, int use_cache)
{
    if (use_cache && coll) {
        uint64_t img = 0;
        if (jog_rd64(coll + AC_IMG, &img) == 0 && img)
            return (void *)(coll + AC_IMG);
    }
    jog_ensure_blank();
    return jog_blank_ok ? (void *)jog_blank : NULL;
}
static void jog_drop_and_set(uintptr_t coll, void *img)
{
    uint64_t view = 0, vt = 0;
    if (jog_rd64(coll + AC_VIEW, &view) || !plausible((uintptr_t)view)) return;
    if (jog_rd64((uintptr_t)view, &vt) || vt != VT_TRACKQ) return;
    if (!img) return;
    jog_drop_fn drop = (jog_drop_fn)VA_DROPQ;
    jog_setart_fn seta = (jog_setart_fn)VA_SETART;
    drop((void *)(uintptr_t)view);
    seta((void *)(uintptr_t)view, img);
}

/* After orig encode (x8 sret MemoryBlock), optionally replace the JPEG. */
static void jog_encode_after(void *self, void *img, void *out, uint64_t t0)
{
    (void)self; (void)img; (void)t0;
    if (!jog_subst || !out || !jog_subst_jpg || jog_subst_n <= 0 || jog_subst_n >= 0x2b401) return;
    jog_mbset_fn setsz = (jog_mbset_fn)VA_MB_SETSIZE;
    setsz(out, (unsigned long)jog_subst_n, 0);
    uint64_t data = 0, sz = 0;
    if (safe_read((uintptr_t)out, &data, 8) != 0 || !data) return;
    if (safe_read((uintptr_t)out + 8, &sz, 8) != 0 || sz < (uint64_t)jog_subst_n) return;
    memcpy((void *)(uintptr_t)data, jog_subst_jpg, (size_t)jog_subst_n);
}
static void *jog_orig_encode_ptr(void) __asm__("al_jog_orig_encode");
static void jog_encode_after_asm(void *self, void *img, void *out, uint64_t t0) __asm__("al_jog_encode_after");
__attribute__((noinline, used))
static void *jog_orig_encode_ptr(void) { return (void *)orig_jog_encode; }
__attribute__((noinline, used))
static void jog_encode_after_asm(void *self, void *img, void *out, uint64_t t0)
{
    jog_encode_after(self, img, out, t0);
}
/* Top-level stub: save incoming x8 (MemoryBlock* sret) before any C prologue, call orig
 * with x0/x1/x8 intact, then the post-hook. Same pattern as jogprobe 0.0.3. */
void *my_jog_encode(void *, void *);
__asm__(
    ".text\n"
    ".align 2\n"
    ".hidden my_jog_encode\n"
    ".type my_jog_encode, %function\n"
    "my_jog_encode:\n"
    ".cfi_startproc\n"
    "stp x29, x30, [sp, #-64]!\n"
    ".cfi_def_cfa_offset 64\n"
    ".cfi_offset 29, -64\n"
    ".cfi_offset 30, -56\n"
    "mov x29, sp\n"
    ".cfi_def_cfa x29, 64\n"
    "stp x0, x1, [sp, #16]\n"
    "str x8, [sp, #32]\n"
    "bl al_jog_orig_encode\n"
    "mov x16, x0\n"
    "ldp x0, x1, [sp, #16]\n"
    "ldr x8, [sp, #32]\n"
    "blr x16\n"
    "str x0, [sp, #48]\n"
    "ldp x0, x1, [sp, #16]\n"
    "ldr x2, [sp, #32]\n"
    "mov x3, xzr\n"
    "bl al_jog_encode_after\n"
    "ldr x0, [sp, #48]\n"
    "ldp x29, x30, [sp], #64\n"
    ".cfi_restore 29\n"
    ".cfi_restore 30\n"
    ".cfi_def_cfa sp, 0\n"
    "ret\n"
    ".cfi_endproc\n"
    ".size my_jog_encode, .-my_jog_encode\n"
);

static void my_jog_lambda(void *task)
{
    uintptr_t coll = 0;
    if (task) {
        uint64_t c = 0;
        if (safe_read((uintptr_t)task + 0x20, &c, 8) == 0) coll = (uintptr_t)c;
    }
    if (coll) {
        jog_adopt_coll(coll);
        __atomic_store_n(&jog_cur_coll, coll, __ATOMIC_RELAXED);
    }
    if (orig_jog_lambda) orig_jog_lambda(task);
    /* After EP147's own work: maybe drop + setArtwork for a pending send/restore. */
    if (coll && jog_hooks_ok) {
        uint32_t deck = 0;
        if (jog_rd32(coll + AC_DECK, &deck) == 0 && deck <= 3) {
            int act = __atomic_load_n(&jog_pending[deck], __ATOMIC_ACQUIRE);
            uint32_t seq = __atomic_load_n(&jog_seq[deck], __ATOMIC_ACQUIRE);
            if (act && seq != __atomic_load_n(&jog_sent[deck], __ATOMIC_ACQUIRE)) {
                uint64_t t0 = now_ns();
                if (act == JOG_ACT_SEND && jog_jpg_n[deck] > 0) {
                    jog_subst_jpg = jog_jpg[deck];
                    jog_subst_n = jog_jpg_n[deck];
                    jog_subst = 1;
                    void *img = jog_img_ptr(coll, 1);
                    if (!img) img = jog_img_ptr(coll, 0);
                    if (img) jog_drop_and_set(coll, img);
                    jog_subst = 0;
                    jog_subst_jpg = NULL;
                    jog_subst_n = 0;
                    uint64_t us = (now_ns() - t0) / 1000ull;
                    jog_apply_send(&jog_state[deck], jog_text[deck], now_ns());
                    __atomic_store_n(&jog_sent[deck], seq, __ATOMIC_RELEASE);
                    __atomic_store_n(&jog_pending[deck], 0, __ATOMIC_RELEASE);
                    LOG("JOG d%d send \"%s\" jpeg=%d pieces=%d us=%llu",
                        deck + 1, jog_text[deck], jog_jpg_n[deck], jog_pieces(jog_jpg_n[deck]),
                        (unsigned long long)us);
                } else if (act == JOG_ACT_RESTORE) {
                    void *img = jog_img_ptr(coll, 1);
                    if (img) jog_drop_and_set(coll, img);
                    uint64_t us = (now_ns() - t0) / 1000ull;
                    jog_apply_restore(&jog_state[deck], now_ns());
                    __atomic_store_n(&jog_sent[deck], seq, __ATOMIC_RELEASE);
                    __atomic_store_n(&jog_pending[deck], 0, __ATOMIC_RELEASE);
                    uint64_t imgw = 0;
                    (void)jog_rd64(coll + AC_IMG, &imgw);
                    LOG("JOG d%d restore %s us=%llu",
                        deck + 1, imgw ? "artwork" : "blank", (unsigned long long)us);
                }
            }
        }
    }
    __atomic_store_n(&jog_cur_coll, 0, __ATOMIC_RELAXED);
}
static void *my_jog_hidart(void *self, int w, int h, void *img)
{
    void *r = orig_jog_hidart ? orig_jog_hidart(self, w, h, img) : NULL;
    /* An EP147 send (not our substitute) starts the 2.5 s first-send wait. */
    if (!jog_subst) {
        uintptr_t coll = __atomic_load_n(&jog_cur_coll, __ATOMIC_RELAXED);
        uint32_t deck = 0;
        if (coll && jog_rd32(coll + AC_DECK, &deck) == 0 && deck <= 3)
            jog_note_art(&jog_state[deck], now_ns());
    }
    (void)w; (void)h; (void)img;
    return r;
}
static int my_jog_isrem(void *self)
{
    int r = orig_jog_isrem ? orig_jog_isrem(self) : 0;
    jog_adopt_trs((uintptr_t)self);
    return r;
}
static void my_jog_widget(void *self)
{
    if (orig_jog_widget) orig_jog_widget(self);
    jog_adopt_tw((uintptr_t)self);
}
#endif /* aarch64 && !HARNESS */

static int64_t jog_in_ms(int d)
{
    if (d < 0 || d >= NDECK || !act_ok[d]) return -1;
    char how[64];
    double u = units_per_ms(d, &anlz_res[d], how, sizeof how);
    if (u <= 0.0) u = 44.1;
    return (int64_t)((double)active_in[d] / u + 0.5);
}
static int jog_have_loop(int d)
{
    return act_ok[d] && !spent[d] &&
           __atomic_load_n(&arm_in[d], __ATOMIC_RELAXED) != INT64_MAX;
}
static int jog_custom(int d)
{
    uintptr_t coll = __atomic_load_n(&jog_coll[d], __ATOMIC_ACQUIRE);
    uint32_t v = 0;
    if (!coll || jog_rd32(coll + AC_IMGSET, &v)) return 0;
    return v == 1;
}
static void jog_note_load_d(int d, uint64_t now)
{
    if (d < 0 || d >= NDECK) return;
    jog_note_load(&jog_state[d], now);
    jog_pending[d] = 0;
    jog_jpg_for[d][0] = 0;
    jog_jpg_n[d] = 0;
}
/* Tick: compose the text, render a JPEG if we are going to send, and publish a pending
 * send/restore for the collector lambda. Called even when the Active Loop row is off
 * (so a loop image is restored). */
static void jog_tick(int enabled)
{
    if (!jog_hooks_ok && !jog_have_widget && !jog_have_isrem)
        return;                              /* nothing claimed: leave the jog alone */
    uint64_t now = now_ns();
    int mode = enabled ? jog_show_mode() : JOGSHOW_OFF;
    for (int d = 0; d < NDECK; d++) {
        jog_poll_len_rem(d);
        uintptr_t coll = __atomic_load_n(&jog_coll[d], __ATOMIC_ACQUIRE);
        if (coll) {
            uint16_t lc = 0;
            if (safe_read(coll + AC_LOADC, &lc, 2) == 0) {
                if (jog_loadc[d] && lc != jog_loadc[d])
                    jog_note_load_d(d, now);
                /* first sighting of a collector already mid-session: start the 2.5 s wait now */
                if (!jog_state[d].load_seen) {
                    jog_state[d].load_ns = now;
                    jog_state[d].load_seen = 1;
                    uint64_t img = 0;
                    if (jog_rd64(coll + AC_IMG, &img) == 0 && img)
                        jog_note_art(&jog_state[d], now);
                }
                jog_loadc[d] = lc;
            }
        }
        if (jog_custom(d) && jog_state[d].showing)
            jog_state[d].showing = 0;        /* Custom: forget ours; do not restore over it */
        jog_want w;
        memset(&w, 0, sizeof w);
        w.enabled = enabled;
        w.show_mode = mode;
        w.custom = jog_custom(d);
        w.have_loop = enabled && jog_have_loop(d);
        int have_bar = 0, bar = 0, beat = 0, remain = jog_rem_ok[d] ? jog_rem[d] : 0;
        int64_t in_ms = -1, len_ms = jog_len_ms[d];
        if (w.have_loop) {
            in_ms = jog_in_ms(d);
            have_bar = (jog_grid_bar(d, in_ms, &bar, &beat) == 0);
            jog_compose_text(mode, remain, in_ms, len_ms, have_bar, bar, beat, w.text, sizeof w.text);
        }
        int act = jog_decide(&jog_state[d], &w, now);
        if (act == JOG_ACT_SEND) {
            if (strcmp(jog_jpg_for[d], w.text) != 0 || jog_jpg_n[d] <= 0) {
                int n = jog_render_jpeg(mode, remain, in_ms, len_ms, have_bar, bar, beat,
                                        jog_jpg[d], JOG_JPEG_CAP);
                if (n <= 0 || n >= 0x2b401) {
                    jog_jpg_n[d] = 0;
                    continue;                /* leave stock */
                }
                jog_jpg_n[d] = n;
                snprintf(jog_jpg_for[d], sizeof jog_jpg_for[d], "%s", w.text);
            }
            snprintf(jog_text[d], sizeof jog_text[d], "%s", w.text);
            __atomic_store_n(&jog_pending[d], JOG_ACT_SEND, __ATOMIC_RELEASE);
            __atomic_add_fetch(&jog_seq[d], 1u, __ATOMIC_RELEASE);
        } else if (act == JOG_ACT_RESTORE) {
            __atomic_store_n(&jog_pending[d], JOG_ACT_RESTORE, __ATOMIC_RELEASE);
            __atomic_add_fetch(&jog_seq[d], 1u, __ATOMIC_RELEASE);
        }
        (void)act;
    }
}

static int jog_claim_one(uintptr_t va, const uint32_t sig[4], const char *what,
                         void *hook, void **orig)
{
    uint32_t ins[4];
    if (!target_ok(va, what)) return 0;
    if (safe_read(va, ins, sizeof ins) != 0 ||
        ins[0] != sig[0] || ins[1] != sig[1] || ins[2] != sig[2] || ins[3] != sig[3]) {
        LOG("jog: %s %#lx prologue mismatch; not claimed", what, (unsigned long)va);
        return 0;
    }
    if (C->claim_hook(MOD_ID, va, hook, orig) != 0) {
        LOG("jog: claim_hook %s %#lx refused", what, (unsigned long)va);
        *orig = NULL;
        return 0;
    }
    return 1;
}
static void jog_claim(void)
{
    jog_hooks_ok = 0;
    jog_have_isrem = jog_have_widget = jog_have_hidart = 0;
#if defined(__aarch64__) && !defined(AZ_HARNESS)
    {
        static const uint32_t s_lam[4] = { JOG_LAMBDA_INS0, JOG_LAMBDA_INS1, JOG_LAMBDA_INS2, JOG_LAMBDA_INS3 };
        static const uint32_t s_hid[4] = { JOG_HIDART_INS0, JOG_HIDART_INS1, JOG_HIDART_INS2, JOG_HIDART_INS3 };
        static const uint32_t s_enc[4] = { JOG_ENCODE_INS0, JOG_ENCODE_INS1, JOG_ENCODE_INS2, JOG_ENCODE_INS3 };
        static const uint32_t s_isr[4] = { JOG_ISREM_INS0, JOG_ISREM_INS1, JOG_ISREM_INS2, JOG_ISREM_INS3 };
        static const uint32_t s_wid[4] = { JOG_WIDGET_INS0, JOG_WIDGET_INS1, JOG_WIDGET_INS2, JOG_WIDGET_INS3 };
        int lam = jog_claim_one(VA_ART_LAMBDA, s_lam, "artLambda", (void *)my_jog_lambda, (void **)&orig_jog_lambda);
        int enc = jog_claim_one(VA_ENCODE, s_enc, "encode", (void *)my_jog_encode, (void **)&orig_jog_encode);
        jog_have_hidart = jog_claim_one(VA_HIDART, s_hid, "hidArtWork", (void *)my_jog_hidart, (void **)&orig_jog_hidart);
        jog_have_isrem  = jog_claim_one(VA_TRS_ISREM, s_isr, "isRemain", (void *)my_jog_isrem, (void **)&orig_jog_isrem);
        jog_have_widget = jog_claim_one(VA_TIMEWIDGET, s_wid, "timeWidget", (void *)my_jog_widget, (void **)&orig_jog_widget);
        jog_hooks_ok = lam && enc;
        if (!jog_hooks_ok)
            LOG("jog: display off (need artLambda + encode); other Active Loop features unchanged");
        else
            LOG("jog: hooks lambda=%d encode=%d hidart=%d isRemain=%d widget=%d",
                lam, enc, jog_have_hidart, jog_have_isrem, jog_have_widget);
    }
#else
    (void)jog_claim_one;
#endif
}
static void jog_row_changed(int value, void *user)
{
    (void)user;
    if (row_on()) LOG("row Jog shows -> %s (applied now)", jog_show_label(value));
}
