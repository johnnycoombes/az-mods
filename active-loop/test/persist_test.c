/* 0.2.10 (0.2.9 + the data-loss fixes): Active Loop persistence + red pad (host, stubbed firmware calls).
 *   store with flag / same-extent twin = arm only / foreign flag = arm only / own copy replaced / ownership only after the read-back /
 *   the mod never deletes a loop it did not store and confirm / toggle off / DELETE+pad / cap incl. memory points / no store pointer /
 *   job spacing / LED cell edit (order, brightness, mode gate, other bytes untouched) / store pointer (vtable, queue, scan) / read-back. */
#include "../active-loop.c"
#include <assert.h>
static int copy_n;
static void st_copy(void *d, const void *s) { memcpy(d, s, CUE_SIZE); copy_n++; }
static void st_dtor(void *c) { (void)c; }
static int  st_loop(void *c) { return ((const uint8_t *)c)[0] == 1; }
static uint8_t HOLD0[512] __attribute__((aligned(16))), HOLD1[512] __attribute__((aligned(16)));
static uint8_t LS0[64] __attribute__((aligned(16))), LS1[64] __attribute__((aligned(16)));
static uint8_t SLOT0[9][CUE_SIZE] __attribute__((aligned(16))), SLOT1[9][CUE_SIZE] __attribute__((aligned(16)));
static void *st_slot(void *ls, int kind) { return ls == (void *)LS1 ? SLOT1[kind] : SLOT0[kind]; }
#define SLOT(d, k) ((d) ? SLOT1[k] : SLOT0[k])
static void mkcue(uint8_t *c, int loop, int64_t in, int64_t out)
{
    memset(c, 0, CUE_SIZE); c[0] = loop ? 1 : 0;
    *(int64_t *)(c + CUE_IN_TIME) = in; *(int64_t *)(c + CUE_OUT_TIME) = loop ? out : INT64_MAX;
}
static void st_set(void *h, void *c) { (void)h; (void)c; }
static void st_clear(void *h) { (void)h; }
static int st_ls(void *l) { (void)l; return 0; }
static int row_state = 1;
static int fake_row_value(int h) { (void)h; return row_state; }
static char lastlog[512]; static int nlog;
static void fake_log(const char *id, const char *fmt, ...)
{ (void)id; va_list a; va_start(a, fmt); vsnprintf(lastlog, sizeof lastlog, fmt, a); va_end(a); nlog++; }
static az_core_v1 FC;
static uint32_t fake_cid[NDECK];
static int fake_deck_info(int deck, az_deck_info *o) { o->pad_mode = -1; o->loaded = fake_cid[deck] != 0; o->content_id = fake_cid[deck]; return 0; }
/* the firmware's store / delete, recorded */
typedef struct { int op; int kind; int64_t in, out; int flag; } rec_t;
static rec_t rec[64]; static int nrec;
static int del_seen;
static void f_orig_delete(void *m, int k, const void *c) { (void)m; (void)k; (void)c; del_seen++; }
static uint8_t FAKE_STORE[64] __attribute__((aligned(16)));
static void f_store(void *s, int kind, void *c) { assert(s == (void *)FAKE_STORE); rec[nrec++] = (rec_t){ PJ_STORE, kind, cue_in(c), cue_out(c), ((uint8_t *)c)[CP_FLAG] }; }
static void f_del(void *s, const void *c) { assert(s == (void *)FAKE_STORE); rec[nrec++] = (rec_t){ PJ_DEL, 0, cue_in(c), cue_out(c), ((const uint8_t *)c)[CP_FLAG] }; }
static void anlz_ready_for(int d, uint32_t cid) { anlz_res[d].cid = cid; anlz_state[d] = 2; anlz_for[d] = cid; }
/* an ANLZ entry (memory loop when hot == 0), times in samples -> ms */
static void anlz_add(int d, int hot, int64_t in, int64_t out, int flag)
{
    anlz_res_t *r = &anlz_res[d];
    r->e[r->n++] = (al_t){ hot, (uint32_t)(in / 44.1 + 0.5), (uint32_t)(out / 44.1 + 0.5), flag };
    if (!hot) r->memcues++;
}
static void reset_state(void)
{
    nrec = 0; copy_n = 0; nlog = 0; lastlog[0] = 0;
    for (int d = 0; d < NDECK; d++) {
        act_ok[d] = 0; spent[d] = 0; pend_op[d] = OP_IDLE; act_src_hot[d] = 0; act_src_idx[d] = 0; active_cid[d] = 0;
        for (int k = 0; k < NHOT; k++) hot_ok[d][k] = 0;
        mem_n[d] = 0; memset(mem_ok[d], 0, sizeof mem_ok[d]); memset(mem_live[d], 0, sizeof mem_live[d]);
        memset(mem_syn[d], 0, sizeof mem_syn[d]); memset(mem_flag[d], 0, sizeof mem_flag[d]); memset(mem_mine[d], 0, sizeof mem_mine[d]);
        ex_inside[d] = 0; ex_past[d] = 0; fake_cid[d] = 0; pv_due[d] = 0; pv_active[d] = 0;
    }
    pj_r = 0; pj_cnt = 0; pj_due = 0; pj_nstore = pj_ndel = 0;
    memset(press_obs_ns, 0, sizeof press_obs_ns);
    for (int k = 0; k < 9; k++) { mkcue(SLOT0[k], 0, 0, 0); mkcue(SLOT1[k], 0, 0, 0); }
    toast_pending = 0; toast_text = ""; FS.req = 0; hot_dirty_until = 0;
    cfg_bits |= CFG_ENABLE; row_state = 1; store_any = FAKE_STORE; mgr_any = NULL;
    fake_cid[0] = 77; for (int d = 0; d < NDECK; d++) { memset(&anlz_res[d], 0, sizeof anlz_res[d]); anlz_state[d] = 0; anlz_for[d] = 0; pv_idx[d] = -1; }
    anlz_ready_for(0, 77);
    led_pub[0] = led_pub[1] = 0; side_deck[0] = 0; side_deck[1] = 1; led_sig[0] = led_sig[1] = 0; led_dbg_n = 0;
}
static void put_hot(int d, int k, int64_t in, int64_t out)
{
    uint8_t c[CUE_SIZE] __attribute__((aligned(16))); mkcue(c, 1, in, out); hot_put(d, k, c);
    mkcue(SLOT(d, k), 1, in, out);
}
/* a memory loop the firmware showed us live, also listed in the ANLZ (the normal case) */
static int put_mem(int d, int64_t in, int64_t out, int flag)
{
    uint8_t c[CUE_SIZE] __attribute__((aligned(16))); mkcue(c, 1, in, out);
    int i = mem_put(d, c); mem_live[d][i] = 1; mem_flag[d][i] = (uint8_t)flag; if (flag) mem_buf[d][i][CP_FLAG] = 1;
    anlz_add(d, 0, in, out, flag);
    return i;
}
/* ANLZ only (what is listed in the file but not in our cache yet) */
static void put_anlz_mem(int d, int64_t in, int64_t out, int flag) { anlz_add(d, 0, in, out, flag); }
/* run the job queue to the end, one job per PJ_GAP ticks */
static void run_jobs(void)
{
    for (int n = 0; n < 200 && pj_cnt; n++) { tick_no += PJ_GAP; pj_service(); }
    assert(pj_cnt == 0);
}
static int count_flagged(int d) { int n = 0; for (int i = 0; i < mem_n[d]; i++) n += mem_loop_ok(d, i) && mem_flag[d][i]; return n; }
static void fresh_deck(void) { reset_state(); }
int main(void)
{
    fw_cue_copy = st_copy; fw_cue_dtor = st_dtor; fw_is_loop = st_loop; fw_loop_slot = st_slot; fw_set = st_set; fw_clear = st_clear;
    orig_store = f_store; fw_store_del = f_del; fw_ls_has_loop = st_ls; fw_ls_looping = st_ls;
    cfg_bits = CFG_ENABLE | CFG_HOTCUE | CFG_CLEAR | CFG_TOAST | CFG_REARM | CFG_AUTOADV;
    engine_of[0] = HOLD0; ls_of[0] = LS0; engine_of[1] = HOLD1; ls_of[1] = LS1;
    C = &FC; h_enable = 1; FC.size = sizeof FC; FC.deck_info = fake_deck_info; FC.log = fake_log; FC.row_value = fake_row_value;
    al_live = 1;
    probe_safe_read();

    char b[160];
#define TOAST_HAS(sub) do { snprintf(b, sizeof b, toast_text, "x"); assert(strstr(b, sub)); } while (0)

    /* ---- 1. first press, nothing in the way: ONE flagged memory copy is stored, ownership only after the read-back ---- */
    reset_state();
    mkcue(SLOT0[2], 1, 2000000, 2500000);
    apply_pad(0, 0, 2);
    assert(act_ok[0] && pend_op[0] == OP_SET && hot_ok[0][2]);
    assert(pj_cnt == 1 && nrec == 0);                                         /* queued, not run inside the press */
    TOAST_HAS("Active Loop saved");
    assert(mem_n[0] == 1 && mem_flag[0][0] && mem_mine[0][0] == OWN_PEND && mem_buf[0][0][CP_FLAG] == 1 && mem_live[0][0]);
    run_jobs();
    assert(nrec == 1 && rec[0].op == PJ_STORE && rec[0].kind == 0 && rec[0].in == 2000000 && rec[0].out == 2500000 && rec[0].flag == 1);
    assert(pv_due[0] && pv_want[0] == 1 && pv_in[0] == 2000000 && pv_pre[0] == 1 && pv_idx[0] == 0);
    assert(count_flagged(0) == 1);
    led_refresh(); assert(led_pub[0] == (1u << 1) && led_pub[1] == 0);        /* the pad is red (armed) */
    /* not confirmed yet: a second press only turns it off, deletes NOTHING, the toast says the flag stays */
    nrec = 0; apply_pad(0, 0, 2);
    assert(!act_ok[0] && pj_cnt == 0 && count_flagged(0) == 1 && mem_ok[0][0]);
    TOAST_HAS("its saved flag stays");
    /* read-back: the file shows exactly one flagged loop there and the expected count: confirmed */
    reset_state(); mkcue(SLOT0[2], 1, 2000000, 2500000); put_mem(0, 100000, 150000, 0);
    apply_pad(0, 0, 2); run_jobs(); assert(pv_pre[0] == 2 && mem_mine[0][pv_idx[0]] == OWN_PEND);
    { int ci = pv_idx[0]; anlz_res_t r; memcpy(&r, &anlz_res[0], sizeof r); anlz_res[0].n = 0; anlz_res[0].memcues = 0;
      r.e[r.n++] = (al_t){ 0, 45351, 56689, 1 }; r.memcues++;                 /* + our flagged loop */
      pv_report(0, &r); assert(strstr(lastlog, "is confirmed") && mem_mine[0][ci] == OWN_OK && owned(0, ci)); assert(pv_idx[0] == -1);
      /* now a second press deletes OUR copy (and only that) */
      nrec = 0; apply_pad(0, 0, 2);
      assert(!act_ok[0] && pend_op[0] == OP_CLEAR && hot_ok[0][2] && pj_cnt == 1);
      assert(toast_pending && !strncmp(toast_text, "Active Loop off", 15));
      run_jobs(); assert(nrec == 1 && rec[0].op == PJ_DEL && rec[0].in == 2000000 && rec[0].out == 2500000 && count_flagged(0) == 0 && live_loops(0) == 1);
      led_refresh(); assert(led_pub[0] == 0); }
    /* read-back with an unexpected loop count (someone else stored/deleted meanwhile): NOT ours for deleting purposes */
    reset_state(); mkcue(SLOT0[2], 1, 2000000, 2500000); put_mem(0, 100000, 150000, 0);
    apply_pad(0, 0, 2); run_jobs();
    { int ci = pv_idx[0]; static anlz_res_t r; memset(&r, 0, sizeof r);
      r.e[r.n++] = (al_t){ 0, 2268, 3401, 0 }; r.e[r.n++] = (al_t){ 0, 45351, 56689, 1 }; r.e[r.n++] = (al_t){ 0, 70000, 71000, 0 }; r.memcues = 3;
      pv_report(0, &r); assert(strstr(lastlog, "will not delete it") && mem_mine[0][ci] == OWN_NONE && !owned(0, ci)); }
    /* read-back with nothing flagged at all: the firmware did not store it: forgotten, nothing deleted, toast */
    reset_state(); mkcue(SLOT0[2], 1, 2000000, 2500000); apply_pad(0, 0, 2); run_jobs();
    { int ci = pv_idx[0]; static anlz_res_t r; memset(&r, 0, sizeof r); pv_report(0, &r);
      assert(!mem_ok[0][ci] && count_flagged(0) == 0); assert(toast_pending && strstr(toast_text, "NOT saved")); }

    /* ---- 2. an UNflagged memory loop with the same in/out exists: the firmware ignores a same-extent store and has no set-flag path:
     *         armed only, NOTHING stored, NOTHING deleted ---- */
    reset_state(); put_hot(0, 3, 4000000, 4400000); int mi = put_mem(0, 4000000, 4400000, 0);
    apply_pad(0, 0, 3);
    assert(act_ok[0] && pj_cnt == 0 && nrec == 0 && mem_n[0] == 1 && !mem_flag[0][mi] && mem_ok[0][mi]);
    TOAST_HAS("same loop already in memory"); assert(strstr(lastlog, "never deletes"));
    /* ANLZ-built twin (never seen live), and a twin a few hundred ms off */
    reset_state(); put_hot(0, 3, 4000000, 4400000); put_anlz_mem(0, 4000000, 4400000, 0);
    apply_pad(0, 0, 3); assert(act_ok[0] && pj_cnt == 0 && nrec == 0); TOAST_HAS("same loop already in memory");
    reset_state(); put_hot(0, 3, 4000000, 4400000); put_anlz_mem(0, 4000000 + 8000, 4400000 - 8000, 0);
    apply_pad(0, 0, 3); assert(pj_cnt == 0); TOAST_HAS("same loop already in memory");
    reset_state(); put_hot(0, 3, 4000000, 4400000); put_mem(0, 4000050, 4400050, 0);                  /* live twin 1 ms off */
    apply_pad(0, 0, 3); assert(pj_cnt == 0 && mem_n[0] == 1);
    /* the twin is already flagged (rekordbox's): armed, "already saved", nothing stored */
    reset_state(); put_hot(0, 3, 4000000, 4400000); mi = put_mem(0, 4000000, 4400000, 1);
    apply_pad(0, 0, 3); assert(act_ok[0] && pj_cnt == 0 && nrec == 0); TOAST_HAS("already saved");
    reset_state(); put_hot(0, 3, 4000000, 4400000); put_anlz_mem(0, 4000000, 4400000, 1);
    apply_pad(0, 0, 3); assert(pj_cnt == 0); TOAST_HAS("already saved");

    /* ---- 3. another flagged loop we do not own (rekordbox's): storing a flagged loop would make the firmware DELETE it: armed only ---- */
    reset_state(); put_hot(0, 4, 6000000, 6400000); int old = put_mem(0, 1000000, 1500000, 1);
    apply_pad(0, 0, 4);
    assert(act_ok[0] && pj_cnt == 0 && nrec == 0 && mem_flag[0][old] && mem_ok[0][old] && mem_n[0] == 1);
    TOAST_HAS("another loop is saved as Active"); assert(strstr(lastlog, "firmware deletes the old flagged loop"));
    reset_state(); put_hot(0, 4, 6000000, 6400000); put_anlz_mem(0, 1000000, 1500000, 1);          /* flag only in the file */
    apply_pad(0, 0, 4); assert(pj_cnt == 0 && mem_n[0] == 0);
    /* our own copy that is not confirmed yet counts as foreign too */
    reset_state(); put_hot(0, 4, 6000000, 6400000); put_hot(0, 5, 8000000, 8300000);
    apply_pad(0, 0, 4); run_jobs(); nrec = 0; apply_pad(0, 0, 5); assert(pj_cnt == 0 && nrec == 0 && count_flagged(0) == 1);
    TOAST_HAS("another loop is saved as Active");

    /* ---- 4. our confirmed copy is replaced by the firmware when the flag moves: one store, NO delete job ---- */
    reset_state(); put_hot(0, 4, 6000000, 6400000); put_hot(0, 5, 8000000, 8300000);
    apply_pad(0, 0, 4); run_jobs(); { int ci = pv_idx[0]; mem_mine[0][ci] = OWN_OK; pv_idx[0] = -1; }
    nrec = 0; apply_pad(0, 0, 5); run_jobs();
    assert(nrec == 1 && rec[0].op == PJ_STORE && rec[0].in == 8000000 && rec[0].flag == 1);
    assert(count_flagged(0) == 1 && live_loops(0) == 1 && cue_in(mem_buf[0][flagged_idx(0)]) == 8000000 && pv_pre[0] == 1);
    TOAST_HAS("Active Loop saved");

    /* ---- 5. second press on the armed pad ---- */
    /* a flagged loop of his (rekordbox's, same extent as the pad): off, nothing deleted, the flag stays, the toast says so */
    reset_state(); put_hot(0, 2, 2000000, 2500000); int hm = put_mem(0, 2000000, 2500000, 1);
    act_ok[0] = 1; active_in[0] = 2000000; active_out[0] = 2500000; act_src_hot[0] = 1; act_src_idx[0] = 2; mkcue(act_buf[0], 1, 2000000, 2500000);
    apply_pad(0, 0, 2); run_jobs();
    assert(nrec == 0 && pj_cnt == 0 && count_flagged(0) == 1 && mem_ok[0][hm] && pend_op[0] == OP_CLEAR);
    TOAST_HAS("its saved flag stays"); assert(!act_ok[0]);
    /* ... and the pad stays red (the flag is still there); pressing it again ARMS it again (never stuck) */
    led_refresh(); assert(led_pub[0] == (1u << 1));
    apply_pad(0, 0, 2); assert(act_ok[0] && pj_cnt == 0); TOAST_HAS("already saved");
    /* ours and confirmed, but no store pointer: the loop STAYS armed, toast says so, nothing disarmed silently */
    reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1); mem_mine[0][hm] = OWN_OK;
    act_ok[0] = 1; active_in[0] = 2000000; active_out[0] = 2500000; act_src_hot[0] = 1; act_src_idx[0] = 2; mkcue(act_buf[0], 1, 2000000, 2500000);
    store_any = NULL; mgr_any = NULL; apply_pad(0, 0, 2);
    assert(act_ok[0] && pend_op[0] != OP_CLEAR && pj_cnt == 0 && count_flagged(0) == 1); TOAST_HAS("stays on");
    store_any = FAKE_STORE;
    /* ours and confirmed, the queue full: stays armed too */
    reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1); mem_mine[0][hm] = OWN_OK;
    act_ok[0] = 1; active_in[0] = 2000000; active_out[0] = 2500000; act_src_hot[0] = 1; act_src_idx[0] = 2; mkcue(act_buf[0], 1, 2000000, 2500000);
    pj_cnt = NPJ; apply_pad(0, 0, 2); assert(act_ok[0] && count_flagged(0) == 1); pj_cnt = 0;
    /* nothing saved for the armed pad: plain off */
    reset_state(); put_hot(0, 2, 2000000, 2500000);
    act_ok[0] = 1; active_in[0] = 2000000; active_out[0] = 2500000; act_src_hot[0] = 1; act_src_idx[0] = 2; mkcue(act_buf[0], 1, 2000000, 2500000);
    apply_pad(0, 0, 2); assert(!act_ok[0] && pj_cnt == 0); TOAST_HAS("Active Loop off: hot cue");

    /* ---- 6. DELETE+pad: the hot cue goes; OUR confirmed copy goes with it, anything else stays ---- */
    reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1); mem_mine[0][hm] = OWN_OK;
    mkcue(SLOT0[2], 0, 0, 0);                                                  /* the firmware cleared the live slot */
    hot_dirty_until = tick_no + 8; hot_sweep();
    assert(!hot_ok[0][2] && pj_cnt == 1 && count_flagged(0) == 0 && !mem_ok[0][hm]);
    run_jobs(); assert(nrec == 1 && rec[0].op == PJ_DEL && rec[0].in == 2000000);
    /* his flagged loop with that extent (not ours): NOT deleted */
    reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1);
    mkcue(SLOT0[2], 0, 0, 0); hot_dirty_until = tick_no + 8; hot_sweep();
    assert(!hot_ok[0][2] && pj_cnt == 0 && count_flagged(0) == 1 && mem_ok[0][hm]); assert(strstr(lastlog, "left in place"));
    /* a copy of ours that was not confirmed: not deleted */
    reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1); mem_mine[0][hm] = OWN_PEND;
    mkcue(SLOT0[2], 0, 0, 0); hot_dirty_until = tick_no + 8; hot_sweep(); assert(pj_cnt == 0 && count_flagged(0) == 1);
    /* another pad holds the same loop: the memory loop stays flagged */
    reset_state(); put_hot(0, 2, 2000000, 2500000); put_hot(0, 3, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1); mem_mine[0][hm] = OWN_OK;
    mkcue(SLOT0[2], 0, 0, 0); hot_dirty_until = tick_no + 8; hot_sweep();
    assert(pj_cnt == 0 && count_flagged(0) == 1);
    /* a pad that is not the flagged loop: nothing */
    reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 7000000, 7500000, 1); mem_mine[0][hm] = OWN_OK;
    mkcue(SLOT0[2], 0, 0, 0); hot_dirty_until = tick_no + 8; hot_sweep(); assert(pj_cnt == 0 && count_flagged(0) == 1);
    /* without the sweep window a vanished slot never deletes anything in the firmware */
    reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1); mem_mine[0][hm] = OWN_OK;
    mkcue(SLOT0[2], 0, 0, 0); tick_no += 100; hot_refresh(0); assert(pj_cnt == 0 && count_flagged(0) == 1);
    /* memory loop deleted by the user (DELETE+MEMORY): the flag and the ownership are forgotten with it */
    reset_state(); hm = put_mem(0, 2000000, 2500000, 1); mem_mine[0][hm] = OWN_OK; mem_gone(0, hm);
    assert(count_flagged(0) == 0 && !mem_mine[0][hm]);
    /* a load is landing: nothing is saved or deleted for the old track's lists */
    reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1); mem_mine[0][hm] = OWN_OK;
    last_load_tick[0] = tick_no - 5; mkcue(SLOT0[2], 0, 0, 0); hot_dirty_until = tick_no + 8; hot_sweep();
    assert(pj_cnt == 0 && count_flagged(0) == 1);
    reset_state(); last_load_tick[0] = tick_no - 5; mkcue(SLOT0[3], 1, 4000000, 4400000); apply_pad(0, 0, 3); assert(act_ok[0] && pj_cnt == 0);
    last_load_tick[0] = 0;

    /* ---- 7. the memory list cap counts loops AND points (the firmware stops at 10 memory cues) ---- */
    reset_state(); put_hot(0, 2, 9000000, 9300000);
    for (int i = 0; i < MEM_SOFT_CAP; i++) put_mem(0, 100000 * (i + 1), 100000 * (i + 1) + 40000, 0);
    apply_pad(0, 0, 2);
    assert(act_ok[0] && pj_cnt == 0 && mem_n[0] == MEM_SOFT_CAP && count_flagged(0) == 0);   /* armed, not saved */
    TOAST_HAS("NOT saved: memory list full");
    reset_state(); put_hot(0, 2, 9000000, 9300000);                            /* 7 loops + 3 points = 10 */
    for (int i = 0; i < 7; i++) put_mem(0, 100000 * (i + 1), 100000 * (i + 1) + 40000, 0);
    anlz_res[0].memcues += 3;
    apply_pad(0, 0, 2); assert(act_ok[0] && pj_cnt == 0); TOAST_HAS("memory list full"); assert(strstr(lastlog, "7 loops + 3 points"));
    reset_state(); put_hot(0, 2, 9000000, 9300000);                            /* 7 loops + 2 points = 9: room for one */
    for (int i = 0; i < 7; i++) put_mem(0, 100000 * (i + 1), 100000 * (i + 1) + 40000, 0);
    anlz_res[0].memcues += 2;
    apply_pad(0, 0, 2); assert(pj_cnt == 1);
    reset_state(); put_hot(0, 2, 9000000, 9300000);                            /* 9 loops, 0 points */
    for (int i = 0; i < 9; i++) put_mem(0, 100000 * (i + 1), 100000 * (i + 1) + 40000, 0);
    apply_pad(0, 0, 2); assert(pj_cnt == 1);
    /* at the cap, replacing our own confirmed copy frees its slot */
    reset_state(); put_hot(0, 2, 9000000, 9300000); put_hot(0, 3, 9500000, 9700000);
    for (int i = 0; i < MEM_SOFT_CAP - 1; i++) put_mem(0, 100000 * (i + 1), 100000 * (i + 1) + 40000, 0);
    apply_pad(0, 0, 2); run_jobs(); { int ci = pv_idx[0]; mem_mine[0][ci] = OWN_OK; pv_idx[0] = -1; anlz_add(0, 0, 9000000, 9300000, 1); }
    assert(live_loops(0) == MEM_SOFT_CAP);
    apply_pad(0, 0, 3); run_jobs(); assert(live_loops(0) == MEM_SOFT_CAP && count_flagged(0) == 1 && cue_in(mem_buf[0][flagged_idx(0)]) == 9500000);

    /* ---- 8. nothing can be saved yet: armed, nothing queued, the toast says why ---- */
    reset_state(); store_any = NULL; mgr_any = NULL; mkcue(SLOT0[2], 1, 2000000, 2500000);
    apply_pad(0, 0, 2);
    assert(act_ok[0] && pj_cnt == 0 && mem_n[0] == 0); TOAST_HAS("not saved yet");
    store_any = FAKE_STORE;
    reset_state(); fake_cid[0] = 0; mkcue(SLOT0[2], 1, 2000000, 2500000); apply_pad(0, 0, 2); assert(act_ok[0] && pj_cnt == 0);   /* no content id */
    reset_state(); anlz_state[0] = 1; mkcue(SLOT0[2], 1, 2000000, 2500000); apply_pad(0, 0, 2); assert(act_ok[0] && pj_cnt == 0); TOAST_HAS("reading the track");   /* ANLZ not read yet */
    reset_state(); anlz_state[0] = 3; mkcue(SLOT0[2], 1, 2000000, 2500000); apply_pad(0, 0, 2); assert(act_ok[0] && pj_cnt == 0 && mem_n[0] == 0);          /* unreadable */
    reset_state(); anlz_res[0].cid = 5; mkcue(SLOT0[2], 1, 2000000, 2500000); apply_pad(0, 0, 2); assert(act_ok[0] && pj_cnt == 0);                        /* another track's data */
    /* the store pointer: the real CueStoreMaintainer is recognised by its vtable (0x2951330), or by its task queue, or searched in the CueCallManager */
    {   static uint8_t store[0x200] __attribute__((aligned(16))), mgr[0x400] __attribute__((aligned(16))), other[64] __attribute__((aligned(16)));
        static uintptr_t qvt[4] __attribute__((aligned(16))); static uint8_t queue[64] __attribute__((aligned(16)));
        store_any = NULL; mgr_any = NULL; assert(store_ptr() == NULL);
        orig_delete = f_orig_delete;
        /* by its vtable, at +0x140 */
        memset(store, 0, sizeof store); memset(mgr, 0, sizeof mgr); *(uintptr_t *)store = VT_CSM; *(uintptr_t *)(mgr + 0x140) = (uintptr_t)store;
        my_delete(mgr, 2, NULL); assert(__atomic_load_n(&mgr_any, __ATOMIC_RELAXED) == (void *)mgr && del_seen >= 1);       /* the hook remembers the manager */
        assert(store_ptr() == (void *)store);
        /* a wrong vtable and no queue: refused */
        *(uintptr_t *)store = VT_CSM + 8; assert(store_ptr() == NULL);
        /* by its task queue: *(store+0x150) -> object whose vtable slot 0 is 0x86ff40 */
        memset(qvt, 0, sizeof qvt); qvt[0] = VA_QUEUE0; *(uintptr_t *)queue = (uintptr_t)qvt; *(uintptr_t *)(store + 0x150) = (uintptr_t)queue;
        assert(store_ptr() == (void *)store);
        *(uintptr_t *)(store + 0x150) = 0; assert(store_ptr() == NULL);
        /* not at +0x140: found by the scan, if exactly one candidate */
        *(uintptr_t *)store = VT_CSM; *(uintptr_t *)(mgr + 0x140) = 0; *(uintptr_t *)(mgr + 0x2c8) = (uintptr_t)store;
        assert(store_ptr() == (void *)store);
        memset(other, 0, sizeof other); *(uintptr_t *)other = VT_CSM; *(uintptr_t *)(mgr + 0x1a0) = (uintptr_t)other; assert(store_ptr() == NULL);   /* two: ambiguous */
        mgr_any = NULL; orig_delete = NULL; }
    store_any = FAKE_STORE;

    /* ---- 9. job spacing, and a track change in between ---- */
    reset_state(); put_hot(0, 4, 6000000, 6400000); put_hot(0, 5, 8000000, 8300000);
    apply_pad(0, 0, 4); run_jobs(); { int ci = pv_idx[0]; mem_mine[0][ci] = OWN_OK; pv_idx[0] = -1; } nrec = 0;
    apply_pad(0, 0, 4);                                                        /* second press: delete our copy ... */
    assert(pj_cnt == 1);
    fake_cid[0] = 99;                                                          /* ... another track now: the lone delete of the old track is dropped */
    run_jobs(); assert(nrec == 0);
    reset_state(); put_hot(0, 4, 6000000, 6400000); apply_pad(0, 0, 4); fake_cid[0] = 5; run_jobs(); assert(nrec == 0 && pj_cnt == 0);   /* a lone store of the old track too */
    reset_state(); put_hot(0, 4, 6000000, 6400000); apply_pad(0, 0, 4); pj_due = 0; tick_no = 1000; pj_service(); assert(nrec == 1);
    pj_cnt = 1; pj[pj_r].op = PJ_DEL; pj[pj_r].d = 0; pj[pj_r].cid = 77; mkcue(pj[pj_r].cue, 1, 1, 2);
    pj_service(); assert(nrec == 1);                                           /* the gap: nothing until PJ_GAP ticks later */
    tick_no += PJ_GAP; pj_service(); assert(nrec == 2 && rec[1].op == PJ_DEL);
    /* jobs finish while the row is off (my_tick off-branch) */
    reset_state(); put_hot(0, 4, 6000000, 6400000); apply_pad(0, 0, 4); row_state = 0; tick_no += 100; my_tick(); row_state = 1; tick_no += 1; my_tick();
    assert(nrec == 1 && led_pub[0] == 0);
    tk_row_was = 1;

    /* ---- 9. LED frame ---- */
    {   unsigned char f[128], g[128];
        reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1);        /* flagged memory loop = hot pad 2 */
        led_refresh(); assert(led_pub[0] == 2u && led_pub[1] == 0);
        memset(f, 0x5a, sizeof f);
        unsigned char *mode = f + 0x0e;                                       /* side 0: HOT CUE button lit, pad cells lit */
        mode[0] = 0x98; mode[1] = 0xff; mode[2] = 0x9a; mode[3] = 0;
        for (int k = 0; k < 8; k++) { unsigned char *c = f + 0x1e + 4 * k; c[0] = 0x10; c[1] = 0xe0; c[2] = 0x20; c[3] = 0; }
        memcpy(g, f, sizeof g);
        assert(on_led(AZ_BUS_ERP_LED, f, 0x78) == AZ_FRAME_OK);
        for (int i = 0; i < 128; i++) {
            int in_cell = i >= 0x1e + 4 && i < 0x1e + 8 && i < 0x1e + 7;   /* pad 2, RGB bytes */
            if (in_cell) continue;
            assert(f[i] == g[i]);                                           /* nothing else is touched (incl. byte 3 of the cell) */
        }
        assert(f[0x22] == 0xff && f[0x23] == 0x00 && f[0x24] == 0x00 && f[0x25] == g[0x25]);   /* pad 2 = cell 1, red, full */
        /* a dim pad keeps the stock envelope */
        memcpy(f, g, sizeof f); f[0x22] = 0x08; f[0x23] = 0x0b; f[0x24] = 0x04; on_led(AZ_BUS_ERP_LED, f, 0x78);
        assert(f[0x22] == 12 && f[0x23] == 0 && f[0x24] == 0);
        /* a dark pad stays dark */
        memcpy(f, g, sizeof f); f[0x22] = f[0x23] = f[0x24] = 0; on_led(AZ_BUS_ERP_LED, f, 0x78); assert(!f[0x22] && !f[0x23] && !f[0x24]);
        /* HOT CUE not the live mode: dim button, BEAT LOOP lit, or the lime secondary page */
        memcpy(f, g, sizeof f); f[0x0e] = 0x07; f[0x0f] = 0x0c; f[0x10] = 0x07; on_led(AZ_BUS_ERP_LED, f, 0x78); assert(f[0x22] == g[0x22] && f[0x23] == g[0x23] && f[0x24] == g[0x24]);
        memcpy(f, g, sizeof f); f[0x0e] = 0x9b; f[0x0f] = 0xff; f[0x10] = 0x00; on_led(AZ_BUS_ERP_LED, f, 0x78); assert(f[0x22] == g[0x22]);
        /* a Pad Colors pick on the HOT CUE button (any hue at full) still counts as lit */
        memcpy(f, g, sizeof f); f[0x0e] = 0x00; f[0x0f] = 0x00; f[0x10] = 0xff; on_led(AZ_BUS_ERP_LED, f, 0x78); assert(f[0x22] == 0xff && f[0x23] == 0 && f[0x24] == 0);
        /* other bus / short frame: untouched */
        memcpy(f, g, sizeof f); on_led(AZ_BUS_MIXER_LED, f, 0x78); assert(!memcmp(f, g, sizeof f));
        on_led(AZ_BUS_ERP_LED, f, 0x70); assert(!memcmp(f, g, sizeof f));
        /* the evidence line: once per change, not per frame */
        reset_state(); put_hot(0, 2, 2000000, 2500000); put_mem(0, 2000000, 2500000, 1); led_refresh();
        memcpy(f, g, sizeof f); nlog = 0; on_led(AZ_BUS_ERP_LED, f, 0x78);
        assert(nlog == 1 && strstr(lastlog, "led side 1: red pads 02") && strstr(lastlog, "pad 2 cell (RGB) 10e020 -> ff0000"));
        memcpy(f, g, sizeof f); on_led(AZ_BUS_ERP_LED, f, 0x78); assert(nlog == 1);
        memcpy(f, g, sizeof f); f[0x0e] = f[0x0f] = f[0x10] = 7; on_led(AZ_BUS_ERP_LED, f, 0x78); assert(nlog == 2 && strstr(lastlog, "not the lit mode"));
        /* side B (deck 2): +0x38 */
        reset_state(); fake_cid[1] = 78; put_hot(1, 8, 5000000, 5100000); put_mem(1, 5000000, 5100000, 1);
        led_refresh(); assert(led_pub[0] == 0 && led_pub[1] == 0x80u);
        memset(f, 0, sizeof f);
        f[0x0e + 0x38] = 0x98; f[0x0f + 0x38] = 0xff; f[0x10 + 0x38] = 0x9a;
        for (int k = 0; k < 8; k++) { unsigned char *c = f + 0x1e + 4 * k + 0x38; c[0] = 0x10; c[1] = 0xe0; c[2] = 0x20; }
        on_led(AZ_BUS_ERP_LED, f, 0x78);
        for (int k = 0; k < 8; k++) { unsigned char *c = f + 0x1e + 4 * k + 0x38; assert(k == 7 ? (c[0] == 0xff && !c[1] && !c[2]) : (c[0] == 0x10 && c[1] == 0xe0 && c[2] == 0x20)); }
        assert(f[0x1e + 0x38 + 28 + 2] == 0 && 0x1e + 0x38 + 28 + 2 < 0x78);                 /* the last cell ends inside the CRC'd part */
        /* secondary deck select: side A drives deck 3 */
        reset_state(); fake_cid[2] = 79; side_deck[0] = 2; engine_of[2] = HOLD0; ls_of[2] = LS0; put_hot(2, 1, 100000, 200000); put_mem(2, 100000, 200000, 1);
        led_refresh(); assert(led_pub[0] == 1u); engine_of[2] = NULL; ls_of[2] = NULL; side_deck[0] = 0;
        /* red goes away with the flag: the very next refresh clears the bit */
        reset_state(); put_hot(0, 2, 2000000, 2500000); hm = put_mem(0, 2000000, 2500000, 1); led_refresh(); assert(led_pub[0] == 2u);
        mem_flag[0][hm] = 0; led_refresh(); assert(led_pub[0] == 0);
        /* an armed loop that is not a pad: no pad red; row off / deck not bound: none */
        reset_state(); put_hot(0, 2, 2000000, 2500000);
        act_ok[0] = 1; active_in[0] = 777; active_out[0] = 999; led_refresh(); assert(led_pub[0] == 0);
        active_in[0] = 2000000; active_out[0] = 2500000; led_refresh(); assert(led_pub[0] == 2u);
        engine_of[0] = NULL; led_refresh(); assert(led_pub[0] == 0); engine_of[0] = HOLD0;
        /* the callback reads only led_pub: with nothing published it never writes */
        led_pub[0] = led_pub[1] = 0; memset(f, 0xa5, sizeof f); memcpy(g, f, sizeof f); on_led(AZ_BUS_ERP_LED, f, 0x78); assert(!memcmp(f, g, sizeof f));
    }

    /* ---- 10. read-back report ---- */
    reset_state(); fake_cid[0] = 77;
    { static anlz_res_t r; memset(&r, 0, sizeof r);
      r.n = 2; r.e[0] = (al_t){ 0, 90000, 91000, 0 }; r.e[1] = (al_t){ 0, 45351, 56689, 1 };       /* 2000000 / 44.1 = 45351 ms */
      pv_in[0] = 2000000; pv_out[0] = 2500000; pv_want[0] = 1; put_mem(0, 2000000, 2500000, 1); anlz_res[0].n = 0; anlz_res[0].memcues = 0;
      pv_report(0, &r); assert(strstr(lastlog, "OK") && !strstr(lastlog, "MISMATCH") && strstr(lastlog, "2 memory loops (0 cues in all), 1 flagged"));
      pv_want[0] = 0; pv_report(0, &r); assert(strstr(lastlog, "MISMATCH"));
      r.e[1].flag = 0; pv_report(0, &r); assert(strstr(lastlog, "OK"));
      pv_want[0] = 1; pv_report(0, &r); assert(strstr(lastlog, "MISMATCH"));
    }
    reset_state(); pv_due[0] = tick_no + 5; pv_service(); assert(!pv_active[0]); tick_no += 10; pv_service(); assert(pv_active[0] && anlz_for[0] == 0 && pv_due[0] == 0);
    reset_state(); pv_due[0] = tick_no - 1; pj_cnt = 1; pv_service(); assert(!pv_active[0]); pj_cnt = 0;            /* waits for the jobs */

    printf("persist_test: ok\n");
    return 0;
}
