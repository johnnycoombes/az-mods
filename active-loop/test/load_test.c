/* 0.2.8 (al028f round): content changes. AZ-EMU al028e: after loading another track 'restore deck 1' and 'reading stored
 * loops' were logged twice and a stale 'Active Loop at 00:04.2 armed' stayed on a track without loops; the first 'track
 * loaded' line said 'content id 0'. Host test with stubbed firmware calls; the three load paths are driven for real
 * (deck_events via deck_evt, the load cues via load_changed, poll_cids via the content id). One test per finding:
 *  (1) loop -> track without loops, on each deck: the armed state is gone at once (act_ok, arm_in, watch_loops), the firmware clear
 *      is queued, restore logs 'other track' ONCE, 'reading stored loops' ONCE, no stale 'armed' line
 *  (2) loop -> track with loops, both decks at once: each deck handled alone; same track again: restored once
 *  (3) the three paths report one load once; a different id, or a call after the window, is a new load
 *  (4) the invariant: a deck that reports another content id with an armed loop drops it (no event needed)
 *  (5) eject then same track: restored once; eject then another track: nothing
 *  (6) a spent (exited) loop is not restored; the row going off/on leaves nothing behind
 *  (7) 'track loaded' waits for the content id: never 'content id 0'
 *  (8) point-cue lines carry the raw Position words
 *  (9) al028g: the id of a load seen from the load cues is never the previous track's (one 'track loaded', one reading, one load) */
#include "../active-loop.c"
#include <assert.h>
static void f_copy(void *d, const void *s) { memcpy(d, s, CUE_SIZE); }
static int dtors;
static void f_dtor(void *c) { (void)c; dtors++; }
static int  f_loop(void *c) { return ((const uint8_t *)c)[0] == 1; }
static int f_pred(void *ls) { (void)ls; return 0; }
static int set_n, clr_n;
static void f_set(void *h, void *c) { (void)h; (void)c; set_n++; }
static void f_clear(void *h) { (void)h; clr_n++; }
static uint8_t HOLD[NDECK][512] __attribute__((aligned(16))), LS[NDECK][64] __attribute__((aligned(16)));
static uint8_t SL[NDECK][9][CUE_SIZE] __attribute__((aligned(16)));
static void *f_slot(void *ls, int kind) { for (int d = 0; d < NDECK; d++) if (ls == (void *)LS[d]) return SL[d][kind]; return NULL; }
static uint32_t cid_of[NDECK];
static int f_deck_info(int deck, az_deck_info *o) { o->loaded = cid_of[deck] != 0; o->content_id = cid_of[deck]; o->pad_mode = -1; o->pos_s = 0; return 0; }
static char logbuf[400][400]; static int nl_;
static void f_log(const char *id, const char *fmt, ...)
{
    (void)id; va_list ap; va_start(ap, fmt);
    if (nl_ < 400) vsnprintf(logbuf[nl_++], 400, fmt, ap);
    va_end(ap);
}
static int lines(const char *needle) { int n = 0; for (int i = 0; i < nl_; i++) if (strstr(logbuf[i], needle)) n++; return n; }
static az_core_v1 FC;
static void mkcue(uint8_t *c, int loop, int64_t in, int64_t out)
{
    memset(c, 0, CUE_SIZE); c[0] = loop ? 1 : 0;
    *(int64_t *)(c + CUE_IN_TIME) = in; *(int64_t *)(c + CUE_OUT_TIME) = loop ? out : INT64_MAX;
}
static void reset(void)
{
    nl_ = 0; set_n = clr_n = 0;
    for (int d = 0; d < NDECK; d++) {
        if (act_ok[d]) act_ok[d] = 0;
        rem_ok[d] = 0; rem_cid[d] = 0; clear_owed[d] = 0; tc_seen[d] = 0; tc_tick[d] = 0; tc_cid[d] = 0; ld_pend[d] = NULL;
        active_cid[d] = 0; spent[d] = 0; restore_pending[d] = 0; pend_op[d] = OP_IDLE; seen_cid[d] = 0; anlz_for[d] = 0;
        mem_n[d] = 0; memset(mem_ok[d], 0, sizeof mem_ok[d]); memset(mem_live[d], 0, sizeof mem_live[d]);
        memset(hot_ok[d], 0, sizeof hot_ok[d]); cid_of[d] = 0; last_load_tick[d] = 0; pos_seq[d] = 0;
        __atomic_store_n(&pos_now[d], POS_UNSET, __ATOMIC_RELAXED);
        __atomic_store_n(&arm_in[d], INT64_MAX, __ATOMIC_RELAXED);
        __atomic_store_n(&anlz_state[d], 0u, __ATOMIC_RELAXED);
        dg_near[d] = dg_stall[d] = 0; ex_inside[d] = ex_past[d] = 0; restore_wait[d] = 0;
    }
    memset(ld_ns, 0, sizeof ld_ns); memset(ld_skip, 0, sizeof ld_skip);
    deck_evt = 0; tick_no = 100000; cid_poll = 0; cfg_bits |= CFG_ENABLE | CFG_REARM | CFG_AUTOADV;
}
/* a memory loop of the deck as the firmware showed it live (CALL), armed as the Active Loop (cid = the loaded track) */
static void arm_mem_loop(int d, uint32_t cid, int64_t in, int64_t out)
{
    uint8_t c[CUE_SIZE] __attribute__((aligned(16))); mkcue(c, 1, in, out);
    cid_of[d] = cid; seen_cid[d] = cid;
    int i = mem_put(d, c); mem_live[d][i] = 1;
    uint32_t keep = pend_op[d];
    pick_and_set(d, mem_buf[d][i], 0, i, "test"); (void)keep;
    pend_op[d] = OP_IDLE;                                  /* the playhead hook applied it */
    publish_arm(d);
    assert(act_ok[d] && active_cid[d] == cid && __atomic_load_n(&arm_in[d], __ATOMIC_RELAXED) == in);
}
/* what the tick does around a load, in my_tick's order */
static void tick(int n)
{
    for (int k = 0; k < n; k++) {
        tick_no++;
        deck_events(); poll_cids(); load_service(); loop_guard(); try_restore(); pend_service(); watch_loops(); anlz_service();
        for (int d = 0; d < NDECK; d++) publish_arm(d);
    }
}
static void event_load(int d) { __atomic_or_fetch(&deck_evt, 1u << d, __ATOMIC_RELAXED); }
int main(void)
{
    fw_cue_copy = f_copy; fw_cue_dtor = f_dtor; fw_is_loop = f_loop; fw_loop_slot = f_slot; fw_set = f_set; fw_clear = f_clear; fw_ls_has_loop = f_pred; fw_ls_looping = f_pred;
    memset(&FC, 0, sizeof FC); FC.size = sizeof FC; FC.log = f_log; FC.deck_info = f_deck_info; C = &FC;
    for (int d = 0; d < NDECK; d++) { engine_of[d] = HOLD[d]; ls_of[d] = LS[d]; }
    al_live = 1;

    /* ---- (1) loop -> track without loops, deck 1 and deck 2, through each of the three paths ---- */
    for (int path = 0; path < 4; path++)
    for (int d = 0; d < 2; d++) {
        reset(); cfg_bits |= CFG_ENABLE;
        arm_mem_loop(d, 45, 185220, 300000);               /* the 00:04.2 loop of Spread Love */
        tick(1); assert(lines("armed, playhead not moving") == 0);
        __atomic_store_n(&pos_now[d], 44100, __ATOMIC_RELAXED); pos_seq[d] = 1;
        tick(2); assert(lines("5 s or less before the Active Loop") == 1);     /* armed on the old track: watched as always */
        nl_ = 0; n_reload = 0;
        /* the new track (id 28): the load cues come first, with no id yet */
        cid_of[d] = 0;
        if (path == 0) { assert(load_changed(d, "load events") == 1); log_load(d, "seen from the load cues"); }
        if (path == 1) { event_load(d); cid_of[d] = 28; }
        if (path == 2) { cid_of[d] = 28; cid_poll = 29; }    /* only the poll sees it: seen_cid still 45 */
        if (path == 3) { cid_of[d] = 28; cid_poll = 0; }     /* nobody reports it: the invariant catches it before the next poll */
        if (path == 0) { cid_of[d] = 28; event_load(d); }    /* ... then the core's event with the id, then the poll */
        unsigned n0 = n_reload;
        tick(1);
        assert(!act_ok[d] && active_cid[d] == 0);            /* nothing armed on the new track */
        assert(__atomic_load_n(&arm_in[d], __ATOMIC_RELAXED) == INT64_MAX);   /* the hook has nothing to re-arm */
        assert(n_reload - n0 <= 1 || path == 0);             /* path 0 had its first call before n0 */
        assert(pend_op[d] == OP_CLEAR || clr_n >= 1);        /* the firmware's copy was cleared (queued or applied) */
        cid_poll = 29; tick(1); tick(120);                   /* the poll and the event of the same load: nothing more */
        if (path == 3) assert(lines("belongs to content id 45, but content id 28 is loaded: dropped") == 1 && lines("restore deck") == 0);
        else assert(lines("restore deck") == 1 && lines("other track (28, was 45)") == 1);
        assert(lines("reading stored loops for content id 28") == 1);
        assert(lines("armed, playhead not moving") == 0 && lines("5 s or less before the Active Loop") == 0);
        assert(lines("track loaded (content id 28;") == 1 && lines("content id 0") == 0);
        assert(!rem_ok[d] && !restore_pending[d] && !clear_owed[d]);
        /* the other deck was never touched */
        assert(!act_ok[d ^ 1]);
    }

    /* ---- (2) loop -> loop track on both decks at once; then the same track again on deck 1 ---- */
    reset();
    arm_mem_loop(0, 45, 185220, 300000); arm_mem_loop(1, 37, 176400, 290000);
    cid_of[0] = 0; cid_of[1] = 0;
    assert(load_changed(0, "load events") == 1 && load_changed(1, "load events") == 1);   /* both decks, hook path, id unknown */
    log_load(0, "seen from the load cues"); log_load(1, "seen from the load cues");
    assert(!act_ok[0] && !act_ok[1] && rem_ok[0] && rem_ok[1] && rem_cid[0] == 45 && rem_cid[1] == 37 && restore_pending[0] && restore_pending[1]);
    tick(5); assert(lines("track loaded") == 0);              /* still no id: the lines wait */
    cid_of[0] = 51; cid_of[1] = 52; event_load(0); event_load(1);
    { unsigned n0 = n_reload; tick(2); assert(n_reload == n0); }       /* the events are the loads already handled */
    assert(lines("track loaded (content id 51;") == 1 && lines("track loaded (content id 52;") == 1);
    tick(70);
    assert(lines("other track (51, was 45)") == 1 && lines("other track (52, was 37)") == 1 && lines("restore deck") == 2);
    assert(!act_ok[0] && !act_ok[1] && !rem_ok[0] && !rem_ok[1]);
    /* the new track on deck 1 has a stored loop of its own: it can be armed and counts alone */
    { uint8_t c[CUE_SIZE] __attribute__((aligned(16))); mkcue(c, 1, 500000, 600000);
      int i = mem_put(0, c); mem_live[0][i] = 1;
      assert(count_known(0, -1) == 1 && count_known(1, -1) == 0); }
    /* same track again on deck 1: loop armed on 51, eject-less reload of 51 after the window -> restored once */
    reset();
    arm_mem_loop(0, 51, 185220, 300000);
    tick_no += 1000;
    cid_of[0] = 51; assert(load_changed(0, "load events") == 1); log_load(0, "seen from the load cues");
    assert(!act_ok[0] && rem_ok[0] && restore_pending[0]);
    { uint8_t c[CUE_SIZE] __attribute__((aligned(16))); mkcue(c, 1, 185220, 300000);   /* seen live again (CALL) */
      int i = mem_put(0, c); mem_live[0][i] = 1; }
    tick(70);
    assert(act_ok[0] && active_cid[0] == 51 && !rem_ok[0] && !restore_pending[0]);
    assert(lines("restore deck 1: same track 51") == 1);
    tick(80); assert(lines("restore deck") == 1);

    /* ---- (3) one load, three paths ---- */
    reset();
    cid_of[0] = 0;
    assert(load_changed(0, "load events") == 1);                        /* hook: id unknown */
    assert(load_changed(0, "deck event") == 0);                          /* still unknown: the same load */
    cid_of[0] = 28; tick_no += 200;                                      /* known 3 s later (inside the 5 s window of an unknown id) */
    assert(load_changed(0, "deck event") == 0 && tc_cid[0] == 28);
    assert(load_changed(0, "content id changed") == 0);
    tick_no += 50; cid_of[0] = 30; assert(load_changed(0, "content id changed") == 1);    /* another track inside the window */
    assert(load_changed(0, "deck event") == 0);
    tick_no += 119; assert(load_changed(0, "load events") == 0);
    tick_no += 2; assert(load_changed(0, "load events") == 1);           /* the same id after the 2 s window: a reload */
    tc_seen[0] = 0; assert(load_changed(0, "load events") == 1);         /* an eject / row switch resets */
    reset(); cid_of[0] = 0; assert(load_changed(0, "load events") == 1);
    tick_no += 301; assert(load_changed(0, "load events") == 1);         /* the id never came: the window ends */
    /* deck independence */
    reset(); cid_of[0] = 5; cid_of[1] = 5; assert(load_changed(0, "e") == 1 && load_changed(1, "e") == 1 && load_changed(0, "e") == 0);

    /* ---- (4) the invariant: a content id that changed without any event ---- */
    reset(); arm_mem_loop(0, 45, 185220, 300000);
    cid_of[0] = 88;
    tick(1);
    assert(!act_ok[0] && !rem_ok[0] && !restore_pending[0] && __atomic_load_n(&arm_in[0], __ATOMIC_RELAXED) == INT64_MAX);
    assert(lines("belongs to content id 45, but content id 88 is loaded") == 1 && (pend_op[0] == OP_CLEAR || clr_n));
    tick(40); assert(lines("belongs to content id") == 1);
    /* the same id: untouched; an unknown id (0, loading): untouched, the load events decide */
    reset(); arm_mem_loop(0, 45, 185220, 300000); tick(3); assert(act_ok[0]); cid_of[0] = 0; tick(3); assert(act_ok[0]);

    /* ---- (5) eject, then the same track / another track ---- */
    reset(); arm_mem_loop(0, 45, 185220, 300000);
    cid_of[0] = 0; __atomic_or_fetch(&deck_evt, 16u, __ATOMIC_RELAXED); tick(1);
    assert(!act_ok[0] && rem_ok[0] && rem_cid[0] == 45 && !restore_pending[0]);
    tick(5); cid_of[0] = 45; event_load(0); tick(1);
    assert(restore_pending[0] && rem_ok[0]);
    { uint8_t c[CUE_SIZE] __attribute__((aligned(16))); mkcue(c, 1, 185220, 300000); int i = mem_put(0, c); mem_live[0][i] = 1; }
    tick(70); assert(act_ok[0] && active_cid[0] == 45 && lines("restore deck 1: same track 45") == 1);
    reset(); arm_mem_loop(0, 45, 185220, 300000);
    cid_of[0] = 0; __atomic_or_fetch(&deck_evt, 16u, __ATOMIC_RELAXED); tick(1);
    tick(5); cid_of[0] = 99; event_load(0); tick(70);
    assert(!act_ok[0] && !rem_ok[0] && lines("other track (99, was 45)") == 1);

    /* ---- (6) a spent loop is not restored; row off / on leaves nothing ---- */
    reset(); arm_mem_loop(0, 45, 185220, 300000); spent[0] = 1;
    cid_of[0] = 45; tick_no += 500; assert(load_changed(0, "load events") == 1);
    assert(!rem_ok[0] && !restore_pending[0] && !clear_owed[0]);
    reset(); arm_mem_loop(0, 45, 185220, 300000);
    row_went_off(); assert(!act_ok[0] && !rem_ok[0] && !restore_pending[0]);
    cid_of[0] = 45; { int n = nl_; row_went_on(); (void)n; assert(!act_ok[0] && !rem_ok[0] && !restore_pending[0]); }

    /* ---- (7) 'track loaded' waits for the content id ---- */
    reset(); nl_ = 0;
    cid_of[0] = 0; log_load(0, "seen from the load cues"); tick(3); assert(lines("track loaded") == 0 && ld_pend[0]);
    cid_of[0] = 45; tick(1); assert(lines("track loaded (content id 45; seen from the load cues)") == 1 && !ld_pend[0]);
    tick(5); assert(lines("track loaded") == 1);
    { reset(); cid_of[1] = 0; log_load(1, "core deck event"); tick(150); assert(lines("track loaded") == 0); tick(40);
      assert(lines("track loaded (content id not known yet; core deck event)") == 1 && lines("content id 0") == 0); }
    /* a second load that arrives while the first still waits keeps the first path's name and logs once */
    reset(); cid_of[0] = 0; log_load(0, "seen from the load cues"); log_load(0, "core deck event"); cid_of[0] = 7; tick(1);
    assert(lines("track loaded (content id 7; seen from the load cues)") == 1 && lines("track loaded") == 1);

    /* ---- (8) point cue lines carry the raw Position words, like the EMPTY lines ---- */
    reset();
    { uint8_t *c = SL[0][3]; mkcue(c, 0, 1234567, 0); c[0x30] = 0x11; c[0x34] = 0x22; c[0x36] = 0x33; *(uint32_t *)(c + 0x38) = 0x44; *(int64_t *)(c + 0x40) = 1234567;
      apply_pad(0, 0, 3);
      assert(lines("hot cue 3 is a point cue (position valid: key 11223344, sample 1234567, in=1234567)") == 1);
      memset(SL[0][4], 0, CUE_SIZE); *(int64_t *)(SL[0][4] + 0x40) = INT64_MAX; apply_pad(0, 0, 4);
      assert(lines("hot cue 4 is EMPTY (position invalid: key 00000000, sample 9223372036854775807)") == 1); }

    /* ---- (9) al028g: the load cues run before the deck reports the new track (deck_cid still says the OLD id) ----
     * AZ-EMU al028f: 'track loaded' printed the previous id in 7 of 8 loads from the load cues, and one load read the stored loops
     * twice (old id, then new id). Driven through load_cue_seen (what handle_ev runs for a load cue). The new id appears at
     * tick `arrive` after the last load cue; the old id is on the deck until then. */
    {
        static const int arrive_at[] = { 5, 6, 20, 40, 58, 59 };   /* ticks after the last load cue (the burst loop below ends 5 ticks after it); the deck reports within 1 s, as try_restore relies on */
        for (int path = 0; path < 2; path++)
        for (int d = 0; d < 2; d++)
        for (unsigned ai = 0; ai < sizeof arrive_at / sizeof *arrive_at; ai++) {
            int arrive = arrive_at[ai];
            reset();
            arm_mem_loop(d, 37, 185220, 300000);               /* track 37, an Active Loop armed on it */
            tick(40); nl_ = 0; n_reload = 0;
            /* a burst of load cues over ~0.3 s (every cue goes through load_cue_seen), the deck still reports 37 */
            for (int k = 0; k < 4; k++) { load_cue_seen(d); tick(5); }
            assert(cid_of[d] == 37 && lines("track loaded") == 0);          /* never printed with the old id */
            assert(!act_ok[d] && rem_ok[d] && n_reload == 1);
            if (path == 1) { tick(arrive - 5); cid_of[d] = 45; }
            else           { int t = 0; for (; t < arrive - 5; t++) tick(1); cid_of[d] = 45; event_load(d); }
            tick(200);
            assert(lines("track loaded (content id 37") == 0);
            assert(lines("track loaded (content id 45;") == 1 && lines("track loaded") == 1 && lines("content id 0") == 0);
            assert(lines("reading stored loops for content id 37") == 0);
            assert(lines("reading stored loops for content id 45") == 1 && lines("reading stored loops") == 1);
            assert(lines("other track (45, was 37)") == 1 && lines("restore deck") == 1);
            assert(n_reload == 1);                                           /* ONE load: no second track_changed from the id change */
            assert(anlz_for[d] == 45 && tc_cid[d] == 45);
            assert(!act_ok[d ^ 1]);
        }
    }
    /* the stale id never starts a reading, even when the tick that polls the stick lands inside the burst */
    reset(); cid_of[0] = 37; seen_cid[0] = 37; anlz_for[0] = 37; tick_no = 100000; nl_ = 0;
    load_cue_seen(0); anlz_for[0] = 0;                                  /* track_changed cleared it */
    for (int k = 0; k < 59; k++) { tick(1); load_cue_seen(0); }         /* cues keep coming: the burst goes on */
    tick(15); assert(lines("reading stored loops") == 0);
    cid_of[0] = 45; tick(120);
    assert(lines("reading stored loops for content id 45") == 1 && lines("reading stored loops") == 1 && lines("track loaded (content id 45;") == 1);
    /* the same track loaded again: its (unchanged) id is logged once after the burst */
    reset(); cid_of[0] = 45; seen_cid[0] = 45; tick_no += 1000; nl_ = 0;
    load_cue_seen(0); tick(3); assert(lines("track loaded") == 0);
    tick(70); assert(lines("track loaded (content id 45; seen from the load cues)") == 1 && lines("track loaded") == 1);
    /* first load of a deck (no earlier id at all) */
    reset(); cid_of[1] = 0; nl_ = 0; load_cue_seen(1); tick(10); cid_of[1] = 66; tick(120);
    assert(lines("track loaded (content id 66;") == 1 && lines("reading stored loops for content id 66") == 1 && lines("content id 0") == 0);
    /* load_id itself */
    reset(); cid_of[0] = 9; assert(load_id(0) == 9);                    /* no load cue seen: the deck's id */
    last_load_tick[0] = tick_no; assert(load_id(0) == 0); tick_no += 59; assert(load_id(0) == 0); tick_no += 1; assert(load_id(0) == 9);

    printf("load_test: ok\n");
    return 0;
}
