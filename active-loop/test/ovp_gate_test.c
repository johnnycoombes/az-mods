/* Overview-pulse colour gates + repaint throttle + deferred suppress + update-fresh live + mutate 10 Hz / early-out / ON-edge defer. */
#include "../active-loop.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_allow(const char *how) { assert(ovp_colour_allowed(how) == 1); }
static void expect_deny(const char *how) { assert(ovp_colour_allowed(how) == 0); }

/* Stub firmware CueProperty operations only; run the real picker/exit code. */
static void test_cue_copy(void *dst, const void *src) { memcpy(dst, src, CUE_SIZE); }
static void test_cue_dtor(void *cue) { (void)cue; }
static int test_is_loop(void *cue) { return cue_out(cue) > cue_in(cue); }

static unsigned test_ui_moves, test_ui_repaints;
static void test_repaint(void *comp) { (void)comp; test_ui_repaints++; }
static void test_move(void *comp, int x, int y)
{
    test_ui_moves++;
    *(int32_t *)((uint8_t *)comp + 0x20) = x;
    *(int32_t *)((uint8_t *)comp + 0x24) = y;
}

static void test_exit_at(int64_t pos)
{
    pos_now[0] = pos;
    for (int n = 0; n < 3; n++) watch_loops();
    assert(pos_now[0] == pos); /* advance selects only: no playhead jump */
}

int main(void)
{
    const uint64_t t0 = 10000000000ull;
    uintptr_t bars[8];
    uint64_t last;
    int i, s0, s1;

    expect_allow("perdeck+modeobj");
    expect_allow("sdk(self)");
    expect_deny("loop-armed");
    expect_deny("clash");
    expect_deny("none");
    expect_deny("waveform");

    assert(ovp_should_mutate(0, "perdeck+modeobj", 0, 0, 1, 1) == 1);
    assert(ovp_should_mutate(0, "loop-armed", 0, 0, 1, 1) == 0);
    assert(ovp_should_mutate(-1, "perdeck+modeobj", 0, 0, 1, 1) == 0);
    assert(ovp_should_mutate(0, "perdeck+modeobj", 0, 0, 0, 1) == 0);
    assert(ovp_should_mutate(0, "perdeck+modeobj", 0, 0, 1, 0) == 0); /* stale update: never mutate */
    assert(ovp_should_mutate(0, "perdeck+modeobj", 1, 0, 1, 1) == 0);
    assert(ovp_should_mutate(0, "perdeck+modeobj", 1, 1, 0, 0) == 1);
    assert(ovp_should_mutate(-1, "none", 1, 1, 0, 0) == 1);

    assert(ovp_ends_eq(78, 415720, 78, 415720) == 1);

    last = t0;
    assert(ovp_repaint_due(last, t0 + 1ull, 0) == 0);
    assert(ovp_repaint_due(last, t0 + OVP_REPAINT_MIN_NS, 0) == 1);
    assert(ovp_repaint_due(last, t0 + 1ull, 1) == 1);
    for (i = 0; i < 50; i++)
        assert(ovp_repaint_due(last, t0 + (uint64_t)i * 1000000ull, 0) == 0);

    memset(bars, 0, sizeof bars);
    bars[0] = 0x11110000ull;
    assert(ovp_rep_find(bars, 8, 0x11110000ull) == 0);
    assert(ovp_rep_find(bars, 8, 0x33330000ull) == -1);

    memset(ovp_rep_bar, 0, sizeof ovp_rep_bar);
    s0 = ovp_rep_slot(0xaaaa0000ull);
    s1 = ovp_rep_slot(0xaaaa0000ull);
    assert(s0 == s1);

    assert(ovp_alb_of(0, 1) == 0);

    /* Sticky live bar */
    memset(ovp_live_bar, 0, sizeof ovp_live_bar);
    __atomic_store_n(&ovp_live_bar[0], 0x1000ull, __ATOMIC_RELAXED);
    assert(ovp_bar_is_live(0, 0x1000ull) == 1);
    assert(ovp_bar_is_live(0, 0x2000ull) == 0);
    __atomic_store_n(&ovp_live_bar[0], 0x2000ull, __ATOMIC_RELAXED);
    assert(ovp_bar_is_live(0, 0x1000ull) == 0);
    assert(ovp_bar_is_live(0, 0x2000ull) == 1);

    /* Deferred suppress: mark before "async" paint; still ours after call returns */
    memset(ovp_sup_bar, 0, sizeof ovp_sup_bar);
    memset(ovp_sup_until, 0, sizeof ovp_sup_until);
    assert(ovp_paint_is_ours(0xabc0ull, t0) == 0);
    ovp_suppress_bar_until(0xabc0ull, t0);
    assert(ovp_paint_is_ours(0xabc0ull, t0) == 1);
    assert(ovp_paint_is_ours(0xabc0ull, t0 + 1ull) == 1);
    assert(ovp_paint_is_ours(0xabc0ull, t0 + OVP_SUPPRESS_NS - 1) == 1);
    assert(ovp_paint_is_ours(0xabc0ull, t0 + OVP_SUPPRESS_NS) == 0);
    assert(ovp_paint_is_ours(0xdef0ull, t0 + 1ull) == 0); /* other bar */
    /* Extend, don't shorten */
    ovp_suppress_bar_until(0xabc0ull, t0);
    ovp_suppress_bar_until(0xabc0ull, t0 + 50000000ull);
    assert(ovp_paint_is_ours(0xabc0ull, t0 + 50000000ull + OVP_SUPPRESS_NS - 1) == 1);

    /* 10 Hz mutate throttle (align with repaint cap) */
    last = t0;
    assert(ovp_mutate_due(0, t0) == 1);
    assert(ovp_mutate_due(last, t0 + 1ull) == 0);
    assert(ovp_mutate_due(last, t0 + OVP_MUTATE_MIN_NS - 1) == 0);
    assert(ovp_mutate_due(last, t0 + OVP_MUTATE_MIN_NS) == 1);
    for (i = 0; i < 50; i++)
        assert(ovp_mutate_due(last, t0 + (uint64_t)i * 1000000ull) == 0);

    /* Early-out: skip resolve when Pulse is ON but nothing can change colour */
    assert(ovp_mutate_work_pending(0, 0, 0, 0, 0) == 0); /* no update slots */
    assert(ovp_mutate_work_pending(1, 0, 0, 0, 0) == 0); /* slots but idle */
    assert(ovp_mutate_work_pending(1, 1, 0, 0, 0) == 1); /* flagged */
    assert(ovp_mutate_work_pending(1, 0, 1, 0, 0) == 1); /* sticky live */
    assert(ovp_mutate_work_pending(1, 0, 0, 1, 0) == 1); /* pending solid restore */
    assert(ovp_mutate_work_pending(0, 1, 1, 1, 0) == 0); /* no slots to paint */
    assert(ovp_mutate_work_pending(0, 0, 0, 0, 1) == 0); /* OFF, nothing mutated */
    assert(ovp_mutate_work_pending(0, 0, 0, 1, 1) == 1); /* OFF restore */
    assert(ovp_mutate_work_pending(1, 0, 0, 1, 1) == 1);

    /* ON-edge defer: menu-tap tick (0) never ready; log after 1 tick+15ms; mutate after 2+30ms */
    assert(ovp_on_edge_ready(0, t0, 5, 1, OVP_LOG_ON_DEFER_NS) == 0);
    assert(ovp_on_edge_ready(t0, t0, 0, 1, OVP_LOG_ON_DEFER_NS) == 0);
    assert(ovp_on_edge_ready(t0, t0 + OVP_LOG_ON_DEFER_NS, 0, 1, OVP_LOG_ON_DEFER_NS) == 0);
    assert(ovp_on_edge_ready(t0, t0 + OVP_LOG_ON_DEFER_NS, 1, 1, OVP_LOG_ON_DEFER_NS) == 1);
    assert(ovp_on_edge_ready(t0, t0 + OVP_MUTATE_ON_DEFER_NS, 1, 2, OVP_MUTATE_ON_DEFER_NS) == 0);
    assert(ovp_on_edge_ready(t0, t0 + OVP_MUTATE_ON_DEFER_NS - 1, 2, 2, OVP_MUTATE_ON_DEFER_NS) == 0);
    assert(ovp_on_edge_ready(t0, t0 + OVP_MUTATE_ON_DEFER_NS, 2, 2, OVP_MUTATE_ON_DEFER_NS) == 1);
    assert(ovp_on_edge_ready(t0, t0 - 1ull, 2, 1, 0) == 0);

    /* Fresh organic paints only (pointer with seen==0 is not fresh) */
    {
        uintptr_t pp[4];
        uint64_t ps[4];
        uint8_t mut[8];
        memset(pp, 0, sizeof pp);
        memset(ps, 0, sizeof ps);
        memset(mut, 0, sizeof mut);
        assert(ovp_fresh_paint_notes(pp, ps, 4, 0) == 0);
        assert(ovp_any_ptr(pp, 4) == 0);
        assert(ovp_any_u8(mut, 8) == 0);
        pp[0] = 0x1000ull;
        assert(ovp_any_ptr(pp, 4) == 1);
        assert(ovp_fresh_paint_notes(pp, ps, 4, 0) == 0); /* no seen */
        ps[0] = t0;
        assert(ovp_fresh_paint_notes(pp, ps, 4, 0) == 1);
        assert(ovp_fresh_paint_notes(pp, ps, 4, t0) == 0);
        assert(ovp_fresh_paint_notes(pp, ps, 4, t0 - 1ull) == 1);
        mut[2] = 1;
        assert(ovp_any_u8(mut, 8) == 1);
    }

    /* al0216i: live = newest upd_seen within 0.5 s; paint only breaks ties.
     * Hidden SOURCE (calls=0) must not stay live even with a newer organic paint. */
    {
        const uint64_t T = t0;
        uintptr_t src = 0x3730000ull, wav = 0x5900000ull;
        uintptr_t d1[2], d2[2];
        uint64_t u1[2], p1[2], u2[2], p2[2];

        assert(ovp_upd_fresh(0, T) == 0);
        assert(ovp_upd_fresh(T, T) == 1);
        assert(ovp_upd_fresh(T, T + OVP_UPD_FRESH_NS) == 1);
        assert(ovp_upd_fresh(T, T + OVP_UPD_FRESH_NS + 1) == 0);
        assert(ovp_upd_fresh(T - 1ull, T) == 1);

        d1[0] = src; d1[1] = wav;
        /* Active WAVEFORM update wins over stale SOURCE, even if SOURCE just painted */
        u1[0] = T - OVP_UPD_FRESH_NS - 1; /* SOURCE stale */
        u1[1] = T;                         /* WAVEFORM fresh */
        p1[0] = T;                         /* rare organic paint on idle SOURCE */
        p1[1] = 0;
        assert(ovp_pick_live_bar(d1, u1, p1, 2, T) == wav);
        assert(ovp_live_better(u1[1], p1[1], u1[0], p1[0], T) == 1);
        assert(ovp_live_better(u1[0], p1[0], u1[1], p1[1], T) == 0);

        /* Newest fresh update wins */
        u1[0] = T - 1000ull;
        u1[1] = T;
        p1[0] = 0; p1[1] = 0;
        assert(ovp_pick_live_bar(d1, u1, p1, 2, T) == wav);

        /* Organic paint breaks ties when upd_seen matches */
        u1[0] = T; u1[1] = T;
        p1[0] = T - 10ull; p1[1] = T;
        assert(ovp_pick_live_bar(d1, u1, p1, 2, T) == wav);
        p1[0] = T; p1[1] = T - 10ull;
        assert(ovp_pick_live_bar(d1, u1, p1, 2, T) == src);

        /* All stale → no live (solid restore of idle tree) */
        u1[0] = T - OVP_UPD_FRESH_NS - 1;
        u1[1] = T - OVP_UPD_FRESH_NS - 1;
        p1[0] = T; p1[1] = T;
        assert(ovp_pick_live_bar(d1, u1, p1, 2, T) == 0);

        /* Two decks independent: d1 stale, d2 source fresh */
        d2[0] = 0x200373ull; d2[1] = 0x200590ull;
        u2[0] = T; u2[1] = 0;
        p2[0] = 0; p2[1] = 0;
        assert(ovp_pick_live_bar(d1, u1, p1, 2, T) == 0);
        assert(ovp_pick_live_bar(d2, u2, p2, 2, T) == d2[0]);
        assert(ovp_pick_live_bar(d2, u2, p2, 2, T) != src);
        assert(ovp_pick_live_bar(d2, u2, p2, 2, T) != wav);

        /* After flip: d1 waveform fresh, d2 still its own source */
        u1[1] = T;
        assert(ovp_pick_live_bar(d1, u1, p1, 2, T) == wav);
        assert(ovp_pick_live_bar(d2, u2, p2, 2, T) == d2[0]);

        /* Empty / no paint array */
        assert(ovp_pick_live_bar(d1, u1, NULL, 2, T) == wav);
        assert(ovp_pick_live_bar(NULL, u1, p1, 2, T) == 0);
        assert(ovp_pick_live_bar(d1, u1, p1, 0, T) == 0);
    }


    /* Breath gate is the ACTIVE flag, not picker arm (act_ok && !spent).
     * JC: pulse the overview marker of a Memory/Cue loop flagged ACTIVE;
     * al0216j Cola ALB fields were playhead:duration (78:415720), not the
     * flagged loop 17354310:18048312, so unmatched extents still breathe. */
    {
        const int64_t cola_in = 17354310LL, cola_out = 18048312LL;
        const int64_t alb_runtime = 78, alb_dur = 415720; /* al0216j Cola */

        act_ok[0] = 1; spent[0] = 0;
        assert(ovp_deck_armed(0) == 1);
        assert(ovp_deck_has_flagged(0) == 0);
        assert(ovp_flagged_eligible(1, 0, 0, 0) == 0); /* vis, no flag (Spread Love) */
        assert(ovp_flagged_eligible(1, 1, 0, 0) == 1); /* vis + flag, extents not loop times */
        assert(ovp_flagged_eligible(1, 1, 1, 0) == 1); /* extents match flagged */
        assert(ovp_flagged_eligible(1, 1, 0, 1) == 0); /* extents match a non-ACTIVE loop */
        assert(ovp_flagged_eligible(0, 1, 0, 0) == 0); /* hidden ALB */
        assert(ovp_flagged_eligible(1, 1, 1, 1) == 1); /* flagged match wins */
        assert(ovp_ends_visible(INT64_MAX, 415720) == 0);
        assert(ovp_ends_visible(78, INT64_MAX) == 0);
        assert(ovp_ends_visible(78, 415720) == 1);

        memset(mem_ok[0], 0, sizeof mem_ok[0]);
        memset(mem_flag[0], 0, sizeof mem_flag[0]);
        memset(mem_buf[0][0], 0, CUE_SIZE);
        mem_n[0] = 1;
        mem_ok[0][0] = 1;
        mem_flag[0][0] = 1;
        *(int64_t *)(mem_buf[0][0] + CUE_IN_TIME) = cola_in;
        *(int64_t *)(mem_buf[0][0] + CUE_OUT_TIME) = cola_out;
        act_ok[0] = 0; spent[0] = 1; /* picker not armed / already spent */
        assert(ovp_deck_armed(0) == 0);
        assert(ovp_deck_has_flagged(0) == 1);
        assert(ovp_any_flagged_deck() == 1);
        assert(ovp_matches_flagged_ends(0, cola_in, cola_out) == 1);
        assert(ovp_matches_flagged_ends(0, alb_runtime, alb_dur) == 0);
        assert(ovp_matches_other_loop_ends(0, alb_runtime, alb_dur) == 0);
        assert(ovp_flagged_eligible(ovp_ends_visible(alb_runtime, alb_dur),
                                    ovp_deck_has_flagged(0),
                                    ovp_matches_flagged_ends(0, alb_runtime, alb_dur),
                                    ovp_matches_other_loop_ends(0, alb_runtime, alb_dur)) == 1);

        /* Live CueProperty+0xe1 without mem_flag still counts */
        mem_flag[0][0] = 0;
        mem_buf[0][0][CP_FLAG] = 1;
        assert(ovp_deck_has_flagged(0) == 1);

        /* ANLZ PCPT status 4 with no mem_* yet (load before merge / no auto-arm) */
        mem_ok[0][0] = 0;
        mem_n[0] = 0;
        mem_buf[0][0][CP_FLAG] = 0;
        assert(ovp_deck_has_flagged(0) == 0);
        anlz_state[0] = 2;
        anlz_res[0].n = 1;
        anlz_res[0].e[0].flag = 1;
        anlz_res[0].e[0].hot = 0;
        anlz_res[0].e[0].in_ms = 393522;
        anlz_res[0].e[0].out_ms = 409259;
        assert(ovp_deck_has_flagged(0) == 1);
        assert(ovp_matches_flagged_ends(0, cola_in, cola_out) == 1); /* ms*44.1 */

        /* Non-ACTIVE stored loop: ALB extents of THAT loop stay solid */
        anlz_state[0] = 0;
        anlz_res[0].n = 0;
        anlz_res[0].e[0].flag = 0;
        mem_n[0] = 2;
        mem_ok[0][0] = 1; mem_flag[0][0] = 1;
        mem_buf[0][0][CP_FLAG] = 1;
        *(int64_t *)(mem_buf[0][0] + CUE_IN_TIME) = cola_in;
        *(int64_t *)(mem_buf[0][0] + CUE_OUT_TIME) = cola_out;
        mem_ok[0][1] = 1; mem_flag[0][1] = 0;
        mem_buf[0][1][CP_FLAG] = 0;
        *(int64_t *)(mem_buf[0][1] + CUE_IN_TIME) = 1000;
        *(int64_t *)(mem_buf[0][1] + CUE_OUT_TIME) = 2000;
        assert(ovp_matches_other_loop_ends(0, 1000, 2000) == 1);
        assert(ovp_flagged_eligible(1, 1, 0, 1) == 0);
        assert(ovp_matches_flagged_ends(0, 1000, 2000) == 0);

        mem_n[0] = 0;
        memset(mem_ok[0], 0, sizeof mem_ok[0]);
        memset(mem_flag[0], 0, sizeof mem_flag[0]);
        act_ok[0] = 0; spent[0] = 0;
        anlz_state[0] = 0;
        anlz_res[0].n = 0;
    }

    /* Flag move + unflag: SHIFT+pad pick overrides the stored flag; toggle-off
     * hides even if the rekordbox flag remains; SHIFT+DELETE (picked, not armed)
     * falls back to the stored flag. */
    {
        const int64_t cola_in = 17354310LL, cola_out = 18048312LL;
        const int64_t cue_in = 2646000LL, cue_out = 3572100LL; /* 1:00-1:21 */
        int64_t in = 0, out = 0;
        int x;

        assert(ovp_pos_for_denom(cola_in, 415720) == 393522);
        assert(ovp_pos_for_denom(cola_in, 18000000LL) == cola_in);
        assert(ovp_mark_x(0, 100, 50, 100) == 50);
        assert(ovp_mark_x(24, 590, 0, 415720) == 24);
        assert(ovp_mark_x(24, 590, 415720, 415720) == 24 + 590);
        assert(ovp_mark_x(24, 590, 500000, 415720) == 24 + 590);
        x = ovp_mark_x(24, 590, 393522, 415720);
        assert(x >= 550 && x <= 590); /* ~582 with origin 24, width 590 */

        /* picked + armed to a different cue loop: current is the pick */
        assert(ovp_current_mark(1, 0, 1, 0, cue_in, cue_out, 1, cola_in, cola_out, &in, &out) == 1);
        assert(in == cue_in && out == cue_out);
        assert(ovp_place_mode(1, 0, 1, 0, cue_in, cue_out, 1, cola_in, cola_out, &in, &out) == 1);
        assert(in == cue_in && out == cue_out);
        assert(ovp_flagged_eligible(1, 1, 0, 0) == 1); /* vis + current, extents unmatched */

        /* unflag (SHIFT+pad toggle): no mark, hide, even with stored flag */
        assert(ovp_current_mark(1, 1, 0, 0, cue_in, cue_out, 1, cola_in, cola_out, &in, &out) == 0);
        assert(ovp_place_mode(1, 1, 0, 0, cue_in, cue_out, 1, cola_in, cola_out, &in, &out) == 2);
        assert(ovp_flagged_eligible(1, 0, 0, 0) == 0);

        /* SHIFT+DELETE: picked but picker cleared -> fall back to stored flag */
        assert(ovp_current_mark(1, 0, 0, 0, cue_in, cue_out, 1, cola_in, cola_out, &in, &out) == 1);
        assert(in == cola_in && out == cola_out);
        assert(ovp_place_mode(1, 0, 0, 0, cue_in, cue_out, 1, cola_in, cola_out, &in, &out) == 0);

        /* load-time flag, no user pick: stock */
        assert(ovp_current_mark(0, 0, 0, 1, 0, 0, 1, cola_in, cola_out, &in, &out) == 1);
        assert(in == cola_in && out == cola_out);
        assert(ovp_place_mode(0, 0, 0, 1, 0, 0, 1, cola_in, cola_out, &in, &out) == 0);

        /* pick of the same loop as the stored flag: no override (stock already there) */
        assert(ovp_place_mode(1, 0, 1, 0, cola_in, cola_out, 1, cola_in, cola_out, &in, &out) == 0);

        /* deck wrappers: stored flag then pick then unflag then stock */
        mem_n[0] = 1;
        mem_ok[0][0] = 1; mem_flag[0][0] = 1; mem_live[0][0] = 1; mem_syn[0][0] = 0;
        *(int64_t *)(mem_buf[0][0] + CUE_IN_TIME) = cola_in;
        *(int64_t *)(mem_buf[0][0] + CUE_OUT_TIME) = cola_out;
        ovp_sess[0] = OVP_SESS_STOCK;
        act_ok[0] = 0; spent[0] = 0;
        assert(ovp_deck_current_ends(0, &in, &out) == 1);
        assert(in == cola_in && out == cola_out);
        assert(ovp_deck_place_mode(0, &in, &out) == 0);
        assert(ovp_any_current_deck() == 1);

        ovp_sess[0] = OVP_SESS_PICKED;
        act_ok[0] = 1; spent[0] = 0;
        active_in[0] = cue_in; active_out[0] = cue_out;
        assert(ovp_deck_current_ends(0, &in, &out) == 1);
        assert(in == cue_in && out == cue_out);
        assert(ovp_deck_place_mode(0, &in, &out) == 1);

        ovp_sess[0] = OVP_SESS_CLEARED;
        act_ok[0] = 0; spent[0] = 0;
        assert(ovp_deck_current_ends(0, &in, &out) == 0);
        assert(ovp_deck_place_mode(0, &in, &out) == 2);
        assert(ovp_flagged_eligible(1, ovp_deck_current_ends(0, &in, &out), 0, 0) == 0);

        ovp_sess[0] = OVP_SESS_STOCK;
        assert(ovp_deck_current_ends(0, &in, &out) == 1); /* flag remains */
        assert(ovp_deck_place_mode(0, &in, &out) == 0);

        mem_n[0] = 0;
        memset(mem_ok[0], 0, sizeof mem_ok[0]);
        memset(mem_flag[0], 0, sizeof mem_flag[0]);
        act_ok[0] = 0; spent[0] = 0;
        active_in[0] = 0; active_out[0] = 0;
        ovp_sess[0] = OVP_SESS_STOCK;
    }

    /* Run the real exit -> auto-advance path, including the important case
     * where the next memory loop IS the stored flag (mode 0 must restore X). */
    {
        uint8_t cue[CUE_SIZE] __attribute__((aligned(64))) = {0};
        int64_t in = 0, out = 0;
        int dest = -1, mode;
        uint32_t gen;
        const uintptr_t alb = 0x12340000;
        fw_cue_copy = test_cue_copy;
        fw_cue_dtor = test_cue_dtor;
        fw_is_loop = test_is_loop;
        cfg_bits = CFG_ENABLE | CFG_AUTOADV;
        tick_no = 1000;
        pos_seq[0] = 1;
        *(int64_t *)(cue + CUE_IN_TIME) = 100000;
        *(int64_t *)(cue + CUE_OUT_TIME) = 200000;
        mem_n[0] = 1;
        mem_ok[0][0] = mem_live[0][0] = mem_flag[0][0] = 1;
        *(int64_t *)(mem_buf[0][0] + CUE_IN_TIME) = 400000;
        *(int64_t *)(mem_buf[0][0] + CUE_OUT_TIME) = 500000;
        pend_op[0] = OP_IDLE;
        assert(pick_and_set(0, cue, 1, 1, "test SHIFT+pad") == 0);
        ovp_sess_set(0, OVP_SESS_PICKED);
        assert(ovp_deck_current_ends(0, &in, &out) && in == 100000);
        assert(ovp_deck_place_mode(0, &in, &out) == 1);
        gen = ovp_sess_gen;
        ovp_px_store(alb, 100, gen); /* actual marker at picked cue X */
        assert(ovp_px_overridden(alb));
        pend_op[0] = OP_IDLE; /* queued SET applied by firmware */
        pos_now[0] = 150000;
        watch_loops(); /* observed inside */
        test_exit_at(200000 + EXIT_TOL + 1);
        assert(act_ok[0] && !spent[0] && !act_src_hot[0]);
        assert(active_in[0] == 400000 && act_src_idx[0] == 0);
        assert(ovp_sess[0] == OVP_SESS_PICKED && ovp_sess_gen != gen);
        assert(pend_op[0] == OP_SET);
        assert(ovp_deck_current_ends(0, &in, &out) && in == 400000);
        mode = ovp_deck_place_mode(0, &in, &out);
        assert(mode == 0); /* target is the stock flag */
        assert(ovp_place_destination(mode, ovp_px_overridden(alb), 400, 0, &dest));
        assert(dest == 400); /* physically restore stock, not stale cue X 100 */
        ovp_px_forget(alb);
        assert(!ovp_place_destination(mode, ovp_px_overridden(alb), 400, 0, &dest));
        /* Stable stock return requires only one move, not a repaint storm. */

        /* A subsequent unflagged MEMORY target also follows the armed picker,
         * including when the previous session was STOCK. */
        mem_flag[0][0] = 0;
        mem_buf[0][0][CP_FLAG] = 0;
        *(int64_t *)(mem_buf[0][0] + CUE_IN_TIME) = 700000;
        *(int64_t *)(mem_buf[0][0] + CUE_OUT_TIME) = 800000;
        ovp_sess_set(0, OVP_SESS_STOCK);
        pend_op[0] = OP_IDLE;
        pos_now[0] = 450000; watch_loops();
        test_exit_at(500000 + EXIT_TOL + 1);
        assert(active_in[0] == 700000 && !act_src_hot[0]);
        assert(ovp_sess[0] == OVP_SESS_PICKED);
        assert(ovp_deck_current_ends(0, &in, &out) && in == 700000);
        mode = ovp_deck_place_mode(0, &in, &out);
        assert(mode == 1);
        assert(ovp_place_destination(mode, 0, 400, 700, &dest) && dest == 700);

        /* No next target: hide rather than revive a stored flag behind us. */
        mem_flag[0][0] = 1; /* old flag is now behind playhead at terminal exit */
        pend_op[0] = OP_IDLE;
        pos_now[0] = 750000; watch_loops();
        test_exit_at(800000 + EXIT_TOL + 1);
        assert(spent[0] && pend_op[0] == OP_CLEAR);
        assert(ovp_sess[0] == OVP_SESS_CLEARED);
        assert(!ovp_deck_current_ends(0, &in, &out));
        assert(ovp_deck_place_mode(0, &in, &out) == 2);
        assert(!ovp_place_destination(2, 1, 400, 700, &dest));

        /* Explicit SHIFT+pad toggle-off remains hidden as well. */
        spent[0] = 0; act_ok[0] = 0;
        ovp_sess_set(0, OVP_SESS_CLEARED);
        assert(!ovp_deck_current_ends(0, &in, &out));
        assert(ovp_deck_place_mode(0, &in, &out) == 2);
        mem_n[0] = 0;
        memset(mem_ok[0], 0, sizeof mem_ok[0]);
        pend_op[0] = OP_IDLE;
    }

    /* Four global decks, two overview trees each. No firmware calls: heap-like
     * fixtures reproduce the RE ALB/PIS layouts and run real note/resolve/live/
     * mutate paths. Repaint deadlines are held closed so no fixed-VA UI calls. */
    {
        uint8_t pis[0x10 + NDECK * PERDECK_STRIDE] __attribute__((aligned(64))) = {0};
        uint8_t objects[NDECK * 2][0x300] __attribute__((aligned(64))) = {{0}};
        uint8_t managed[NDECK][0x100] __attribute__((aligned(64))) = {{0}};
        const uint64_t T = now_ns();
        const uint32_t breath = 0xff550000u;
        uintptr_t base = (uintptr_t)pis + 0x10;
        int64_t in = 0, out = 0;
        const char *how;
        assert(NDECK == 4 && OVP_SLOTS >= 8);
        ovp_ui_repaint = test_repaint;
        ovp_ui_move = test_move;
        test_ui_moves = test_ui_repaints = 0;
        memset(ovp_upd_ptr, 0, sizeof ovp_upd_ptr);
        memset(ovp_upd_seen, 0, sizeof ovp_upd_seen);
        memset(ovp_paint_ptr, 0, sizeof ovp_paint_ptr);
        memset(ovp_paint_seen, 0, sizeof ovp_paint_seen);
        memset(ovp_live_bar, 0, sizeof ovp_live_bar);
        memset(ovp_rep_bar, 0, sizeof ovp_rep_bar);
        memset(ovp_rep_mut, 0, sizeof ovp_rep_mut);
        memset(ovp_rep_rebind, 0, sizeof ovp_rep_rebind);
        memset(ovp_bound_alb, 0, sizeof ovp_bound_alb);
        ovp_note_dropped = 0;
        *(uintptr_t *)pis = PISCTRL_VT;
        for (int d = 0; d < NDECK; d++) {
            uintptr_t blk = base + d * PERDECK_STRIDE;
            *(uint64_t *)blk = d;
            *(uintptr_t *)(blk + PIS_SP58) = (uintptr_t)managed[d];
            *(int64_t *)(managed[d] + ALB_LOOP_IN_OFF) = 78;
            *(int64_t *)(managed[d] + ALB_LOOP_OUT_OFF) = 415720;
            mem_n[d] = 0;
            anlz_state[d] = 0;
            ovp_sess[d] = OVP_SESS_PICKED;
            act_ok[d] = 1; spent[d] = 0;
            active_in[d] = (int64_t)(d + 1) * 2000000;
            active_out[d] = active_in[d] + 500000;
            for (int tree = 0; tree < 2; tree++) {
                /* First four notes = decks 1/2 SOURCE+WAVEFORM (old full table).
                 * Deck-3/4 notes must still be enrolled in slots 4..7. */
                int k = d * 2 + tree;
                uintptr_t alb = (uintptr_t)objects[k], bar = alb + ALB_BAR_OFF;
                *(uintptr_t *)alb = VT_ALB;
                *(uintptr_t *)(alb + ALB_BAR_OFF) = VT_BAR;
                *(uintptr_t *)(alb + ALB_P50_OFF) = blk + PIS_SP50;
                *(uintptr_t *)(alb + ALB_P58_OFF) = blk + PIS_SP58;
                *(uint32_t *)(bar + BAR_COLOUR_OFF) = OVP_SOLID_RED;
                *(uint32_t *)(bar + BAR_COMPONENT_OFF + 0x28) = 2;
                *(uint32_t *)(bar + BAR_X_OFF) = 582;
                *(uint32_t *)(alb + ALB_WIDTH_OFF) = 590;
                *(uint32_t *)(alb + ALB_ORIGIN_OFF) = 24;
                ovp_note(ovp_upd_ptr, ovp_upd_col, ovp_upd_n, ovp_upd_seen, alb, OVP_SOLID_RED);
                assert(ovp_upd_ptr[k] == alb);
                ovp_upd_seen[k] = tree ? T : T - OVP_UPD_FRESH_NS - 1;
                how = "none";
                assert(ovp_deck_resolve(alb, 1, &how) == d);
                assert(ovp_colour_allowed(how));
                assert(ovp_deck_fast(alb) == d); /* placement agrees */
                assert(ovp_deck_current_ends(d, &in, &out) && in == active_in[d]);
                assert(ovp_deck_place_mode(d, &in, &out) == 1);
                ovp_alb_apply_place((void *)alb);
                int x = ovp_mark_x(24, 590, ovp_pos_for_denom(active_in[d], 415720), 415720);
                assert(*(int32_t *)(bar + BAR_COMPONENT_OFF + 0x20) == x - 1);
                unsigned moves = test_ui_moves;
                ovp_alb_apply_place((void *)alb);
                assert(test_ui_moves == moves); /* stable placement is cheap */
                int r = ovp_rep_slot(bar);
                ovp_rep_ns[r] = T; /* prevent fixed-VA Component::repaint in host */
            }
        }
        assert(ovp_note_dropped == 0);
        ovp_refresh_live_bars(T);
        for (int d = 0; d < NDECK; d++) {
            uintptr_t alb = (uintptr_t)objects[d * 2 + 1];
            assert(ovp_bar_is_live(d, alb + ALB_BAR_OFF));
            assert(ovp_alb_is_flagged_marker(d, alb));
            assert(ovp_should_mutate(d, "perdeck+pis", 0, 0, 1, 1));
        }
        /* Visibility fields can accidentally equal another deck's loop.
         * They are NOT cue extents: cannot veto a structural deck-3 binding. */
        mem_n[0] = 1; mem_ok[0][0] = mem_flag[0][0] = 1;
        *(int64_t *)(mem_buf[0][0] + CUE_IN_TIME) = 78;
        *(int64_t *)(mem_buf[0][0] + CUE_OUT_TIME) = 415720;
        how = "none";
        assert(ovp_deck_resolve((uintptr_t)objects[5], 1, &how) == 2);
        assert(ovp_colour_allowed(how));
        assert(ovp_alb_is_flagged_marker(2, (uintptr_t)objects[5]));
        mem_n[0] = 0; mem_ok[0][0] = mem_flag[0][0] = 0;
        assert(!ovp_should_mutate(NDECK, "perdeck+pis", 0, 0, 1, 1));
        ovp_mutate_alb_slots(breath, OVP_SOLID_RED, 0, T + 1);
        for (int d = 0; d < NDECK; d++) {
            assert(*(uint32_t *)(objects[d * 2 + 1] + ALB_COLOUR_OFF) == breath);
            assert(*(uint32_t *)(objects[d * 2] + ALB_COLOUR_OFF) == OVP_SOLID_RED);
        }

        /* Reuse the same two ALBs for 1/2 -> 3/4, then 3/4 -> 1/2.
         * Pulse must rebind like placement, not retain a per-Bar deck cache. */
        for (int flip = 0; flip < 2; flip++) {
            for (int side = 0; side < 2; side++) {
                int d = side + (flip == 0 ? 2 : 0);
                uintptr_t alb = (uintptr_t)objects[side * 2 + 1];
                uintptr_t bar = alb + ALB_BAR_OFF;
                uintptr_t blk = base + d * PERDECK_STRIDE;
                uint32_t gen = ovp_sess_gen;
                ovp_px_store(alb, 44, gen);
                int prior_r = ovp_rep_slot(bar);
                uint64_t deadline = ovp_rep_ns[prior_r];
                *(uintptr_t *)(alb + ALB_P50_OFF) = blk + PIS_SP50;
                *(uintptr_t *)(alb + ALB_P58_OFF) = blk + PIS_SP58;
                ovp_upd_seen[side * 2 + 1] = T + 10 + flip;
                assert(ovp_deck_fast(alb) == d);
                assert(ovp_rep_ns[prior_r] == deadline); /* retain repaint cap */
                assert(ovp_px_cached(alb, gen) == INT32_MIN);
                assert(ovp_px_overridden(alb)); /* stock restore evidence retained */
                how = "none";
                assert(ovp_deck_resolve(alb, 1, &how) == d);
                assert(ovp_deck_place_mode(d, &in, &out) == 1 && in == active_in[d]);
                ovp_alb_apply_place((void *)alb);
                int x = ovp_mark_x(24, 590, ovp_pos_for_denom(active_in[d], 415720), 415720);
                assert(*(int32_t *)(bar + BAR_COMPONENT_OFF + 0x20) == x - 1);
                int r = ovp_rep_slot(bar);
                assert(ovp_rep_rebind[r]); /* prior owner's colour needs solid pass */
                for (int other = 0; other < NDECK; other++)
                    assert(!ovp_bar_is_live(other, bar));
            }
            ovp_mutate_alb_slots(breath, OVP_SOLID_RED, 0, T + 20 + flip);
            for (int side = 0; side < 2; side++)
                assert(*(uint32_t *)(objects[side * 2 + 1] + ALB_COLOUR_OFF) == OVP_SOLID_RED);
            ovp_mutate_alb_slots(breath, OVP_SOLID_RED, 0, T + 30 + flip);
            for (int side = 0; side < 2; side++) {
                int d = side + (flip == 0 ? 2 : 0);
                uintptr_t alb = (uintptr_t)objects[side * 2 + 1];
                assert(ovp_bar_is_live(d, alb + ALB_BAR_OFF));
                assert(*(uint32_t *)(alb + ALB_COLOUR_OFF) == breath);
            }
        }
        /* Selected new-layer deck has no mark: never inherit old deck breath. */
        ovp_sess[2] = OVP_SESS_CLEARED;
        uintptr_t alb = (uintptr_t)objects[1];
        *(uintptr_t *)(alb + ALB_P50_OFF) = base + 2 * PERDECK_STRIDE + PIS_SP50;
        *(uintptr_t *)(alb + ALB_P58_OFF) = base + 2 * PERDECK_STRIDE + PIS_SP58;
        ovp_upd_seen[1] = T + 40;
        assert(ovp_deck_fast(alb) == 2);
        assert(ovp_deck_place_mode(2, &in, &out) == 2);
        ovp_mutate_alb_slots(breath, OVP_SOLID_RED, 0, T + 41);
        ovp_mutate_alb_slots(breath, OVP_SOLID_RED, 0, T + 42);
        assert(*(uint32_t *)(alb + ALB_COLOUR_OFF) == OVP_SOLID_RED);
        /* Fixtures leave scope: clear every retained pointer. */
        memset(ovp_upd_ptr, 0, sizeof ovp_upd_ptr);
        memset(ovp_rep_bar, 0, sizeof ovp_rep_bar);
        memset(ovp_bound_alb, 0, sizeof ovp_bound_alb);
        memset(ovp_live_bar, 0, sizeof ovp_live_bar);
    }

    printf("ovp_gate_test: ok\n");
    printf("  colour/live/row-off/throttle/dedup ok\n");
    printf("  deferred suppress: window covers async Bar::paint after Component::repaint\n");
    printf("  update-fresh live: newest upd within 0.5s wins; stale not live; paint tie-break only\n");
    printf("  two decks independent; suppress still blocks paint self-feed\n");
    printf("  mutate <=10 Hz + idle early-out + ON-edge defer\n");
    printf("  flagged/real-loop eligibility (not act_ok): vis+flag breathes; no flag solid; other-loop solid\n");
    printf("  flag move/unflag: pick overrides stored flag; unflag hides; SHIFT+DELETE falls back\n");
    printf("  real exit/advance: cue -> flagged memory restores stock X once; unflagged memory follows; no target hides\n");
    printf("  four decks/eight ALBs: actual pulse writes; placement agrees; reused layer widgets restore solid and rebind\n");
    return 0;
}
