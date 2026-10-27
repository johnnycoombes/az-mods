/* SPDX-License-Identifier: GPL-3.0-only
 *
 * tempo-range: replaces the magnitude of the three non-WIDE TEMPO RANGE slots
 * (+/-6%, +/-10%, +/-16%) with user-chosen percentages, leaving WIDE alone.
 *
 * How: claim_hook on TempoCtl::recompute (the function at 0x149afe8, VA 0x149afe8 on tag
 * ep147-9e9454e357389ece). The hook calls the original, rewrites +0x14/+0x18,
 * re-feeds the master's display/audio sink (0x152cfc8) and ST_SET_TEMPO
 * (0x1525c80). See /workspace/ep147/EP147-TEMPO-RANGE.md.
 *
 * 0.1.15: layout mode from an inline hook on WaveformView's paintOverChildren (0x2335660):
 *         0.1.14's claim_vslot on the paint slot was refused ("not inside EP147's data": the
 *         vtables of this non-PIE binary sit in .rodata), so the mode stayed unknown.
 *         Deck-select filter switch-off is logged with the fallback in force.
 * 0.1.14: layer from the deck-select buttons (read-only ERP filter), so the 2-deck overlay
 *         follows a layer switch at once, fader still; 2/4 DECK view drawn per deck (the
 *         WaveformView mode, read from its own paint call); range changes logged per deck.
 * 0.1.13: release build (source link, description); an unknown deck is skipped (no seen-order guess).
 * 0.1.12: decks 3/4 (deck from the player id, layer per side), panel colours, margins.
 * 0.1.11: BPM shown with two decimals (125.00) by an overlay; see "BPM with two decimals".
 *
 * 0.1.4:
 *  - Log fix: the live-apply log was tied to the row-value de-dupe and could be
 *    skipped. on_change now applies and logs directly from its `value` argument
 *    (authoritative, no stale re-read); tick() is a backstop for any other path
 *    (e.g. an azmods.cfg edit). Both funnel through set_pct(), which logs on
 *    every real change, even to 0 decks. A one-shot debug line reports each path.
 *  - Overlay fix: deck_for_ds was fed the player-state object (good for
 *    ST_SET_TEMPO, not a DeckState) and returned -1, so nothing drew. We now try
 *    deck_for_ds, then deck_for_modeobj on the parent and on self, then fall back
 *    to first-seen order, and log the resolved deck index + method once.
 *  - New measured label boxes.
 *
 * The pitch-scale ticks are fixed artwork. SYNC still forces pitch to 0.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "azmod-mod.h"

#define MOD_ID "tempo-range"
#define FW_TAG "ep147-9e9454e357389ece"
#define VA_RECOMPUTE    ((uintptr_t)0x149afe8u)
#define VA_ST_SET_TEMPO ((uintptr_t)0x1525c80u)
#define VA_PITCH_SINK   ((uintptr_t)0x152cfc8u)
#define PARENT_ADJ  0x50
#define ADJ_OFF     0x20

/* TempoControl layout (confirmed in the 2.00 disassembly). */
#define OFF_PARENT  0x08
#define OFF_SLIDER  0x10
#define OFF_ANALOG  0x14
#define OFF_APPLIED 0x18
#define OFF_SYNC    0x20
#define OFF_RANGE   0x24
#define PARENT_STATE 0x68

#define NDECK 4

static const az_core_v1 *C;

typedef void (*recompute_fn)(void *self);
typedef void (*st_set_tempo_fn)(void *state, float pitch);

static recompute_fn      orig_recompute;
static st_set_tempo_fn   st_set_tempo;
static st_set_tempo_fn   pitch_sink;

/* TempoControl objects seen by the hook, in first-seen order (= deck order fallback). */
#define MAXSEEN 8
static void *seen[MAXSEEN];
static int seen_n;
static int seen_index(void *self)   /* first-seen slot for self, assigning one if new */
{
    for (int i = 0; i < MAXSEEN; i++) {
        void *v = __atomic_load_n(&seen[i], __ATOMIC_ACQUIRE);
        if (v == self) return i;
        if (!v) {
            void *exp = NULL;
            if (__atomic_compare_exchange_n(&seen[i], &exp, self, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                __atomic_add_fetch(&seen_n, 1, __ATOMIC_RELAXED);
                return i;
            }
            if (exp == self) return i;
        }
    }
    return -1;
}
static void my_recompute(void *self);
static int logged_first;

/* Custom magnitude for enum 0/1/2, as percent. 0 means "use stock". */
/* Defaults: the three non-WIDE slots become +/-4, +/-8, +/-16. WIDE stays +/-100%. */
static int pct[3] = { 4, 8, 16 };
static int h_row[3] = { -1, -1, -1 };
static int applied_pct[3] = { -1, -1, -1 };     /* last magnitude we applied+logged */
static const int STOCK[3] = { 6, 10, 16 };
static const char *const KEY[3] = { "pct6", "pct10", "pct16" };
static const char *const LABEL[3] = { "Replace +/-6%", "Replace +/-10%", "Replace +/-16%" };

/* SDK: 2..8 choices per row. Index 0 = std (firmware 6/10/16). Same list on every row. */
static const char *const CH_ALL[]  = { "std", "4%", "8%", "16%" };
static const int         PCT_ALL[] = { 0, 4, 8, 16 };
/* Fresh install defaults: map the three slots to 4 / 8 / 16. */
static const int DEF_CHOICE[3] = { 1, 2, 3 };
enum { NCHOICE = 4 };
_Static_assert(sizeof CH_ALL / sizeof CH_ALL[0] == NCHOICE && sizeof PCT_ALL / sizeof PCT_ALL[0] == NCHOICE, "all");
_Static_assert(NCHOICE >= 2 && NCHOICE <= 8, "SDK: 2..8 choices");
static const char *const *const ROW_CH[3] = { CH_ALL, CH_ALL, CH_ALL };
static const int *const ROW_PCT[3] = { PCT_ALL, PCT_ALL, PCT_ALL };

static int idx_to_pct(int slot, int idx)          /* out of range -> std (0) */
{
    return (idx >= 0 && idx < NCHOICE) ? ROW_PCT[slot][idx] : 0;
}
/* 0.1.8 saved choice indices against a longer list; 0.1.9+ uses 4 entries.
 * On first boot of this build, reset the three rows to the 4/8/16 defaults. */
#define CFG_VER_KEY "cfg-ver"
#define CFG_VER_NOW 10
static void migrate_choice_rows(void)
{
    if (!AZ_HAS(C, cfg_get) || !AZ_HAS(C, cfg_set) || !AZ_HAS(C, row_set)) return;
    int ver = C->cfg_get(MOD_ID, CFG_VER_KEY, 0);
    if (ver >= CFG_VER_NOW) return;
    for (int i = 0; i < 3; i++) {
        if (h_row[i] < 0) continue;
        C->row_set(h_row[i], DEF_CHOICE[i]);
        pct[i] = idx_to_pct(i, DEF_CHOICE[i]);
        applied_pct[i] = pct[i];
    }
    C->cfg_set(MOD_ID, CFG_VER_KEY, CFG_VER_NOW);
    C->log(MOD_ID, "migrated choice rows from cfg-ver %d to %d", ver, CFG_VER_NOW);
}
static int pct_to_choice(int slot, int p)
{
    for (int i = 0; i < NCHOICE; i++)
        if (ROW_PCT[slot][i] == p) return i;
    return 0;
}
static int valid_pct(int slot, int p) { return p == 0 || pct_to_choice(slot, p) > 0; }

/* ---- label overlay -------------------------------------------------------------------- */
static int dshow[NDECK];            /* per-deck magnitude to draw; 0 = hide */
static az_surface *S[NDECK];
static int surf_vi = -1;
static int h_label = -1;
static int lbl_outline;
static int h_outline = -1;

typedef struct { int x[NDECK], y[NDECK], w, h, px; } layout;
static layout L[2];
static const layout DEF[2] = {
    /* 2-deck (measured 1280x800): D1/D2 "+/-NN" box; D3/D4 unused. */
    { { 431, 1071, 431, 1071 }, { 619, 619, 619, 619 }, 60, 25, 18 },
    /* 4-deck (measured): D1/D2 at y624, D3/D4 at y744. */
    { { 580, 1220, 580, 1220 }, { 624, 624, 744, 744 }, 44, 20, 14 },
};

static void lkey(char *b, size_t n, int vi, char f, int d)
{
    if (d < 0) snprintf(b, n, "l%d_%c", vi ? 4 : 2, f);
    else       snprintf(b, n, "l%d_%c%d", vi ? 4 : 2, f, d + 1);
}
static void load_layout(void)
{
    char k[24];
    for (int vi = 0; vi < 2; vi++) {
        for (int d = 0; d < NDECK; d++) {
            lkey(k, sizeof k, vi, 'x', d); L[vi].x[d] = C->cfg_get(MOD_ID, k, DEF[vi].x[d]);
            lkey(k, sizeof k, vi, 'y', d); L[vi].y[d] = C->cfg_get(MOD_ID, k, DEF[vi].y[d]);
        }
        lkey(k, sizeof k, vi, 'w', -1); L[vi].w  = C->cfg_get(MOD_ID, k, DEF[vi].w);
        lkey(k, sizeof k, vi, 'h', -1); L[vi].h  = C->cfg_get(MOD_ID, k, DEF[vi].h);
        lkey(k, sizeof k, vi, 'p', -1); L[vi].px = C->cfg_get(MOD_ID, k, DEF[vi].px);
    }
}

/* ---- BPM with two decimals (0.1.11) ---------------------------------------------------
 * Stock EP147 shows the deck BPM as four PNG digit glyphs, HHH.T: BpmWidgetBase's update
 * (the function at 0x1f185b8 on 2.00) reads the int BPM x100 at [widget+0x90]+0xa8 and passes
 * x100/10000, (x100/100%100)/10, x100/100%10 and (int)((x100%100)*0.1) (truncated tenths) to
 * vtable slots +0x68/+0x70/+0x78/+0x88. There is no fifth glyph and no format string, so two
 * decimals are drawn by an overlay (no extra hook) from the core's deck_bpm(), rounded once
 * to 0.01. The core allows 4 surfaces per mod and the +/-NN labels already use them, so:
 *   2-deck view: surfaces 0/1 = range labels decks 1/2, surfaces 2/3 = BPM decks 1/2.
 *   4-deck view: all four surfaces are either range labels or BPM (Mod Menu row). */
enum { ROLE_OFF, ROLE_LABEL, ROLE_BPM };
/* Layer per side (0.1.12): decks 1/3 share the left controls, 2/4 the right. EP147 only runs
 * TempoCtl::recompute for the deck a side currently controls (0.1.11 log: decks 3/4 never
 * reached the hook while their layer was off), so the deck whose call count moved since the
 * last tick is that side's active layer. */
static unsigned rc_calls[NDECK];
static unsigned rc_seen[NDECK];
static int side_deck[2] = { 0, 1 };
/* 0.1.14: the deck-select buttons. EP147 reads them from the ERP input frame: per side s,
 * byte 8 + 8*s bit 0 = primary (deck 1/2), byte 9 + 8*s bit 0 = secondary (deck 3/4); the
 * core's own erp_deck_select() reads the same bits. A read-only (AZ_FILTER_RAW) filter keeps
 * the last selection per side in one word: bit 2s = side known, bit 2s+1 = secondary;
 * bits 8..31 count frames (heartbeat). Recompute activity (0.1.12) is only the fallback
 * while no press has been seen or the filter is not running. */
#define ERP_SEL_O(s) (8 + 8 * (s))
static uint32_t sel_word;      /* filter writes (relaxed), tick reads */
static uint32_t sel_last_bits; /* filter-only */
static int on_erp_sel(int bus, unsigned char *f, int len, void *user)
{
    (void)bus; (void)user;
    if (!f || len < 18) return AZ_FRAME_OK;
    uint32_t w = __atomic_load_n(&sel_word, __ATOMIC_RELAXED);
    uint32_t bits = 0;
    for (int s = 0; s < 2; s++)
        bits |= ((f[ERP_SEL_O(s)] & 1u) | ((f[ERP_SEL_O(s) + 1] & 1u) << 1)) << (2 * s);
    uint32_t rise = bits & ~sel_last_bits;
    sel_last_bits = bits;
    for (int s = 0; s < 2; s++) {
        unsigned r = (rise >> (2 * s)) & 3u;
        if (r & 1u) w = (w & ~(3u << (2 * s))) | (1u << (2 * s));          /* primary */
        else if (r & 2u) w = (w & ~(3u << (2 * s))) | (3u << (2 * s));     /* secondary */
    }
    w = (w & 0xffu) | ((w + 0x100u) & 0xffffff00u);
    __atomic_store_n(&sel_word, w, __ATOMIC_RELAXED);
    return AZ_FRAME_OK;
}
static int sel_filter_on;      /* 1 = registered */
static uint32_t sel_hb_last;
static int sel_hb_same, sel_dead;
static void update_layers(void)
{
    uint32_t w = __atomic_load_n(&sel_word, __ATOMIC_RELAXED);
    if (sel_filter_on) {
        if ((w >> 8) == sel_hb_last) {
            if (++sel_hb_same == 180 && !sel_dead) {
                sel_dead = 1;
                C->log(MOD_ID, "deck-select filter not running (the core switches a filter off after 8 calls over 200 us, counted with any preemption): layer follows fader activity until EP147 restarts");
            }
        } else { sel_hb_same = 0; if (sel_dead) { sel_dead = 0; C->log(MOD_ID, "deck-select filter running again"); } }
        sel_hb_last = w >> 8;
    }
    for (int sd = 0; sd < 2; sd++) {
        if (sel_filter_on && !sel_dead && ((w >> (2 * sd)) & 1u)) {     /* button state: authoritative */
            int nd = sd + (((w >> (2 * sd + 1)) & 1u) ? 2 : 0);
            unsigned ca = __atomic_load_n(&rc_calls[sd], __ATOMIC_RELAXED), cb = __atomic_load_n(&rc_calls[sd + 2], __ATOMIC_RELAXED);
            rc_seen[sd] = ca; rc_seen[sd + 2] = cb;
            if (nd != side_deck[sd]) {
                side_deck[sd] = nd;
                C->log(MOD_ID, "layer: side %c -> deck %d (deck-select button)", sd ? 'R' : 'L', nd + 1);
            }
            continue;
        }
        int a = sd, b = sd + 2;
        unsigned ca = __atomic_load_n(&rc_calls[a], __ATOMIC_RELAXED);
        unsigned cb = __atomic_load_n(&rc_calls[b], __ATOMIC_RELAXED);
        unsigned da = ca - rc_seen[a], db = cb - rc_seen[b];
        rc_seen[a] = ca; rc_seen[b] = cb;
        int nd = side_deck[sd];
        if (da && !db) nd = a; else if (db && !da) nd = b; else if (da && db) nd = db > da ? b : a;
        if (nd != side_deck[sd]) {
            side_deck[sd] = nd;
            C->log(MOD_ID, "layer: side %c -> deck %d (fader activity)", sd ? 'R' : 'L', nd + 1);
        }
    }
}
/* 0.1.15: see below; 0.1.14 used a vtable slot, refused by the core. EP147's deck layout. WaveformView keeps it at +0x3a8 (0 = 2 DECK, 1 = 2/4 DECK,
 * 2 = 4 DECK; the core reads the same field). deck_count() says 2 for both 2 DECK and 2/4 DECK,
 * but 2/4 DECK shows all four deck panels at the 4 DECK positions, so 2/4 must be drawn per
 * deck. The view is learnt from its own paint call: a vtable-slot hook (claim_vslot) on
 * WaveformView::paint (vtable 0x2d61ae0 slot 26 = 0x1b3aaa0) stores the mode with one relaxed
 * store and calls through. -1 = unknown: the 0.1.13 behaviour (2-deck layout). */
/* 0.1.15: claim_vslot only takes slots in EP147's writable data and this binary keeps its
 * vtables in .rodata, so the slot hook was refused. Instead: an inline claim_hook on
 * 0x2335660, juce::Component::paintOverChildren (shared by many components; the core
 * re-points WaveformView's slot 27 at its own hook, which calls this function, but does not
 * patch its code). The hook compares the vtable (one load) and, for the WaveformView only,
 * reads the mode (WaveformView relayout 0x1b3fa60 reads the same field: ldr w0,[x19,#0x3a8]). */
#define VT_WAVEVIEW   ((uintptr_t)0x2d61ae0u)
#define VA_COMP_POC   ((uintptr_t)0x2335660u)
#define POC_INSN0     0xa9bc7bfdu          /* stp x29, x30, [sp, #-64]! */
#define WV_MODE_OFF   0x3a8
typedef void (*wv_paint_fn)(void *self, void *g);
static wv_paint_fn orig_wv_paint;
static int wv_mode = -1;
static void my_wv_paint(void *self, void *g)
{
    if (self && *(uintptr_t *)self == VT_WAVEVIEW) {
        int m = *(const int *)((const char *)self + WV_MODE_OFF);
        if (m >= 0 && m <= 2) __atomic_store_n(&wv_mode, m, __ATOMIC_RELAXED);
    }
    orig_wv_paint(self, g);
}

/* Panel colour under the stock readouts, sampled on AZ-EMU (0.1.11 run): #DCDCDC on Day,
 * #000000 on stock and every dark theme. Digits are white on dark themes. */
static uint32_t panel_bg(void) { return (AZ_HAS(C, ui_theme) && C->ui_theme() == 1) ? 0xDCDCDCu : 0x000000u; }
static uint32_t panel_fg(void) { return (AZ_HAS(C, ui_theme) && C->ui_theme() == 1) ? C->color(AZ_COL_TEXT) : 0xFFFFFFu; }
static int role[NDECK], role_deck[NDECK];      /* per surface: what it shows, for which deck */
static long bpm_cents[NDECK];                  /* per deck, BPM x100 to draw; 0 = none */
static int h_bpm = -1, h_bpm4 = -1;
static layout B[2];
static const layout BDEF[2] = {
    /* From bpm2dp (measured on AZ-EMU, 1280x800): over the stock "HHH.T" digits. */
    /* 0.1.12: 3 px inside the stock frame (2-deck frame x 509..628 / 1149..1268, bottom 686;
     * 4-deck frame x 530..628 / 1170..1268, bottom 616 / 736), measured on AZ-EMU. */
    { { 513, 1153, 513, 1153 }, { 637, 637, 637, 637 }, 112, 44, 34 },
    { { 534, 1174, 534, 1174 }, { 580, 580, 700, 700 }, 91, 33, 26 },
};
static void load_bpm_layout(void)
{
    char k[24];
    for (int vi = 0; vi < 2; vi++) {
        for (int d = 0; d < NDECK; d++) {
            snprintf(k, sizeof k, "b%d_x%d", vi ? 4 : 2, d + 1); B[vi].x[d] = C->cfg_get(MOD_ID, k, BDEF[vi].x[d]);
            snprintf(k, sizeof k, "b%d_y%d", vi ? 4 : 2, d + 1); B[vi].y[d] = C->cfg_get(MOD_ID, k, BDEF[vi].y[d]);
        }
        snprintf(k, sizeof k, "b%d_w", vi ? 4 : 2); B[vi].w  = C->cfg_get(MOD_ID, k, BDEF[vi].w);
        snprintf(k, sizeof k, "b%d_h", vi ? 4 : 2); B[vi].h  = C->cfg_get(MOD_ID, k, BDEF[vi].h);
        snprintf(k, sizeof k, "b%d_p", vi ? 4 : 2); B[vi].px = C->cfg_get(MOD_ID, k, BDEF[vi].px);
    }
}
/* "125.00": BPM x100 -> text. 20..400 BPM per the core, so at most "400.00" (6 chars). */
static void fmt_bpm(char *s, size_t n, long c) { snprintf(s, n, "%ld.%02ld", c / 100, c % 100); }
/* Largest text size <= px whose width fits w - 4 (so 6 chars never spill past the box). */
static int fit_px(const char *s, int px, int w)
{
    if (!AZ_HAS(C, text_width)) return px;
    while (px > 8 && C->text_width(s, px) > w - 4) px--;
    return px;
}

static void paint_bpm(az_draw *d, az_rect a, int deck)
{
    long c = __atomic_load_n(&bpm_cents[deck], __ATOMIC_RELAXED);
    if (c <= 0) return;
    char s[24];
    fmt_bpm(s, sizeof s, c);
    int vi = surf_vi < 0 ? 0 : surf_vi;
    int px = fit_px(s, B[vi].px, a.w);
    C->fill_rect(d, a.x, a.y, a.w, a.h, panel_bg());   /* hides the stock HHH.T */
    int tw = AZ_HAS(C, text_width) ? C->text_width(s, px) : 0;
    int x = a.x + a.w - tw - 2; if (x < a.x) x = a.x;          /* right-aligned like stock */
    int y = a.y + (a.h - px * 3 / 2) / 2; if (y < a.y) y = a.y;
    C->draw_text(d, x, y, s, px, panel_fg());
    if (lbl_outline) C->draw_rect(d, a.x, a.y, a.w, a.h, 2, 0x00FFFFu);
}

static void paint_label(az_draw *d, az_rect a, int deck)
{
    int p = __atomic_load_n(&dshow[deck], __ATOMIC_RELAXED);
    if (p <= 0) return;                              /* stock: never painted (hidden anyway) */
    char s[16];
    snprintf(s, sizeof s, "+/-%d", p);
    int vi = surf_vi < 0 ? 0 : surf_vi;
    int px = L[vi].px;
    C->fill_rect(d, a.x, a.y, a.w, a.h, panel_bg());   /* opaque patch over the stock box */
    int tw = AZ_HAS(C, text_width) ? C->text_width(s, px) : 0;
    int x = a.x + (a.w - tw) / 2; if (x < a.x) x = a.x;
    int y = a.y + (a.h - px * 3 / 2) / 2; if (y < a.y) y = a.y;
    C->draw_text(d, x, y, s, px, panel_fg());
    if (lbl_outline) C->draw_rect(d, a.x, a.y, a.w, a.h, 2, 0xFF00FFu);     /* after the fill */
}

static void paint(az_draw *d, az_rect a, void *user)
{
    int si = (int)(intptr_t)user;
    if (si < 0 || si >= NDECK) return;
    int r = __atomic_load_n(&role[si], __ATOMIC_RELAXED);
    int deck = __atomic_load_n(&role_deck[si], __ATOMIC_RELAXED);
    if (deck < 0 || deck >= NDECK) return;
    if (r == ROLE_LABEL) paint_label(d, a, deck);
    else if (r == ROLE_BPM) paint_bpm(d, a, deck);
}

/* Assign each surface a role for view vi and place it. 4-deck: bpm4 = BPM instead of labels. */
static void place_all(int vi)
{
    int bpm_on = h_bpm >= 0 ? C->row_value(h_bpm) == 1 : 1;
    int bpm4 = bpm_on && h_bpm4 >= 0 && C->row_value(h_bpm4) == 1;
    for (int s = 0; s < NDECK; s++) {
        int r, dk;
        if (vi == 0) { r = s < 2 ? ROLE_LABEL : (bpm_on ? ROLE_BPM : ROLE_OFF); dk = side_deck[s & 1]; }
        else         { r = bpm4 ? ROLE_BPM : ROLE_LABEL; dk = s; }
        __atomic_store_n(&role_deck[s], dk, __ATOMIC_RELAXED);
        __atomic_store_n(&role[s], r, __ATOMIC_RELAXED);
        const layout *l = r == ROLE_BPM ? &B[vi] : &L[vi];
        int pos = vi == 0 ? (s & 1) : dk;    /* 2-deck: the side's box, whichever layer */
        C->surface_place(S[s], (az_rect){ l->x[pos], l->y[pos], l->w, l->h });
    }
    surf_vi = vi;
}

/* ---- apply ---------------------------------------------------------------------------- */
/* The one place that changes a slot's magnitude, re-applies it live, and logs. */
static void set_pct(int slot, int p, const char *via)
{
    if (slot < 0 || slot > 2) return;
    if (!valid_pct(slot, p)) p = 0;   /* unknown -> stock */
    if (p == applied_pct[slot]) return;             /* genuinely unchanged: no spam */
    applied_pct[slot] = p;
    __atomic_store_n(&pct[slot], p, __ATOMIC_RELAXED);
    /* No re-apply from the UI thread: EP147 runs recompute continuously (the log shows it at
     * slider 0.000 with no fader movement), so the new value takes effect on the deck's own
     * thread within a frame. 0.1.6's UI-thread re-apply never returned to its log line. */
    /* Same shape as 0.1.6's line, which printed; 0.1.4/0.1.5/0.1.7 added a trailing
     * parenthetical and the core never wrote those lines. Keep it short and plain. */
    C->log(MOD_ID, "%s -> %d%% via %s", KEY[slot], p ? p : STOCK[slot], via);
}

/* on_change: use the value argument directly (authoritative; no stale re-read). */
static void on_choice(int value, void *user)
{
    int slot = (int)(intptr_t)user;
    C->log(MOD_ID, "on_change slot=%d value=%d", slot, value);   /* proves the callback fires */
    if (slot < 0 || slot > 2) return;
    set_pct(slot, idx_to_pct(slot, value), "menu");
}

/* ---- deck identity (RE on 2.00, see report) -------------------------------------------
 * DjPlayerUnit ctor 0x155d9f0(unit, x1, x2, w3) builds, in order:
 *   PlayerBasisUnit  [unit+0x180]  (ctor 0x156b058, vtable 0x2bf2268)
 *   holder Y         [unit+0x188]  (ctor 0x156a2d8, called at 0x155de90)
 *   tempo owner X    [Y+0x78]      (ctor 0x149a698, called at 0x156a680; vtable 0x2bdfac0)
 *   TempoControl     X+0x08        (vtable 0x2bdfbe8; its +0x08 points back to X; X+0x38 = PBU)
 * Deck = u32 at [[unit+0x1a0]+0x1f18] (SDK reference-objects.md), set late in the same ctor. */
#define VT_DJPU   ((uintptr_t)0x2bf2348u)
#define VT_PBU    ((uintptr_t)0x2bf2268u)
#define VT_TOWNER ((uintptr_t)0x2bdfac0u)

static int plausible_ptr(uintptr_t p)     /* user-space, aligned (aarch64 Linux: 48-bit VA) */
{
    return p >= 0x10000u && (p & 7u) == 0 && p < ((uintptr_t)1 << 48);
}
static uintptr_t rdp(uintptr_t a) { return plausible_ptr(a) ? *(uintptr_t *)a : 0; }


/* No ctor hook: the core already hooks DjPlayerUnit's ctor (addresses.csv core_hook=yes), so its
 * first instruction is a branch and claim_hook declines it; and the decks exist before mods load.
 *
 * Mapping by behaviour instead: every 500 ms tick() snapshots each seen TempoControl's applied
 * pitch (+0x18) and each deck's core deck_bpm(d). When one TempoControl's pitch moves and exactly
 * one deck's BPM moves by the same ratio (1+p_now)/(1+p_prev), that is a vote; two agreeing votes
 * bind tc -> deck for the session. deck_bpm is the core's own per-deck value, so this is an
 * identity, not an arrival order. Until bound, a TempoControl keeps its provisional seen-order. */
static float snap_p[MAXSEEN];
static double snap_bpm[NDECK];
static int bound[MAXSEEN] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int vote_deck[MAXSEEN] = { -1, -1, -1, -1, -1, -1, -1, -1 }, votes[MAXSEEN];
static long last_snap_ms;

static void correlate(long now_ms)
{
    if (now_ms - last_snap_ms < 500) return;
    int first = last_snap_ms == 0;
    last_snap_ms = now_ms;
    double bpm[NDECK];
    for (int d = 0; d < NDECK; d++) bpm[d] = (C->deck_loaded(d) == 1) ? C->deck_bpm(d) : 0.0;
    for (int i = 0; i < MAXSEEN; i++) {
        void *o = __atomic_load_n(&seen[i], __ATOMIC_ACQUIRE);
        if (!o) continue;
        float pnow = *(float *)((char *)o + OFF_APPLIED);
        float pprev = snap_p[i];
        snap_p[i] = pnow;
        if (first || bound[i] >= 0 || fabsf(pnow - pprev) < 0.002f) continue;
        double want = (1.0 + pnow) / (1.0 + pprev);
        int hit = -1, nhit = 0;
        for (int d = 0; d < NDECK; d++) {
            if (bpm[d] <= 0.0 || snap_bpm[d] <= 0.0) continue;
            double got = bpm[d] / snap_bpm[d];
            if (fabs(got - 1.0) < 0.0005) continue;                 /* this deck did not move */
            if (fabs(got - want) < 0.0015 * want) { hit = d; nhit++; }
        }
        if (nhit != 1) continue;                                    /* ambiguous (e.g. SYNC) */
        if (vote_deck[i] == hit) votes[i]++; else { vote_deck[i] = hit; votes[i] = 1; }
        C->log(MOD_ID, "deck vote: tc#%d -> deck %d (%d/2) ratio %.4f", i, hit, votes[i], want);
        if (votes[i] >= 2) {
            bound[i] = hit;
            C->log(MOD_ID, "deck bound: tc#%d %p -> deck %d via bpm-correlation", i, o, hit);
        }
    }
    for (int d = 0; d < NDECK; d++) snap_bpm[d] = bpm[d];
}

/* Per TempoControl: log its mapping once (each one). */
static void *mapped_obj[MAXSEEN];

/* 0.1.12: deck from the player id. the function at 0x7f0538 builds the four DjPlayerUnits with
 * the function at 0x7f0068(slot, 0..3); the id (low byte = deck) reaches the PlayerBasisUnit ctor
 * 0x156b058 at arg+0x38 and is kept in a sub-object (ctor 0xe18b08, vtable 0x2b04fa0) at
 * [pbu+0x28]+0x10. Every step is vtable-checked; any failure = unknown (-1). */
#define VT_PID ((uintptr_t)0x2b04fa0u)
static int pid_deck[MAXSEEN] = { -1, -1, -1, -1, -1, -1, -1, -1 };
static int deck_from_pbu(uintptr_t pbu)
{
    if (!pbu || rdp(pbu) != VT_PBU) return -1;
    uintptr_t o = rdp(pbu + 0x28);
    if (!o || rdp(o) != VT_PID) return -1;
    unsigned id = *(const uint8_t *)(o + 0x10);
    return id < NDECK ? (int)id : -1;
}

static int resolve_deck(void *self, void *parent0, void *state0)
{
    (void)state0;
    int deck = -1;
    const char *how = "none";
    uintptr_t x = (uintptr_t)parent0;
    uintptr_t xvt = rdp(x), pbu = (xvt == VT_TOWNER) ? rdp(x + 0x38) : 0;
    int si0 = seen_index(self);
    if (si0 >= 0 && pid_deck[si0] >= 0) return pid_deck[si0];      /* cached player id: fast path */
    {
        int pd = deck_from_pbu(pbu);
        if (pd >= 0) { deck = pd; how = "player-id(pbu+0x28)"; if (si0 >= 0) pid_deck[si0] = pd; }
    }
    if (deck < 0 && si0 >= 0 && bound[si0] >= 0) { deck = bound[si0]; how = "bpm-correlation"; }
    if (deck < 0 && AZ_HAS(C, deck_for_ds)) {
        int a = C->deck_for_ds((void *)x), b = pbu ? C->deck_for_ds((void *)pbu) : -1;
        if (a >= 0) { deck = a; how = "core(owner)"; } else if (b >= 0) { deck = b; how = "core(pbu)"; }
    }
    if (deck < 0) how = "unknown (skipped: no seen-order guess, GUIDELINES must 1)";
    int si = seen_index(self);
    static int mapped_deck[MAXSEEN] = { -9, -9, -9, -9, -9, -9, -9, -9 };
    if (si >= 0 && si < MAXSEEN &&
        (__atomic_exchange_n(&mapped_obj[si], self, __ATOMIC_RELAXED) != self ||
         __atomic_exchange_n(&mapped_deck[si], deck, __ATOMIC_RELAXED) != deck))
        C->log(MOD_ID, "deck map: tc#%d %p owner=%p vt=%#lx pbu=%p -> deck %d via %s", si, self,
               parent0, (unsigned long)xvt, (void *)pbu, deck, how);
    return deck;
}

/* 0.1.14: range enum per deck, published by the hook, logged by the tick on change. */
static int rng_pub[NDECK] = { -1, -1, -1, -1 };
static void log_ranges(void)
{
    static int rng_seen[NDECK] = { -1, -1, -1, -1 };
    static const char *const RN[4] = { "6", "10", "16", "WIDE" };
    for (int d = 0; d < NDECK; d++) {
        int r = __atomic_load_n(&rng_pub[d], __ATOMIC_RELAXED);
        if (r == rng_seen[d]) continue;
        int was = rng_seen[d];
        rng_seen[d] = r;
        if (was < 0 || r < 0 || r > 3) continue;            /* first sighting: not a change */
        int sd = d & 1;
        C->log(MOD_ID, "range: deck %d slot %s -> %s (side %c on deck %d%s)", d + 1, was <= 3 ? RN[was] : "?",
               RN[r], sd ? 'R' : 'L', side_deck[sd] + 1,
               side_deck[sd] != d ? "; EP147 sent it to the other layer" : "");
    }
}

/* Runs on EP147's own thread whenever the slider moves or the range changes. */
static void my_recompute(void *self)
{
    orig_recompute(self);
    if (!self) return;
    seen_index(self);

    int32_t range = *(int32_t *)((char *)self + OFF_RANGE);

    void *parent0 = *(void **)((char *)self + OFF_PARENT);
    void *state0 = parent0 ? *(void **)((char *)parent0 + PARENT_STATE) : NULL;

    int show = 0;
    if (range >= 0 && range <= 2) {
        int p = __atomic_load_n(&pct[range], __ATOMIC_RELAXED);
        if (p > 0 && p != STOCK[range]) show = p;
    }
    int deck = resolve_deck(self, parent0, state0);
    if (deck >= 0 && deck < NDECK) __atomic_add_fetch(&rc_calls[deck], 1, __ATOMIC_RELAXED);
    if (deck >= 0 && deck < NDECK) __atomic_store_n(&dshow[deck], show, __ATOMIC_RELAXED);
    if (deck >= 0 && deck < NDECK) __atomic_store_n(&rng_pub[deck], (int)range, __ATOMIC_RELAXED);

    if (range < 0 || range > 2) return;                 /* WIDE/unknown: leave pitch alone */
    int p = __atomic_load_n(&pct[range], __ATOMIC_RELAXED);
    if (p <= 0 || p == STOCK[range]) return;

    float mult = (float)p / 100.0f;
    if (mult > 1.0f) mult = 1.0f;

    uint8_t sync = *((uint8_t *)self + OFF_SYNC);
    float slider = *(float *)((char *)self + OFF_SLIDER);
    float analog, applied;
    if (sync) {
        analog = applied = 0.0f;
    } else {
        /* Resolution scales with the range: 0.01% up to +/-6 (4% = 400 steps), 0.02% up to
         * +/-12, 0.05% above (the stock 10/16 step). N x step = mult keeps the cap exact. */
        float step = p <= 6 ? 0.0001f : p <= 12 ? 0.0002f : 0.0005f;
        float N = mult / step;
        analog = slider * mult;
        if (analog > 1.0f) analog = 1.0f;
        if (analog < -1.0f) analog = -1.0f;
        applied = floorf(slider * N) * step;
        if (applied > 1.0f) applied = 1.0f;
        if (applied < -1.0f) applied = -1.0f;
    }

    *(float *)((char *)self + OFF_ANALOG)  = analog;
    *(float *)((char *)self + OFF_APPLIED) = applied;

    void *par = *(void **)((char *)self + OFF_PARENT);
    if (par && pitch_sink) {
        void *adj = *(void **)((char *)par + PARENT_ADJ);
        if (adj) pitch_sink((char *)adj + ADJ_OFF, applied);
    }
    if (!__atomic_exchange_n(&logged_first, 1, __ATOMIC_RELAXED))
        C->log(MOD_ID, "first rescale: range %d -> %d%%, slider %.3f applied %.4f", (int)range, p,
               (double)slider, (double)applied);

    if (!parent0 || !st_set_tempo || !state0) return;
    st_set_tempo(state0, applied);
}

/* ---- tick: backstop for any path that changes a row outside on_change ------------------ */
static int ticked;
static void my_tick(void)
{
    static long ticks, last_hb;
    ticks++;
    if (!__atomic_exchange_n(&ticked, 1, __ATOMIC_RELAXED))
        C->log(MOD_ID, "tick running");
    {
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        long now = (long)ts.tv_sec;
        if (now - last_hb >= 15) {                  /* debug heartbeat: cadence + raw row values */
            last_hb = now;
            C->log(MOD_ID, "tick alive: %ld ticks, rows=%d/%d/%d pct=%d/%d/%d", ticks,
                   h_row[0] >= 0 ? C->row_value(h_row[0]) : -9, h_row[1] >= 0 ? C->row_value(h_row[1]) : -9,
                   h_row[2] >= 0 ? C->row_value(h_row[2]) : -9, pct[0], pct[1], pct[2]);
        }
    }
    for (int i = 0; i < 3; i++) {
        if (h_row[i] < 0) continue;
        int idx = C->row_value(h_row[i]);
        set_pct(i, idx_to_pct(i, idx), "poll");
    }

    if (h_outline >= 0) {
        int o = C->row_value(h_outline) == 1;
        if (o != lbl_outline) { lbl_outline = o; C->log(MOD_ID, "outline=%d", o); }
    }
    {
        struct timespec ts2; clock_gettime(CLOCK_MONOTONIC, &ts2);
        correlate((long)ts2.tv_sec * 1000 + ts2.tv_nsec / 1000000);
    }
    if (S[0]) {
        int enabled = h_label >= 0 ? C->row_value(h_label) == 1 : 1;
        int n = C->deck_count();
        int wm = __atomic_load_n(&wv_mode, __ATOMIC_RELAXED);
        /* 4 DECK and 2/4 DECK: one panel per deck at the 4 DECK positions */
        int vi = (n == 4 || wm == 1 || wm == 2) ? 1 : 0;
        if (vi) n = 4;
        int bpm_on = h_bpm >= 0 ? C->row_value(h_bpm) == 1 : 1;
        int bpm4 = bpm_on && h_bpm4 >= 0 && C->row_value(h_bpm4) == 1;
        update_layers();
        int key = vi | bpm_on << 1 | bpm4 << 2 | side_deck[0] << 3 | side_deck[1] << 5;
        static int logged_vi = -1;
        if (vi != logged_vi) {
            logged_vi = vi;
            C->log(MOD_ID, "view: %s (deck_count %d, layout mode %d)", vi ? "per deck" : "2 DECK (follows layer)", C->deck_count(), wm);
        }
        log_ranges();
        static int placed_key = -1;
        if (key != placed_key || vi != surf_vi) { place_all(vi); placed_key = key; }
        /* BPM x100 per deck, rounded once (the stock widget truncates to tenths). */
        static long last_bpm_ms;
        struct timespec tb; clock_gettime(CLOCK_MONOTONIC, &tb);
        long tms = (long)tb.tv_sec * 1000 + tb.tv_nsec / 1000000;
        int bpm_due = tms - last_bpm_ms >= 250;            /* deck_bpm refreshes every 250 ms */
        int changed[NDECK] = { 0 };
        if (bpm_due) {
            last_bpm_ms = tms;
            for (int d = 0; d < NDECK; d++) {
                long c = 0;
                if (bpm_on && C->deck_loaded(d) == 1) {
                    double b = C->deck_bpm(d);
                    if (b >= 20.0 && b <= 400.0) c = lround(b * 100.0);
                }
                if (c != bpm_cents[d]) { __atomic_store_n(&bpm_cents[d], c, __ATOMIC_RELAXED); changed[d] = 1; }
            }
        }
        for (int s = 0; s < NDECK; s++) {
            int r = role[s], dk = role_deck[s], on;
            if (r == ROLE_LABEL)
                /* A shown surface replaces what is under it, so a stock deck is never covered,
                 * outline or not: the firmware's own label must stay visible. */
                on = (vi == 0 || dk < n) && enabled && __atomic_load_n(&dshow[dk], __ATOMIC_RELAXED) > 0;
            else if (r == ROLE_BPM)
                on = (vi == 0 || dk < n) && bpm_cents[dk] > 0;   /* no BPM: stock digits stay visible */
            else on = 0;
            C->surface_show(S[s], on);
            if (on && (r == ROLE_LABEL || changed[dk])) C->surface_redraw(S[s]);
        }
    }
}

/* ---- page ----------------------------------------------------------------------------- */
static void on_draw(az_draw *d, az_rect a, void *user)
{
    (void)user;
    C->draw_header(d, "Custom TEMPO RANGE");
    int row = C->row_height(d), px = row / 2;
    if (px < 8) px = 8;
    if (px > 96) px = 96;
    int y = a.y + row;
    const char *lines[] = {
        "The three non-WIDE slots can be retargeted.",
        "WIDE stays at +/-100%. Pitch-scale ticks are fixed artwork.",
        "Change the values from the Mod Menu rows above; applies at once.",
        "The +/-NN label and the BPM (125.00) are overlays.",
    };
    for (int i = 0; i < 4; i++) {
        if (y + row > a.y + a.h) return;
        int lpx = px;   /* shrink a line that would run past the page's right edge */
        if (AZ_HAS(C, text_width))
            while (lpx > 8 && C->text_width(lines[i], lpx) > a.w - row / 2) lpx--;
        if (AZ_HAS(C, text_width) && C->text_width(lines[i], lpx) > a.w - row / 2) { y += row; continue; }
        C->draw_text(d, a.x + row / 4, y + row / 4, lines[i], lpx, C->color(AZ_COL_TEXT));
        y += row;
    }
}

/* ---- init ----------------------------------------------------------------------------- */
static const az_surface_v1 SURF[NDECK] = {
#define SDESC(n) { .size = sizeof(az_surface_v1), .layer = AZ_LAYER_OVER_EP147, \
                   .views = AZ_VIEW_BIT(AZ_VIEW_DECK), .on_paint = paint, .user = (void *)(intptr_t)(n) }
    SDESC(0), SDESC(1), SDESC(2), SDESC(3),
};

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, claim_hook) || !AZ_HAS(C, fw_tag)) {
        C->log(MOD_ID, "needs claim_hook");
        return 2;
    }
    const char *tag = C->fw_tag();
#ifdef AZ_HARNESS   /* TEST-ONLY (make test-harness): the stock harness reports a fake tag */
    tag = FW_TAG;
#endif
    if (!tag || strcmp(tag, FW_TAG) != 0) {
        C->log(MOD_ID, "wrong firmware tag %s (want %s)", tag ? tag : "?", FW_TAG);
        return 3;
    }

    load_layout();
    load_bpm_layout();

    if (AZ_HAS(C, register_row)) {
        for (int i = 0; i < 3; i++) {
            const az_row_v1 row = {
                .size = sizeof row, .key = KEY[i], .label = LABEL[i],
                .kind = AZ_ROW_CHOICE,
                .choices = ROW_CH[i], .n_choices = NCHOICE,
                .default_choice = DEF_CHOICE[i],
                .help = "4 / 8 / 16 for the four-step cycle with WIDE. std = firmware 6/10/16. Applies at once.",
                .on_change = on_choice, .user = (void *)(intptr_t)i,
            };
            h_row[i] = C->register_row(MOD_ID, &row);
            if (h_row[i] < 0) C->log(MOD_ID, "register_row %s refused", KEY[i]);
            if (h_row[i] >= 0) {
                int idx = C->row_value(h_row[i]);
                {
                    pct[i] = idx_to_pct(i, idx);
                    applied_pct[i] = pct[i];        /* boot value already in effect */
                }
            }
        }
        migrate_choice_rows();
        for (int i = 0; i < 3; i++) {
            if (h_row[i] >= 0 && AZ_HAS(C, row_value)) {
                int idx = C->row_value(h_row[i]);
                pct[i] = idx_to_pct(i, idx);
                applied_pct[i] = pct[i];
            }
        }
        const az_row_v1 lrow = {
            .size = sizeof lrow, .key = "label", .label = "Show range label",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
            .help = "Redraw the on-screen +/-NN TEMPO range label with the chosen value.",
        };
        if (AZ_HAS(C, row_value)) h_label = C->register_row(MOD_ID, &lrow);
        /* A row, so "tempo-range:outline = 1" in MODS/azmods.cfg (where rows live) turns it on.
         * 0.1.6 read cfg_get("outline"), the per-mod settings file, so that line was never seen. */
        const az_row_v1 orow = {
            .size = sizeof orow, .key = "outline", .label = "Label outline (align)",
            .kind = AZ_ROW_TOGGLE,
            .help = "Draws a magenta frame round each label box, to check placement.",
        };
        const az_row_v1 brow = {
            .size = sizeof brow, .key = "bpm2dp", .label = "BPM with 2 decimals",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
            .help = "Draws 125.00 over each deck's BPM (stock shows 125.0).",
        };
        if (AZ_HAS(C, row_value) && AZ_HAS(C, deck_bpm)) h_bpm = C->register_row(MOD_ID, &brow);
        static const char *const B4CH[] = { "range label", "BPM 125.00" };
        const az_row_v1 b4row = {
            .size = sizeof b4row, .key = "bpm4", .label = "4-deck view shows",
            .kind = AZ_ROW_CHOICE, .choices = B4CH, .n_choices = 2, .default_choice = 0,
            .help = "4 overlays per mod: in the 4-deck view show either the +/-NN labels or 2-decimal BPM.",
        };
        if (h_bpm >= 0) h_bpm4 = C->register_row(MOD_ID, &b4row);
        if (AZ_HAS(C, row_value)) {
            h_outline = C->register_row(MOD_ID, &orow);
            if (h_outline >= 0) lbl_outline = C->row_value(h_outline) == 1;
        }
    }

    {
        static az_screen_v1 scr;
        scr = (az_screen_v1){ .size = sizeof scr, .on_draw = on_draw };
        if (C->register_screen(MOD_ID, &scr) != 0)
            C->log(MOD_ID, "register_screen refused");
    }

    if (AZ_HAS(C, surface_create) && AZ_HAS(C, deck_count) && AZ_HAS(C, draw_text)) {
        int ok = 1;
        for (int d = 0; d < NDECK; d++) {
            S[d] = C->surface_create(MOD_ID, &SURF[d]);
            if (!S[d]) { C->log(MOD_ID, "surface_create refused for deck %d", d + 1); ok = 0; break; }
            C->surface_show(S[d], 0);
        }
        if (ok) place_all(0);
        else for (int d = 0; d < NDECK; d++) S[d] = NULL;
    }

    st_set_tempo = (st_set_tempo_fn)VA_ST_SET_TEMPO;
    pitch_sink = (st_set_tempo_fn)VA_PITCH_SINK;

#ifdef AZ_HARNESS   /* TEST-ONLY: no hooks or filters under the harness (unit-tested instead) */
    (void)my_recompute; (void)my_wv_paint; (void)on_erp_sel;
#else
    if (AZ_HAS(C, frame_filter) &&
        C->frame_filter(MOD_ID, (1u << AZ_BUS_ERP_IN) | AZ_FILTER_RAW, on_erp_sel, NULL) == 0)
        sel_filter_on = 1;
    else
        C->log(MOD_ID, "frame_filter refused: layer follows fader activity only");
    if (*(const uint32_t *)VA_COMP_POC == POC_INSN0 &&
        C->claim_hook(MOD_ID, VA_COMP_POC, (void *)my_wv_paint, (void **)&orig_wv_paint) == 0)
        C->log(MOD_ID, "layout mode: paintOverChildren hook ok (WaveformView +0x3a8)");
    else
        C->log(MOD_ID, "layout mode unknown (paintOverChildren not claimed): 2/4 DECK drawn as 2 DECK");
    if (C->claim_hook(MOD_ID, VA_RECOMPUTE, (void *)my_recompute, (void **)&orig_recompute) != 0) {
        C->log(MOD_ID, "claim_hook(recompute) refused");
        return 4;
    }
#endif
    C->log(MOD_ID, "loaded 0.1.15; pct=%d/%d/%d label=%s outline=%d bpm2dp=%d", pct[0], pct[1],
           pct[2], S[0] ? "overlay" : "off", lbl_outline,
           h_bpm >= 0 ? C->row_value(h_bpm) : -1);
    return 0;
}

#ifdef AZ_HARNESS   /* TEST-ONLY: the stock harness requires {"any", NULL}; never shipped */
static const char *const FW_TAGS[] = { "any", NULL };
#else
static const char *const FW_TAGS[] = { FW_TAG, NULL };
#endif
static const az_mod_v1 MOD = {
    .abi = AZ_MOD_ABI, .size = sizeof(az_mod_v1), .id = MOD_ID,
    .fw_tags = FW_TAGS, .init = init, .tick = my_tick, .on_frame = NULL,
};

const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
