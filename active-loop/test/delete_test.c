/* 0.2.8 (al028b round): the deleteCue OBSERVER and the tick side that keeps the loop list honest
 * (host, stubbed firmware calls). One test per finding:
 *   (7) hooks do no firmware call for a delete, no clock_gettime; row off = no event at all
 *   (6) row off is silent and inert: no log, no toast, no exit/auto-advance handling
 *   (deck) deck attribution from the live slots / press, never from track identity (same track on 2 decks)
 *   (4b) a memory delete never clears an Active Loop armed from a hot cue with the same extent
 *   (5) one loop stored as memory + hot cue counts once; counts and cycling agree
 *   al028: the first delete of a loop pad arms nothing; stale list; memory loop 0.2 s off; SHIFT+pad arm/pass */
#include "../active-loop.c"
#include <assert.h>
static int orig_n, orig_kind = -1, orig_call_n, orig_load_n, orig_store_n, orig_setout_n;
static int copy_n, nlog;
static void fake_orig(void *m, int k, const void *c) { (void)m; (void)c; orig_n++; orig_kind = k; }
static void fake_call(void *f, uint32_t k, void *c) { (void)f; (void)k; (void)c; orig_call_n++; }
static void fake_load(void *f, uint32_t k, void *c) { (void)f; (void)k; (void)c; orig_load_n++; }
static void fake_store(void *f, int k, void *c) { (void)f; (void)k; (void)c; orig_store_n++; }
static void fake_setout(void *f, int k, int s) { (void)f; (void)k; (void)s; orig_setout_n++; }
static void st_copy(void *d, const void *s) { memcpy(d, s, CUE_SIZE); copy_n++; }
static void st_dtor(void *c) { (void)c; }
static int  st_loop(void *c) { return ((const uint8_t *)c)[0] == 1; }   /* byte 0 = "is a loop" */
static uint8_t HOLD0[512] __attribute__((aligned(16))), HOLD1[512] __attribute__((aligned(16)));
static uint8_t LS0[64] __attribute__((aligned(16))), LS1[64] __attribute__((aligned(16)));   /* fake LoopStates */
static uint8_t SLOT0[9][CUE_SIZE] __attribute__((aligned(16))), SLOT1[9][CUE_SIZE] __attribute__((aligned(16)));
static void *st_slot(void *ls, int kind) { return ls == (void *)LS1 ? SLOT1[kind] : SLOT0[kind]; }
#define SLOT(d, k) ((d) ? SLOT1[k] : SLOT0[k])
static void mkcue(uint8_t *c, int loop, int64_t in, int64_t out)
{
    memset(c, 0, CUE_SIZE); c[0] = loop ? 1 : 0;
    *(int64_t *)(c + CUE_IN_TIME) = in; *(int64_t *)(c + CUE_OUT_TIME) = loop ? out : INT64_MAX;
}
/* a valid firmware Position at +0x28 (what pos_valid() tests): a SET hot cue; slots that are only zeroed are EMPTY */
static void mkpos(uint8_t *c) { c[0x30] = 1; c[0x34] = 1; c[0x36] = 1; *(uint32_t *)(c + 0x38) = 1; }
static int set_n, clr_n;
static void st_set(void *h, void *c) { (void)h; (void)c; set_n++; }
static void st_clear(void *h) { (void)h; clr_n++; }
static int row_state = 1;
static int fake_row_value(int h) { (void)h; return row_state; }
static void fake_log(const char *id, const char *fmt, ...) { (void)id; (void)fmt; nlog++; }
static az_core_v1 FC;
static int fake_padmode = -1;
static uint32_t fake_cid[NDECK];                      /* al028f: content id per deck, 0 = nothing loaded */
static int fake_deck_info(int deck, az_deck_info *o) { o->pad_mode = fake_padmode; o->loaded = fake_cid[deck] != 0; o->content_id = fake_cid[deck]; return 0; }
static void reset_state(void)
{
    orig_n = 0; orig_kind = -1; copy_n = 0;
    for (int d = 0; d < NDECK; d++) {
        act_ok[d] = 0; spent[d] = 0; pend_op[d] = OP_IDLE; act_src_hot[d] = 0; act_src_idx[d] = 0; active_cid[d] = 0;
        for (int k = 0; k < NHOT; k++) { hot_ok[d][k] = 0; }
        mem_n[d] = 0; memset(mem_ok[d], 0, sizeof mem_ok[d]); memset(mem_live[d], 0, sizeof mem_live[d]);
        ex_inside[d] = 0; ex_past[d] = 0;
    }
    memset(press_obs_ns, 0, sizeof press_obs_ns);
    for (int k = 0; k < 9; k++) { mkcue(SLOT0[k], 0, 0, 0); mkcue(SLOT1[k], 0, 0, 0); }
    toast_pending = 0; toast_text = ""; FS.req = 0; hot_dirty_until = 0;
    cfg_bits |= CFG_ENABLE; row_state = 1;
}
/* what my_tick does after the event was queued */
static void tick_drain(int deck_pressed)
{
    tick_no += 10;
    if (deck_pressed >= 0) { press_obs_ns[0] = now_ns(); press_obs_deck[0] = deck_pressed; }
    drain_ring();
    hot_sweep();
}
static void put_hot(int d, int k, int64_t in, int64_t out)
{
    uint8_t c[CUE_SIZE] __attribute__((aligned(16))); mkcue(c, 1, in, out); hot_put(d, k, c);
    mkcue(SLOT(d, k), 1, in, out);                                  /* and the live slot holds it */
}
static void put_mem(int d, int64_t in, int64_t out)
{
    uint8_t c[CUE_SIZE] __attribute__((aligned(16))); mkcue(c, 1, in, out);
    int i = mem_put(d, c); mem_live[d][i] = 1;
}
static void arm_hot(int d, int k, int64_t in, int64_t out)
{
    act_ok[d] = 1; active_in[d] = in; active_out[d] = out; act_src_hot[d] = 1; act_src_idx[d] = k;
    mkcue(act_buf[d], 1, in, out);
}
static void arm_mem(int d, int i)
{
    act_ok[d] = 1; active_in[d] = cue_in(mem_buf[d][i]); active_out[d] = cue_out(mem_buf[d][i]);
    act_src_hot[d] = 0; act_src_idx[d] = i; memcpy(act_buf[d], mem_buf[d][i], CUE_SIZE);
}
int main(void)
{
    fw_cue_copy = st_copy; fw_cue_dtor = st_dtor; fw_is_loop = st_loop; fw_loop_slot = st_slot; fw_set = st_set; fw_clear = st_clear;
    orig_delete = fake_orig; orig_call = fake_call; orig_load = fake_load; orig_store = fake_store; orig_setout = fake_setout;
    cfg_bits = CFG_ENABLE | CFG_HOTCUE | CFG_CLEAR | CFG_TOAST | CFG_REARM | CFG_AUTOADV;
    engine_of[0] = HOLD0; ls_of[0] = LS0; engine_of[1] = HOLD1; ls_of[1] = LS1;
    uint8_t pt[CUE_SIZE] __attribute__((aligned(16))), lp[CUE_SIZE] __attribute__((aligned(16))),
            lp2[CUE_SIZE] __attribute__((aligned(16)));
    mkcue(pt, 0, 1000000, 0); mkcue(lp, 1, 2000000, 2500000); mkcue(lp2, 3000000 > 0, 3000000, 3500000);
    al_live = 1;

    /* ---- (7) hooks: the delete observer calls nothing of the firmware; row off = no event ---- */
    reset_state(); ring_r = ring_w;
    my_delete(NULL, 2, lp);
    assert(orig_n == 1 && orig_kind == 2 && copy_n == 0);           /* no fw_cue_copy for a delete */
    drain_ring(); assert(copy_n == 0);
    my_call(HOLD0, 2, lp);  assert(orig_call_n == 1 && copy_n == 1); drain_ring();   /* the others still copy */
    reset_state(); al_live = 0; ring_r = ring_w; my_delete(NULL, 2, lp); assert(orig_n == 1 && ring_r == ring_w); al_live = 1;
    /* row off: every hook is a bare pass-through, no event, no copy */
    reset_state(); cfg_bits &= ~CFG_ENABLE; ring_r = ring_w;
    { uint32_t w = ring_w;
      my_delete(NULL, 2, lp); my_call(HOLD0, 2, lp); my_load(HOLD0, 2, lp); my_store(NULL, 2, lp); my_setout(HOLD0, 2, 0);
      assert(orig_n == 1 && orig_call_n == 2 && orig_load_n == 1 && orig_store_n == 1 && orig_setout_n == 1);
      assert(ring_w == w && copy_n == 0); }
    cfg_bits |= CFG_ENABLE;
    /* a bad pointer never faults */
    reset_state(); my_delete(NULL, 2, (const void *)0x10); tick_drain(0); assert(orig_n == 1 && !act_ok[0]);

    /* ---- al028: the first delete of a loop pad arms nothing and drops the stale list entry ---- */
    reset_state();
    put_hot(0, 2, 2000000, 2500000);
    mkcue(SLOT0[2], 0, 0, 0);                                        /* the firmware cleared the live slot */
    my_delete(NULL, 2, lp); tick_drain(0);
    assert(orig_n == 1 && !hot_ok[0][2] && !act_ok[0] && pend_op[0] == OP_IDLE && !toast_pending);

    /* the slot is cleared a tick LATER (posted task): the sweep window still catches it */
    reset_state();
    put_hot(0, 2, 2000000, 2500000);
    my_delete(NULL, 2, lp); tick_drain(0);
    assert(hot_ok[0][2]);                                            /* still a loop in the live slot: unchanged */
    mkcue(SLOT0[2], 0, 0, 0); tick_no += 1; hot_sweep();
    assert(!hot_ok[0][2]);

    /* ---- Active Loop armed from that hot cue: cleared, with the toast ---- */
    reset_state();
    put_hot(0, 2, 2000000, 2500000); arm_hot(0, 2, 2000000, 2500000);
    mkcue(SLOT0[2], 0, 0, 0);
    my_delete(NULL, 2, lp); tick_drain(0);
    assert(!hot_ok[0][2] && !act_ok[0] && pend_op[0] == OP_CLEAR);
    assert(toast_pending && !strcmp(toast_text, "Active Loop cleared (loop deleted)"));
    /* another pad deleted: the armed loop and the other pad stay */
    reset_state();
    put_hot(0, 2, 2000000, 2500000); put_hot(0, 4, 3000000, 3500000); arm_hot(0, 2, 2000000, 2500000);
    mkcue(SLOT0[4], 0, 0, 0); my_delete(NULL, 4, lp2); tick_drain(0);
    assert(act_ok[0] && hot_ok[0][2] && !hot_ok[0][4] && pend_op[0] == OP_IDLE);

    /* ---- (deck) the same track on both decks: only the deck whose live slot changed is touched ---- */
    reset_state();
    put_hot(0, 3, 4000000, 4400000); put_hot(1, 3, 4000000, 4400000);           /* identical cues */
    arm_hot(0, 3, 4000000, 4400000); arm_hot(1, 3, 4000000, 4400000);
    mkcue(SLOT1[3], 0, 0, 0);                                                    /* deleted on deck 2 only */
    my_delete(NULL, 3, lp); tick_drain(-1);                                      /* no press: filter may be off */
    assert(hot_ok[0][3] && act_ok[0] && pend_op[0] == OP_IDLE);                  /* deck 1 untouched */
    assert(!hot_ok[1][3] && !act_ok[1] && pend_op[1] == OP_CLEAR);               /* deck 2 handled */
    reset_state();
    put_hot(0, 3, 4000000, 4400000); put_hot(1, 3, 4000000, 4400000); arm_hot(0, 3, 4000000, 4400000); arm_hot(1, 3, 4000000, 4400000);
    mkcue(SLOT0[3], 0, 0, 0); my_delete(NULL, 3, lp); tick_drain(1);            /* a press on the OTHER side must not matter */
    assert(!hot_ok[0][3] && !act_ok[0] && hot_ok[1][3] && act_ok[1] && pend_op[1] == OP_IDLE);
    /* different tracks: same result */
    reset_state();
    put_hot(0, 3, 4000000, 4400000); put_hot(1, 3, 9000000, 9400000);
    mkcue(SLOT1[3], 0, 0, 0); my_delete(NULL, 3, lp); tick_drain(0);
    assert(hot_ok[0][3] && !hot_ok[1][3]);

    /* ---- memory delete: deck by press, else the one deck that has it; ambiguous = unknown = nothing ---- */
    reset_state();
    put_mem(0, 17365172, 17365172 + 88200); put_mem(1, 17365172, 17365172 + 88200);   /* same track, both decks */
    arm_mem(0, 0); arm_mem(1, 0);
    { uint8_t fw[CUE_SIZE] __attribute__((aligned(16)));
      mkcue(fw, 1, 17365172 - 8820, 17365172 + 88200 - 8820);                   /* the firmware's is 0.2 s earlier (al028) */
      my_delete(NULL, 0, fw); tick_drain(1);                                    /* DELETE on deck 2 */
      assert(mem_live[0][0] && act_ok[0]);                                      /* deck 1 untouched */
      assert(!mem_live[1][0] && !act_ok[1] && pend_op[1] == OP_CLEAR);
      assert(toast_pending && !strcmp(toast_text, "Active Loop cleared (loop deleted)"));
      /* no press and both decks have it: ambiguous, nothing at all */
      reset_state();
      put_mem(0, 17365172, 17365172 + 88200); put_mem(1, 17365172, 17365172 + 88200); arm_mem(0, 0); arm_mem(1, 0);
      my_delete(NULL, 0, fw); tick_drain(-1);
      assert(mem_live[0][0] && mem_live[1][0] && act_ok[0] && act_ok[1] && !toast_pending);
      /* no press, only one deck has it: that deck */
      reset_state(); put_mem(0, 17365172, 17365172 + 88200); put_mem(1, 50000000, 50088200); arm_mem(0, 0);
      my_delete(NULL, 0, fw); tick_drain(-1);
      assert(!mem_live[0][0] && mem_live[1][0] && !act_ok[0]);
      /* a far-away memory delete touches nothing */
      reset_state(); put_mem(0, 17365172, 17365172 + 88200); arm_mem(0, 0);
      mkcue(fw, 1, 90000000, 90088200); my_delete(NULL, 0, fw); tick_drain(0);
      assert(mem_live[0][0] && act_ok[0]);
      /* a memory point cue */
      my_delete(NULL, 0, pt); tick_drain(0); assert(act_ok[0] && mem_live[0][0]);
    }

    /* ---- (4b) a memory delete with the extent of the armed HOT cue does not clear it ---- */
    reset_state();
    put_hot(0, 4, 296838, 465256); put_mem(0, 296838, 465256);                  /* hot cue D == memory loop #0 */
    arm_hot(0, 4, 296838, 465256);
    { uint8_t fw[CUE_SIZE] __attribute__((aligned(16))); mkcue(fw, 1, 296838, 465256);
      my_delete(NULL, 0, fw); tick_drain(0);
      assert(!mem_live[0][0] && hot_ok[0][4] && act_ok[0] && pend_op[0] == OP_IDLE && !toast_pending);
      /* and the other way: armed from the memory loop, the HOT cue D deleted: stays armed */
      reset_state();
      put_hot(0, 4, 296838, 465256); put_mem(0, 296838, 465256); arm_mem(0, 0);
      mkcue(SLOT0[4], 0, 0, 0); my_delete(NULL, 4, fw); tick_drain(0);
      assert(!hot_ok[0][4] && act_ok[0] && pend_op[0] == OP_IDLE && mem_live[0][0]);
    }

    /* ---- (5) counting: the same loop as memory + hot cue is ONE loop ---- */
    reset_state();
    put_mem(0, 1159963, 1328381);                                              /* memory 26.3 s */
    put_hot(0, 4, 1159963, 1328381);                                           /* hot cue D, same extent */
    put_hot(0, 3, 865242, 949429);                                             /* hot cue C 19.6 s */
    tick_no += 10;
    assert(count_known(0, 100) == 2);                                          /* 2 distinct stops, not 3 */
    { int hot, idx; const uint8_t *c = next_ahead(0, 100, &hot, &idx); assert(c && cue_in(c) == 865242);
      c = next_ahead(0, 865242, &hot, &idx); assert(c && cue_in(c) == 1159963 && !hot);        /* memory wins */
      assert(!next_ahead(0, 1159963, &hot, &idx)); }
    /* C deleted: now 1, and cycling agrees ("only loop ahead") */
    mkcue(SLOT0[3], 0, 0, 0); my_delete(NULL, 3, lp); tick_drain(0);
    assert(!hot_ok[0][3] && count_known(0, 100) == 1);
    { int hot, idx; const uint8_t *c = next_ahead(0, 100, &hot, &idx); assert(c && cue_in(c) == 1159963); }
    /* three distinct loops: 3, then 2 after a delete (the user's expectation) */
    reset_state();
    put_mem(0, 1159963, 1328381); put_hot(0, 3, 865242, 949429); put_hot(0, 5, 2000000, 2100000);
    tick_no += 10; assert(count_known(0, 100) == 3);
    mkcue(SLOT0[3], 0, 0, 0); my_delete(NULL, 3, lp); tick_drain(0);
    assert(count_known(0, 100) == 2);
    /* a hot cue loop deleted that was a duplicate of a memory loop: the count stays */
    reset_state(); put_mem(0, 1159963, 1328381); put_hot(0, 4, 1159963, 1328381); tick_no += 10;
    assert(count_known(0, 100) == 1);
    mkcue(SLOT0[4], 0, 0, 0); my_delete(NULL, 4, lp); tick_drain(0); assert(count_known(0, 100) == 1);
    /* a loop only in the live slot (row was off, no event) is learned */
    reset_state(); mkcue(SLOT0[6], 1, 777777, 888888); tick_no += 10;
    assert(count_known(0, 100) == 1 && hot_ok[0][6]);
    /* a stale entry without any delete event: the live slot says it is gone */
    reset_state(); put_hot(0, 5, 4000000, 4500000); put_hot(0, 6, 5000000, 5500000); mkcue(SLOT0[5], 0, 0, 0); tick_no += 10;
    assert(count_known(0, -1) == 1 && !hot_ok[0][5] && hot_ok[0][6]);
    /* moved loop: live values replace the cached ones */
    mkcue(SLOT0[6], 1, 5000500, 5500500); tick_no += 10;
    { int hot, idx; const uint8_t *c = next_ahead(0, -1, &hot, &idx); assert(c && idx == 6 && cue_in(c) == 5000500); }

    /* a store/load/call of a point cue into a pad that holds a cached loop drops it (event path) */
    reset_state(); put_hot(0, 3, 6000000, 6500000);
    { ev_t ev; memset(&ev, 0, sizeof ev);
      fac_t *f = fac_get(HOLD0); assert(f); f->deck = 0;
      ev.facade = HOLD0; ev.kind = 3; ev.src = SRC_LOAD; ev.has_cue = 1; ev.t_ns = cnt_now(); memcpy(ev.cue, pt, CUE_SIZE);
      handle_ev(&ev); }
    assert(!hot_ok[0][3]);

    /* ---- (6) row off: silent and inert (the al028b leak: "exited", "auto-advance off", "Active Loop done") ---- */
    FC.size = sizeof FC; FC.log = fake_log; FC.row_value = fake_row_value;
    C = &FC; h_enable = 1; h_autoadv = 2; h_rearm = 3;
    reset_state();
    put_mem(0, 296838, 465256); arm_mem(0, 0); act_buf[0][0] = 1;
    pos_now[0] = 465256 + 44100 * 5; pos_seq[0] = 1;
    ex_inside[0] = 1; ex_past[0] = 2;                                          /* about to be reported as "exited" */
    FS.cfg = 0;
    row_state = 0; tk_row_was = 1;
    nlog = 0; toast_pending = 0; clr_n = 0; set_n = 0;
    my_tick();                                                                  /* the transition: one line, the loop cleared */
    assert(!act_ok[0] && clr_n == 1 && !toast_pending);
    int after_transition = nlog;
    pend_op[0] = OP_IDLE;
    for (int i = 0; i < 40; i++) {                                              /* row stays off: nothing at all */
        mem_n[0] = 1; mem_ok[0][0] = 1;
        ex_inside[0] = 1; ex_past[0] = 2; arm_mem(0, 0);   /* an armed loop the playhead has left */
        my_tick();
    }
    assert(nlog == after_transition && !toast_pending && pend_op[0] == OP_IDLE && clr_n == 1 && set_n == 0 && act_ok[0]);
    assert(!(FS.cfg & CFG_ENABLE));
    /* the same state with the row ON does react (proves the test would have caught the leak) */
    row_state = 1; reset_state(); row_state = 1;
    put_mem(0, 296838, 465256); arm_mem(0, 0);
    pos_now[0] = 465256 + 44100 * 5; pos_seq[0] = 1; ex_inside[0] = 1; ex_past[0] = 2;
    tk_row_was = 1; nlog = 0;
    my_tick();
    assert(nlog > 0 && toast_pending);                                          /* "exited ... Active Loop done" */
    /* off -> on re-learns, arms nothing */
    reset_state(); row_state = 0; tk_row_was = 1; my_tick(); row_state = 1;
    mkcue(SLOT0[6], 1, 777777, 888888);
    my_tick(); tick_no += 10; assert(!act_ok[0] && count_known(0, 100) == 1);
    C = NULL;

    /* ---- SHIFT+pad (tick side) ---- */
    reset_state();
    mkcue(SLOT0[2], 1, 2000000, 2500000);
    apply_pad(0, 0, 2);
    assert(act_ok[0] && act_src_hot[0] && act_src_idx[0] == 2 && active_in[0] == 2000000 && pend_op[0] == OP_SET && hot_ok[0][2]);
    assert(FS.req == 0 && toast_pending && !strncmp(toast_text, "Active Loop armed at", 20));   /* 0.2.9: no content id in this stub, so armed but not saved */
    reset_state();
    mkcue(SLOT0[3], 0, 1000000, 0); mkpos(SLOT0[3]);
    apply_pad(1, 0, 3);
    assert(!act_ok[0] && FS.req == (RQ_VALID | (1u << 9) | (1u << 2)));
    for (int k = 1; k <= 8; k++) {
        reset_state(); apply_pad(0, 0, k); assert(FS.req == 0 && !toast_pending);               /* EMPTY pad: SHIFT+pad is swallowed, nothing replayed */
        mkcue(SLOT0[k], 0, 1234567, 0); mkpos(SLOT0[k]);                                          /* a point cue: replayed to stock (it is deleted) */
        apply_pad(0, 0, k); assert(FS.req == (RQ_VALID | (1u << (k - 1))));
    }
    /* al028d 2b: an EMPTY pad on the right deck, a loop that has just been deleted (slot reset in place) */
    reset_state(); mkcue(SLOT1[5], 0, 0, 0); apply_pad(1, 1, 5); assert(FS.req == 0 && !act_ok[1] && !toast_pending);
    reset_state(); put_hot(0, 6, 3000000, 3500000); mkcue(SLOT0[6], 0, 0, 0); apply_pad(0, 0, 6);
    assert(FS.req == 0 && !hot_ok[0][6] && !act_ok[0]);                                          /* the stale loop is dropped, nothing replayed */
    reset_state(); put_hot(0, 4, 3000000, 3500000); mkcue(SLOT0[4], 0, 3000000, 0); mkpos(SLOT0[4]);
    apply_pad(0, 0, 4); assert(!hot_ok[0][4] && !act_ok[0] && FS.req == (RQ_VALID | (1u << 3)));
    reset_state(); ls_of[1] = NULL; apply_pad(1, 1, 2);
    assert(FS.req == 0 && !act_ok[1] && toast_pending && !strcmp(toast_text, "Active Loop: deck not ready"));
    ls_of[1] = LS1;
    reset_state(); apply_pad(0, 0, 0); apply_pad(0, 0, 9); assert(FS.req == 0 && !toast_pending);

    /* ---- al028d: the tick publishes which pads are empty while DELETE is held on a side ---- */
    reset_state(); FS.emp = 0; memset(FS.dh, 0, sizeof FS.dh);
    mkcue(SLOT0[2], 0, 100, 0); mkpos(SLOT0[2]); mkcue(SLOT0[3], 1, 2000000, 2500000); mkpos(SLOT0[3]);
    mkcue(SLOT1[8], 0, 700, 0); mkpos(SLOT1[8]);
    emp_refresh(); assert(FS.emp == 0);                                                          /* DELETE up on both sides: unknown */
    FS.dh[0] = 1; emp_refresh();
    assert(FS.emp == (0xffu & ~(1u << 1) & ~(1u << 2)));                                         /* side A: pads 1,4..8 empty; 2 (point) and 3 (loop) are set */
    FS.dh[1] = 1; emp_refresh();
    assert(FS.emp == ((0xffu & ~(1u << 1) & ~(1u << 2)) | ((0xffu & ~(1u << 7)) << 8)));
    FS.dh[0] = 0; emp_refresh();
    assert(FS.emp == ((0xffu & ~(1u << 7)) << 8));
    FS.dh[1] = 0; emp_refresh(); assert(FS.emp == 0);
    ls_of[0] = NULL; FS.dh[0] = 1; emp_refresh(); assert(FS.emp == 0); ls_of[0] = LS0;           /* unreadable deck: unknown, not "empty" */
    FS.dh[0] = 0; emp_refresh();

    /* BEAT LOOP / SLIP LOOP known: not hot cue pads, so an empty slot is not swallowed (the replay is what stock needs there) */
    { C = &FC; FC.deck_info = fake_deck_info; fake_padmode = 1;
      reset_state(); apply_pad(0, 0, 3); assert(FS.req == (RQ_VALID | (1u << 2)));
      reset_state(); FS.emp = 0; FS.dh[0] = 1; emp_refresh(); assert(FS.emp == 0);
      fake_padmode = 2; reset_state(); apply_pad(0, 0, 3); assert(FS.req == (RQ_VALID | (1u << 2)));
      fake_padmode = 3; reset_state(); apply_pad(0, 0, 3); assert(FS.req == 0);                 /* beat jump: reported as unknown by the core, never 3 in practice */
      fake_padmode = -1; reset_state(); apply_pad(0, 0, 3); assert(FS.req == 0);
      FS.dh[0] = 1; emp_refresh(); assert(FS.emp == 0xffu); FS.dh[0] = 0; emp_refresh(); assert(FS.emp == 0);
      FC.deck_info = NULL; C = NULL; }

    /* ---- al028d/f: a "track loaded" line from every path, one per deck per 2 s, with the real content id ---- */
    { C = &FC; FC.deck_info = fake_deck_info; nlog = 0;
      fake_cid[2] = 45; fake_cid[3] = 46;
      log_load(2, "core deck event"); assert(nlog == 1);
      log_load(2, "seen from the load cues"); log_load(2, "content id changed"); assert(nlog == 1);   /* same deck within 2 s: counted, not logged */
      log_load(3, "seen from the load cues"); assert(nlog == 2);                                          /* another deck: its own gate */
      __atomic_store_n(&deck_evt, 1u << 1, __ATOMIC_RELAXED); nlog = 0; fake_cid[1] = 9; tc_seen[1] = 0; deck_events(); assert(nlog == 1);   /* deck event path logs */
      __atomic_store_n(&deck_evt, 16u << 1, __ATOMIC_RELAXED); deck_events(); assert(nlog == 1);           /* an eject is not logged */
      memset(fake_cid, 0, sizeof fake_cid); FC.deck_info = NULL; C = NULL; }
    /* al028d: frame interval -> replay length (40 ms, 6..200 frames) */
    assert(rate_frames(1000000000ull / 17, 1000000000ull) == 6);           /* emulator, 17 frames/s: 40 ms is under 1 frame: the 6-frame floor */
    assert(rate_frames(1000000000ull / 125, 1000000000ull) == 6);          /* 125/s: 5 frames -> floor 6 */
    assert(rate_frames(1000000000ull / 250, 1000000000ull) == 10);
    assert(rate_frames(1000000000ull / 1000, 1000000000ull) == 40);        /* deck, ~1000/s */
    assert(rate_frames(1000000000ull / 10000, 1000000000ull) == 200);
    assert(rate_frames(0, 1000000000ull) == 12);

    printf("delete_test: ok\n");
    return 0;
}
