/* TEST-ONLY: host unit test for fader-start 0.0.5 (decision gates, never-reloop, object
 * learning / fill-in / vtable checks, onMoved edges and actions, disarm and re-arm, filter
 * fallback, decode, LED loop rule, deck select, rate limit, logging gate; 0.0.5: has_loop read
 * through the Play facade, LoopState/core cross-check, paused-deck fallback, CueLoop scan).
 *   gcc -std=gnu11 -g -O1 -fsanitize=address,undefined -I<sdk>/include test/unit_test.c */
#define FS_UNIT 1
#define FS_SCAN 1   /* the 0.0.5 scan cases still run; the shipped build has FS_SCAN 0 */
#include "../fader-start.c"
#include <assert.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>

static char LOGS[1024][600];
static int nlog;
static void f_log(const char *id, const char *fmt, ...)
{
    assert(!strcmp(id, MOD_ID));
    va_list ap; va_start(ap, fmt);
    if (nlog < 1024) vsnprintf(LOGS[nlog++], sizeof LOGS[0], fmt, ap);
    va_end(ap);
}
static const char *f_tag(void) { return FW_TAG; }
static int f_thread(const char *id, const char *n, void *(*fn)(void *), void *a) { (void)id; (void)n; (void)fn; (void)a; return 0; }
static az_frame_fn FF[8]; static unsigned FM[8]; static int nff;
static int f_filter(const char *id, unsigned m, az_frame_fn fn, void *u) { (void)id; (void)u; FM[nff] = m; FF[nff++] = fn; return 0; }
static int nclaim;
static int f_claim(const char *id, uintptr_t a, void *h, void **o) { (void)id; (void)a; (void)h; (void)o; nclaim++; return 0; }
static int nrows, rowval[16]; static char rowkey[16][32];
static int f_reg_row(const char *id, const az_row_v1 *r)
{
    (void)id; rowval[nrows] = r->kind == AZ_ROW_TOGGLE ? !!(r->flags & AZ_ROW_DEFAULT_ON) : 1;
    snprintf(rowkey[nrows], sizeof rowkey[0], "%s", r->key); return nrows++;
}
static int f_row_value(int h) { return h >= 0 && h < nrows ? rowval[h] : -1; }
static int f_reg_screen(const char *id, const az_screen_v1 *s) { (void)id; (void)s; return 0; }
static int f_deck_ev(const char *id, void (*fn)(int, int, void *), void *u) { (void)id; (void)fn; (void)u; return 0; }
static int D_loaded[4] = { 1, 1, 0, 1 }, D_play[4] = { 0, 0, 0, 0 };
static int f_deck_info(int d, az_deck_info *o) { if (d < 0 || d > 3) return -1; o->loaded = D_loaded[d]; o->playing = D_play[d]; return 0; }
static int nbanner; static const char *last_banner;
static void f_banner(const char *id, const char *text, int ms) { (void)id; (void)ms; nbanner++; last_banner = text; }
static az_core_v1 CORE;

/* fake EP147 memory: addr -> u64 */
#define NM 256
static uintptr_t MA[NM]; static uint64_t MV[NM]; static int nm;
static void mem_set(uintptr_t a, uint64_t v) { for (int i = 0; i < nm; i++) if (MA[i] == a) { MV[i] = v; return; } MA[nm] = a; MV[nm++] = v; }
static int mem_read(uintptr_t a, uint64_t *v) { for (int i = 0; i < nm; i++) if (MA[i] == a) { *v = MV[i]; return 0; } return -1; }
#define P1 0x7f8a34cc20ull
#define CUE1 0x7f4dc24900ull
#define CUE2 0x7f4dc26700ull
#define LS1 0x7f00001000ull
#define LS2 0x7f00002000ull
#define LS3 0x7f00003000ull
#define LS4 0x7f00004000ull
#define CORE1 0x7f10000100ull
#define CORE2 0x7f10001100ull
#define CORE3 0x7f10002100ull
#define CORE4 0x7f10003100ull
#define MGR 0x7f55550000ull
/* a registered loop in LoopState's CueProperty (+0x160), as has_loop 0x14a7668 tests it */
static void set_loop(uint64_t ls, int on)
{
    mem_set(ls + LS_LOOPCUE + 0x30, on ? 0x0001000100000001ull : 0);   /* +0x30 = 1, +0x34 = 1, +0x36 = 1 */
    mem_set(ls + LS_LOOPCUE + 0x38, on ? 44100u : 0);                  /* +0x38 != 0 */
    mem_set(ls + LS_LOOPCUE + 0x40, on ? 88200u : 0x7fffffffffffffffull);
}
static void cue_obj_at(uint64_t a, uint64_t ls, uint64_t core)
{
    mem_set(a, VT_CUELOOP); mem_set(a + 8, VT_CUELOOP2); mem_set(a + 0x10, VT_CUELOOP3);
    mem_set(a + CUE_LS_OFF, ls); mem_set(a + CUE_CORE_OFF, core);
}

/* the only EP147 entry points the mod may call, as stubs */
static int ncall; static char calls[64][16]; static uint64_t call_obj[64]; static unsigned call_a[64], call_b[64];
static uint64_t stub_delay_ns;
static void stub_play10(void *s, uint32_t a, uint32_t b)
{ snprintf(calls[ncall], 16, "play"); call_obj[ncall] = (uintptr_t)s; call_a[ncall] = a; call_b[ncall++] = b; fs_unit_now += stub_delay_ns; }
static void stub_loopexit_call(void *s) { snprintf(calls[ncall], 16, "loopExit"); call_obj[ncall++] = (uintptr_t)s; }
/* originals behind our hooks (EP147's own calls pass through these) */
static int n_orig_reloop, n_orig_loopexit, n_orig_play18, n_orig_onmoved, n_orig_playop;
static void o_reloop(void *s, uint64_t a) { (void)s; (void)a; n_orig_reloop++; }
static void o_loopexit(void *s) { (void)s; n_orig_loopexit++; }
static void o_play18(void *s, uint64_t a, void *j, uint64_t b) { (void)s; (void)a; (void)j; (void)b; n_orig_play18++; }
static void o_playop(void *s, uint64_t a, uint64_t b, uint64_t c) { (void)s; (void)a; (void)b; (void)c; n_orig_playop++; }
static void o_onmoved(void *s, void *sl, uint64_t id, const void *p) { (void)s; (void)sl; (void)id; (void)p; n_orig_onmoved++; }

static void at_ms(uint64_t ms) { fs_unit_now = 1000000000ull + ms * 1000000ull; }
static unsigned char MF[128], EF[32], LF[128];
static void mix(uint64_t ms, int a, int b, int c, int d)
{
    int v[4] = { a, b, c, d };
    memset(MF, 0, sizeof MF);
    for (int i = 0; i < 4; i++) { MF[0x0a + i] = (unsigned char)(v[i] >> 2); MF[0x0e] |= (unsigned char)((v[i] & 3) << (6 - 2 * i)); }
    MF[0x0f] = 0x80;
    at_ms(ms);
    assert(on_mixer(AZ_BUS_MIXER_IN, MF, 120, NULL) == AZ_FRAME_OK);
    my_tick();
}
/* EP147's position word 0 is a float 0..1.0 (AZ-EMU fs003a); word 1 is junk to us */
static uint32_t fbits(float v) { uint32_t b; memcpy(&b, &v, 4); return b; }
static void moved_bits(uint64_t ms, int ch, uint32_t bits)
{
    uint32_t pos[2] = { bits, 0xdeadbeefu };
    at_ms(ms);
    my_onmoved((void *)(uintptr_t)MGR, (void *)0x1, FADER_ID0 + (uint64_t)ch, pos);
}
static void moved(uint64_t ms, int ch, uint32_t raw)
{
    uint32_t pos[2] = { fbits((float)raw / 1023.0f), 0xdeadbeefu };
    at_ms(ms);
    my_onmoved((void *)(uintptr_t)MGR, (void *)0x1, FADER_ID0 + (uint64_t)ch, pos);
}
/* closed for 400 ms, then up: one clean edge on channel ch (1-based) */
static void edge(uint64_t *t, int ch)
{
    moved(*t, ch - 1, 0);
    moved(*t += 400, ch - 1, 300);
    moved(*t += 50, ch - 1, 1023);
}
static void press(uint64_t ms, int side, int byte, int bit)
{
    at_ms(ms);
    EF[byte + 8 * side] |= (unsigned char)(1u << bit); on_erp(AZ_BUS_ERP_IN, EF, 30, NULL);
    EF[byte + 8 * side] &= (unsigned char)~(1u << bit); on_erp(AZ_BUS_ERP_IN, EF, 30, NULL);
}
static void drain(void) { while (drain_once()) {} }
static int count(const char *pfx) { int n = 0; for (int i = 0; i < nlog; i++) if (strstr(LOGS[i], pfx)) n++; return n; }
static const char *find(const char *pfx, int nth) { for (int i = 0; i < nlog; i++) if (strstr(LOGS[i], pfx) && nth-- == 0) return LOGS[i]; return NULL; }
static void clear(void) { nlog = 0; bucket.init = 0; pbucket.init = 0; ncall = 0; }
static fs_in base_in(void)
{
    fs_in i = { .en = 1, .exit_on = 1, .disarmed = 0, .safe_read = 1, .automix = 0, .thread = 1, .mod_guard = 0,
                .cooldown = 0, .info_ok = 1, .loaded = 1, .track_guard = 0, .input = -1, .play_obj = 1, .playing = 0,
                .recent_press = 0, .loop = 0, .cue_obj = 1 };
    return i;
}
static int dec(fs_in i, int *r) { return fs_decide(&i, r); }

int main(void)
{
    CORE.abi = AZ_MOD_ABI; CORE.size = sizeof CORE; CORE.log = f_log; CORE.fw_tag = f_tag;
    CORE.thread_start = f_thread; CORE.frame_filter = f_filter; CORE.claim_hook = f_claim;
    CORE.register_row = f_reg_row; CORE.row_value = f_row_value; CORE.register_screen = f_reg_screen;
    CORE.on_deck_event = f_deck_ev; CORE.deck_info = f_deck_info; CORE.banner = f_banner;
    fs_unit_read = mem_read; fs_unit_play10 = stub_play10; fs_unit_loopexit = stub_loopexit_call;
    orig_reloop = o_reloop; orig_loopexit = o_loopexit; orig_play18 = o_play18; orig_playop = o_playop; orig_onmoved = o_onmoved;
    at_ms(0);
    assert(MOD.init == init && az_mod_v1_entry() == &MOD);
    assert(init(&CORE) == 0);
    /* rows: group, master (default OFF), exit loop (default ON), logging (ON), On-Air hook (off) */
    assert(nrows == 5 && !strcmp(rowkey[h_en], "enable") && rowval[h_en] == 0 && rowval[h_exit] == 1 &&
           rowval[h_log] == 1 && rowval[h_onair] == 0);
    assert(nff == 3 && (FM[0] & AZ_FILTER_RAW) && (FM[1] & AZ_FILTER_RAW));
    /* host: no EP147 code, so every target fails its signature; play +0x10 is never claimed */
    assert(nclaim == 0 && claimed == 0 && sig_failed == 0x1f);
    assert(count("NOT HOOKED play slot+0x10 0x14415a8 (owned by automix)") == 1 && count("SIG mismatch") == 5);
    assert(count("loaded 0.0.7 ACTUATING (Fader Start row off)") == 1);
    /* conflict table, +-16 bytes, includes the core's own mixer hooks */
    assert(conflict_owner(0x14415a8u) && conflict_owner(0x14415a8u + 12) && !conflict_owner(0x14415a8u + 16));
    assert(conflict_owner(0x0abec10u) && conflict_owner(0x0abdd18u + 8));
    for (unsigned i = 0; i < H_N; i++) assert(!conflict_owner(HOOKS[i].addr));
    assert(!strcmp(playop_via(0x1441484u), "slot+0x10-task") && !strcmp(playop_via(0x1444158u), "slot+0x18-task"));
    drain(); clear();

    /* ---- decision gates, in order ---- */
    {
        int r, a;
        a = dec(base_in(), &r); assert(a == A_PLAY && r == RS_OK);
        fs_in i = base_in(); i.en = 0; assert(dec(i, &r) == A_NONE && r == RS_OFF);
        i = base_in(); i.disarmed = 1; assert(dec(i, &r) == A_NONE && r == RS_DISARMED);
        i = base_in(); i.safe_read = 0; assert(dec(i, &r) == A_NONE && r == RS_NO_SAFE_READ);
        i = base_in(); i.automix = 1; assert(dec(i, &r) == A_NONE && r == RS_AUTOMIX);
        i = base_in(); i.thread = 0; assert(dec(i, &r) == A_NONE && r == RS_THREAD_UNKNOWN);
        i = base_in(); i.thread = -1; assert(dec(i, &r) == A_NONE && r == RS_THREAD_MISMATCH);
        i = base_in(); i.mod_guard = 1; assert(dec(i, &r) == A_NONE && r == RS_MOD_LOAD);
        i = base_in(); i.cooldown = 1; assert(dec(i, &r) == A_NONE && r == RS_COOLDOWN);
        i = base_in(); i.info_ok = 0; assert(dec(i, &r) == A_NONE && r == RS_NO_DECK_INFO);
        i = base_in(); i.loaded = 0; assert(dec(i, &r) == A_NONE && r == RS_DECK_EMPTY);
        i = base_in(); i.track_guard = 1; assert(dec(i, &r) == A_NONE && r == RS_TRACK_LOAD);
        i = base_in(); i.input = 0; assert(dec(i, &r) == A_NONE && r == RS_INPUT_NOT_DECK);
        i = base_in(); i.input = -2; assert(dec(i, &r) == A_NONE && r == RS_INPUT_UNKNOWN);
        i = base_in(); i.input = 1; assert(dec(i, &r) == A_PLAY);
        i = base_in(); i.play_obj = 0; assert(dec(i, &r) == A_NONE && r == RS_PLAY_OBJ_UNKNOWN);
        i = base_in(); i.play_obj = -1; assert(dec(i, &r) == A_NONE && r == RS_PLAY_OBJ_INVALID);
        i = base_in(); i.playing = -1; assert(dec(i, &r) == A_NONE && r == RS_PLAY_STATE_UNKNOWN);
        i = base_in(); i.recent_press = 1; assert(dec(i, &r) == A_NONE && r == RS_RECENT_PRESS);
        /* loop handling (0.0.5): unknown = play only if paused (fallback, logged); looping = exit
         * (+play if paused); looping without a CueLoop facade = play only if paused (fallback) */
        i = base_in(); i.loop = -1; assert(dec(i, &r) == A_PLAY && r == RS_FB_LOOP_UNKNOWN);
        i = base_in(); i.loop = -1; i.playing = 1; assert(dec(i, &r) == A_NONE && r == RS_LOOP_UNKNOWN);
        i = base_in(); i.loop = 1; assert(dec(i, &r) == A_EXIT_PLAY && r == RS_OK);
        i = base_in(); i.loop = 1; i.playing = 1; assert(dec(i, &r) == A_EXIT && r == RS_OK);
        i = base_in(); i.loop = 1; i.cue_obj = 0; assert(dec(i, &r) == A_PLAY && r == RS_FB_NO_CUE_OBJ);
        i = base_in(); i.loop = 1; i.cue_obj = 0; i.playing = 1; assert(dec(i, &r) == A_NONE && r == RS_CUE_OBJ_UNKNOWN);
        i = base_in(); i.loop = 1; i.cue_obj = -1; assert(dec(i, &r) == A_NONE && r == RS_CUE_OBJ_INVALID);
        i = base_in(); i.loop = 1; i.cue_obj = -1; i.playing = 1; assert(dec(i, &r) == A_NONE && r == RS_CUE_OBJ_INVALID);
        i = base_in(); i.playing = 1; assert(dec(i, &r) == A_NONE && r == RS_ALREADY_PLAYING);
        /* exit loop row off: loop state not needed, play only if paused */
        i = base_in(); i.exit_on = 0; i.loop = -1; i.cue_obj = 0; assert(dec(i, &r) == A_PLAY);
        i = base_in(); i.exit_on = 0; i.loop = 1; i.playing = 1; assert(dec(i, &r) == A_NONE && r == RS_ALREADY_PLAYING);
        /* exhaustive: no input combination yields anything but none/play/exit/exit+play, play only
         * when paused, exit only when looping and the loop row is on */
        int n = 0;
        for (unsigned m = 0; m < (1u << 12); m++) {
            fs_in x = base_in();
            x.en = m & 1; x.exit_on = m >> 1 & 1; x.automix = m >> 2 & 1; x.playing = (int)(m >> 3 & 1) - (int)(m >> 4 & 1);
            x.loop = (int)(m >> 5 & 1) - (int)(m >> 6 & 1); x.cue_obj = (int)(m >> 7 & 1) - (int)(m >> 8 & 1);
            x.play_obj = (int)(m >> 9 & 1) - (int)(m >> 10 & 1); x.recent_press = m >> 11 & 1;
            a = dec(x, &r);
            assert(a >= A_NONE && a <= A_EXIT_PLAY);
            if (a != A_NONE) { n++; assert(x.en && !x.automix && x.play_obj == 1 && x.playing >= 0 && !x.recent_press); }
            if (a == A_PLAY) assert(x.playing == 0 && (r == RS_OK || r == RS_FB_LOOP_UNKNOWN || r == RS_FB_NO_CUE_OBJ));
            if (r == RS_FB_LOOP_UNKNOWN) assert(a == A_PLAY && x.exit_on && x.loop == -1);
            if (r == RS_FB_NO_CUE_OBJ) assert(a == A_PLAY && x.exit_on && x.loop == 1 && x.cue_obj == 0);
            if (a == A_EXIT_PLAY) assert(x.playing == 0 && x.loop == 1 && x.exit_on && x.cue_obj == 1);
            if (a == A_EXIT) assert(x.playing == 1 && x.loop == 1 && x.exit_on && x.cue_obj == 1);
        }
        assert(n > 0);
    }

    /* ---- fake EP147: Play facades 0x620 apart, CueLoop for deck 1/2, OnAirManager ---- */
    mem_set(VA_PLAY10, PLAY10_WORD0 | 0x910003fd00000000ull);
    for (int d = 0; d < 4; d++) mem_set((uintptr_t)(P1 + 0x620u * (unsigned)d), VT_PLAYPAUSE);
    cue_obj_at(CUE1, LS1, CORE1); cue_obj_at(CUE2, LS2, CORE2);
    set_loop(LS1, 0); set_loop(LS2, 0); set_loop(LS3, 0); set_loop(LS4, 0);
    /* the Play facades' LoopState / core are set later (first: unreadable = loop unknown) */
    mem_set(MGR, VT_ONAIRMGR); mem_set(MGR + ONAIR_IN_OFF, 0x0000000100000000ull);  /* ch3 flag (byte 2) 0, ch4 (byte 3) 0 */
    claimed = 1u << H_ONMOVED;               /* as on the deck: onMoved is the edge source */
    uint64_t t = 100;

    /* deck select on both sides */
    press(t, 0, 0x08, 0); press(t += 5, 1, 0x08, 0);
    assert(erp_sel[0] == 1 && erp_sel[1] == 2);
    drain(); clear();

    /* edges inside 2 s of mod load: skipped (mod-load-guard), nothing called */
    edge(&t, 1);
    drain();
    assert(ncall == 0 && n_orig_onmoved == 3);
    { const char *s = find("SKIP ch=1 src=hook action=none reason=off would=", 0); assert(s); }
    rowval[h_en] = 1;                        /* master on */
    t = 600; edge(&t, 2);                    /* still < 2 s */
    drain();
    assert(ncall == 0 && find("SKIP ch=2 src=hook action=none reason=mod-load-guard", 0));

    /* after 3 s: no objects yet and no bus thread: thread-unknown first (ERP thread known though) */
    t = 3000; clear();
    edge(&t, 1); drain();
    assert(ncall == 0 && find("reason=play-obj-unknown", 0));     /* ERP filter thread = bus thread here */

    /* learn: PLAY press on side A (deck 1 selected) and EP147's play18 on deck 1 */
    clear();
    press(t += 100, 0, 0x0a, 1);
    at_ms(t += 1); my_play18((void *)(uintptr_t)P1, 0, (void *)0x7f7e7fac70, 0);
    drain();
    assert(n_orig_play18 == 1);
    assert(find("LEARN play deck=1 obj=0x7f8a34cc20 ok", 0) && find("FILL play deck=2 obj=0x7f8a34d240 ok", 0) &&
           find("FILL play deck=3 obj=0x7f8a34d860 ok", 0) && find("FILL play deck=4 obj=0x7f8a34de80 ok", 0));
    assert(play_src[0] == 1 && play_src[1] == 2 && hook_bus_thr == thread_ptr());
    /* a playop from our own call (no press) teaches nothing */
    at_ms(t += 1000); my_playop((void *)(uintptr_t)(P1 + 0x620 + 8), 0, 0, 0); drain();
    assert(play_src[1] == 2);

    /* deck 1 paused, exit-loop on, the Play facade's LoopState unreadable and the LED filter never
     * ran: loop unknown -> a paused deck still plays (fallback, logged) */
    clear();
    t += 2000; edge(&t, 1); drain();
    assert(ncall == 1 && !strcmp(calls[0], "play") && find("ACT ch=1 src=hook action=play reason=fallback-loop-unknown", 0) &&
           find("loop=-1(-)", 0) && find("LOOPDIAG ch=1 deck=1 hasloop=-1", 0));
    /* ... and a playing one is left alone */
    D_play[0] = 1; clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 0 && find("SKIP ch=1 src=hook action=none reason=loop-unknown", 0));
    D_play[0] = 0;
    /* exit-loop row off: play is called (start 0, slip 0) on deck 1's facade */
    rowval[h_exit] = 0; clear();
    t += 2000; edge(&t, 1); drain();
    assert(ncall == 1 && !strcmp(calls[0], "play") && call_obj[0] == P1 && call_a[0] == 0 && call_b[0] == 0);
    { const char *s = find("ACT ch=1 src=hook action=play reason=ok call_us play=", 0); assert(s && strstr(s, "play_obj=0x7f8a34cc20(L)")); }
    /* one action per edge, and 1 s per channel: a second edge 500 ms later is skipped */
    clear(); D_play[0] = 1;
    moved(t += 10, 0, 0); moved(t += 300, 0, 500); drain();
    assert(ncall == 0 && find("reason=cooldown", 0));
    /* the deck plays now: already-playing */
    clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 0 && find("reason=already-playing", 0));
    D_play[0] = 0; rowval[h_exit] = 1;

    /* 0.0.5: the deck's LoopState and core through each Play facade (no press needed) */
    { const uint64_t ls[4] = { LS1, LS2, LS3, LS4 }, core[4] = { CORE1, CORE2, CORE3, CORE4 };
      for (int d = 0; d < 4; d++) { mem_set(P1 + 0x620u * (unsigned)d + PLAY_LS_OFF, ls[d]); mem_set(P1 + 0x620u * (unsigned)d + PLAY_CORE_OFF, core[d]); } }
    /* no loop registered: play; the old +8 byte (cue-play flag, 1 here) no longer counts */
    mem_set(LS1 + LS_CUEPLAY, 1);
    clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 1 && !strcmp(calls[0], "play") && find("ACT ch=1 src=hook action=play reason=ok", 0) && find("loop=0(hasloop)", 0));
    { const char *s = find("LOOPDIAG ch=1 deck=1 hasloop=0 led_loop=-1 sel=1 src=hasloop ls=0x7f00001000 ls_cue=0 core=0x7f10000100/0", 0);
      assert(s && strstr(s, "ls08=0000000000000001") && strstr(s, "lc40=7fffffffffffffff")); }
    /* a registered loop but no CueLoop facade: paused -> play (fallback); playing -> nothing */
    set_loop(LS1, 1); mem_set(LS1 + LS_CUEPLAY, 0);
    clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 1 && !strcmp(calls[0], "play") && find("ACT ch=1 src=hook action=play reason=fallback-no-cue-obj", 0) &&
           find("loop=1(hasloop)", 0) && find("LOOPDIAG ch=1 deck=1 hasloop=1", 0));
    D_play[0] = 1; clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 0 && find("SKIP ch=1 src=hook action=none reason=cue-obj-unknown", 0));
    D_play[0] = 0;
    /* each has_loop condition on its own clears it (0x14a7668) */
    { static const struct { uint64_t off, val; } K[] = { { 0x30, 0x0001000100000000ull }, { 0x30, 0x0001002000000001ull },
                                                         { 0x30, 0x0000000100000001ull }, { 0x38, 0 }, { 0x40, 0x7fffffffffffffffull } };
      for (unsigned k = 0; k < sizeof K / sizeof K[0]; k++) {
          set_loop(LS1, 1); assert(ls_has_loop(LS1) == 1);
          mem_set(LS1 + LS_LOOPCUE + K[k].off, K[k].val); assert(ls_has_loop(LS1) == 0);
      }
      assert(ls_has_loop(0) == -1 && ls_has_loop(0x7f0000a000ull) == -1); }
    set_loop(LS1, 0);

    /* learn deck 1's CueLoop object from EXIT (reloop button) + EP147's loopExit */
    clear();
    press(t += 100, 0, 0x09, 3);
    at_ms(t += 1); my_loopexit((void *)(uintptr_t)CUE1); drain();
    assert(n_orig_loopexit == 1 && cue_src[0] == 1 && find("LEARN cue deck=1 obj=0x7f4dc24900 ok", 0));
    /* paused, no loop registered: play */
    clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 1 && !strcmp(calls[0], "play") && find("loop=0(hasloop)", 0) &&
           find("ls=0x7f00001000 ls_cue=0x7f00001000 core=0x7f10000100/0x7f10000100", 0));
    /* paused AND a loop registered: loopExit first, then play */
    set_loop(LS1, 1); clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 2 && !strcmp(calls[0], "loopExit") && call_obj[0] == CUE1 && !strcmp(calls[1], "play") && call_obj[1] == P1);
    assert(find("ACT ch=1 src=hook action=exit+play reason=ok call_us loopExit=", 0));
    /* playing AND looping: loopExit only (the deck keeps playing; deck run 2) */
    D_play[0] = 1; clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 1 && !strcmp(calls[0], "loopExit"));
    assert(find("ACT ch=1 src=hook action=exit reason=ok", 0));
    D_play[0] = 0; set_loop(LS1, 0);
    /* a CueLoop facade whose LoopState or core is not its deck's: invalid, nothing called, disarm */
    for (int k = 0; k < 2; k++) {
        set_loop(LS1, 1);
        if (k == 0) mem_set(CUE1 + CUE_LS_OFF, LS2); else mem_set(CUE1 + CUE_CORE_OFF, CORE2);
        clear(); t += 2000; edge(&t, 1); my_tick(); drain();
        assert(ncall == 0 && find("reason=cue-obj-invalid", 0) && disarm_why == D_LS_MISMATCH && last_banner &&
               !strcmp(last_banner, "Fader Start disarmed: loop object mismatch") &&
               find("ARM state=DISARMED reason=cueloop-loopstate-or-core-differs-from-play-facade", 0));
        cue_obj_at(CUE1, LS1, CORE1);
        rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();
        assert(disarm_why == 0);
    }
    set_loop(LS1, 0);

    /* RELOOP press goes through our pass-through hook; the mod itself never calls reloop */
    press(t += 100, 0, 0x09, 3);
    at_ms(t += 1); my_reloop((void *)(uintptr_t)CUE1, 0); drain();
    assert(n_orig_reloop == 1);

    /* deck 2 (fill-in object): no CueLoop object; a loop is registered; side B selected deck 2
     * and the LED filter runs: LED says not active -> play; LED active -> needs the CueLoop
     * object, so (paused) play as the fallback */
    set_loop(LS2, 1);
    memset(LF, 0, sizeof LF);
    for (int k = 0; k < 3; k++) { at_ms(t += 10); on_led(AZ_BUS_ERP_LED, LF, 0x78, NULL); my_tick(); }
    clear(); t += 2000;
    for (uint64_t x = t - 1500; x < t; x += 300) { at_ms(x); on_led(AZ_BUS_ERP_LED, LF, 0x78, NULL); my_tick(); }
    edge(&t, 2); drain();
    assert(ncall == 1 && call_obj[0] == P1 + 0x620 && find("action=play reason=ok", 0) && find("loop=0(hasloop+led)", 0) &&
           find("play_obj=0x7f8a34d240(F)", 0));
    /* LED loop on side B: IN/OUT blinking with RELOOP lit */
    clear();
    for (int k = 0; k < 8; k++) {
        LF[0x38 + 0x0a] = (unsigned char)(k & 1 ? 0x00 : 0xa0); LF[0x38 + 0x09] = 0x02;
        at_ms(t += 200); on_led(AZ_BUS_ERP_LED, LF, 0x78, NULL); my_tick();
    }
    assert(side_loop(1) == 1 && led_loop_pub[1] == 1);
    t += 400; moved(t, 1, 0); moved(t += 400, 1, 300);
    for (int k = 0; k < 2; k++) { LF[0x38 + 0x0a] = (unsigned char)(k & 1 ? 0x00 : 0xa0); at_ms(t + 1); on_led(AZ_BUS_ERP_LED, LF, 0x78, NULL); }
    drain();
    assert(ncall == 1 && !strcmp(calls[0], "play") && find("reason=fallback-no-cue-obj", 0) && find("loop=1(hasloop+led)", 0));
    /* LED filter silent (switched off): has_loop alone (filter-independent) */
    clear(); at_ms(t += 3000); my_tick(); at_ms(t += 1500); my_tick(); drain();
    assert(led_loop_pub[1] == -1 && find("FILTER_SILENT led", 0));
    t += 100; edge(&t, 2); drain();
    assert(ncall == 1 && find("ACT ch=2 src=hook action=play reason=fallback-no-cue-obj", 0) && find("loop=1(hasloop)", 0));
    /* learn deck 2's CueLoop: exit + play */
    press(t += 100, 1, 0x09, 3); at_ms(t += 1); my_loopexit((void *)(uintptr_t)CUE2); drain();
    clear(); t += 2000; edge(&t, 2); drain();
    assert(ncall == 2 && !strcmp(calls[0], "loopExit") && call_obj[0] == CUE2 && find("action=exit+play", 0) && find("loop=1(hasloop)", 0));
    set_loop(LS2, 0);
    clear(); t += 2000; edge(&t, 2); drain();
    assert(ncall == 1 && !strcmp(calls[0], "play") && find("loop=0(hasloop)", 0));

    /* recent PLAY press on that deck (500 ms): skip */
    clear(); t += 2000;
    moved(t, 1, 0); press(t += 300, 1, 0x0a, 1); moved(t += 200, 1, 500); drain();
    assert(ncall == 0 && find("reason=recent-press", 0));
    /* track load guard: deck 2 loaded 1 s before the edge */
    clear(); t += 2000; at_ms(t); deck_event(1, AZ_DECK_LOADED, NULL);
    moved(t, 1, 0); moved(t += 400, 1, 500); drain();
    assert(ncall == 0 && find("reason=track-load-guard", 0));
    /* empty deck 3 */
    clear(); t += 3000; edge(&t, 3); drain();
    assert(ncall == 0 && find("SKIP ch=3 src=hook action=none reason=deck-empty", 0));
    /* channel 4: input flag 0 = not DECK; 1 = deck (deck 4 has no CueLoop and no LED: exit-loop off) */
    clear(); rowval[h_exit] = 0; t += 2000; edge(&t, 4); drain();
    assert(ncall == 0 && find("reason=input-not-deck", 0));
    mem_set(MGR + ONAIR_IN_OFF, 0x0000000001000000ull);   /* byte 3 (+0x3bb) = 1 */
    clear(); t += 2000; edge(&t, 4); drain();
    assert(ncall == 1 && call_obj[0] == P1 + 3 * 0x620 && find("input=deck", 0));
    rowval[h_exit] = 1;

    /* automix's hook on play +0x10 (first word changed): refuse */
    mem_set(VA_PLAY10, 0x14000000ull); clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 0 && find("reason=automix-hook", 0));
    mem_set(VA_PLAY10, PLAY10_WORD0);

    /* invalid object (vtable changed): skip and disarm; the master off/on re-arms */
    int nb0 = nbanner;
    mem_set(P1, 0x1234); clear(); t += 2000; edge(&t, 1); my_tick(); drain();
    assert(ncall == 0 && find("reason=play-obj-invalid", 0) && disarm_why == D_OBJ_INVALID);
    assert(find("ARM state=DISARMED reason=object-vtable-check-failed", 0) && nbanner == nb0 + 1);
    mem_set(P1, VT_PLAYPAUSE);
    clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 0 && find("reason=disarmed would=play", 0));
    rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();
    assert(disarm_why == 0 && find("ARM re-armed by the master row", 0));
    clear(); t += 2000; edge(&t, 1); drain();
    assert(ncall == 1);

    /* a slow facade call (> 2 ms) disarms after it returns */
    stub_delay_ns = 3000000; clear(); t += 2000; edge(&t, 1); my_tick(); drain();
    assert(ncall == 1 && disarm_why == D_SLOW_CALL && find("ARM state=DISARMED reason=facade-call-over-2ms", 0));
    stub_delay_ns = 0;
    rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();

    /* onMoved on another thread than EP147's facade calls: skip and disarm */
    { uint64_t keep = hook_bus_thr; hook_bus_thr = keep + 0x1000;
      clear(); t += 2000; edge(&t, 1); drain();
      assert(ncall == 0 && find("reason=thread-mismatch", 0) && disarm_why == D_THREAD);
      hook_bus_thr = keep; }
    rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();
    /* position out of range: disarm */
    { static const uint32_t bad[] = { 0x3F800001u /* >1.0 */, 0x3FC00000u /* 1.5 */, 0xBE800000u /* -0.25 */,
                                      0x7FC00000u /* NaN */, 0x7F800000u /* inf */, 0xFF800000u /* -inf */,
                                      0x80000001u /* -denormal */ };
      for (unsigned i = 0; i < sizeof bad / sizeof bad[0]; i++) {
          clear(); moved_bits(t += 10, 0, bad[i]); assert(disarm_why == D_POS_RANGE); at_ms(t += 10); my_tick(); drain();
          char want[80]; snprintf(want, sizeof want, "reason=fader-position-not-a-0..1-float pos=%#010x", bad[i]);
          assert(find(want, 0));
          rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();
          assert(disarm_why == 0);
      } }
    /* 1023 as an integer is the float 1.4e-42 (a denormal), so it reads as 0, not 1023: it must
     * not disarm (only > 1.0, < 0, NaN, inf do) - re-checked by pos_to_raw below */
    /* a press contradicting a fill-in: CONFLICT, disarm, fill-ins forgotten */
    clear();
    mem_set(0x7f00009000ull, VT_PLAYPAUSE);
    press(t += 100, 1, 0x0a, 1); at_ms(t += 1); my_play18((void *)0x7f00009000, 0, NULL, 0); drain();
    assert(find("LEARN play deck=2 obj=0x7f00009000 CONFLICT", 0) && disarm_why == D_OBJ_CONFLICT && play_src[1] == 0 && play_src[2] == 0);
    rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();
    /* re-learn deck 2 properly; fill-ins return */
    press(t += 100, 1, 0x0a, 1); at_ms(t += 1); my_play18((void *)(uintptr_t)(P1 + 0x620), 0, NULL, 0); drain();
    assert(play_src[1] == 1 && play_src[2] == 2 && disarm_why == 0);
    /* OnAirManager with a wrong vtable: disarm */
    { uint32_t pos[2] = { 0, 0 }; at_ms(t += 10); my_onmoved((void *)0x7f66660000, NULL, FADER_ID0, pos); }
    assert(disarm_why == D_ONAIR_VT);
    { uint32_t pos[2] = { 0, 0 }; my_onmoved((void *)(uintptr_t)MGR, NULL, FADER_ID0, pos); }
    rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();
    assert(disarm_why == 0);
    /* crossfader and other sliders: ignored */
    { uint32_t pos[2] = { 1023, 0 }; int c0 = ncall; my_onmoved((void *)(uintptr_t)MGR, NULL, FADER_ID0 + 4, pos);
      my_onmoved((void *)(uintptr_t)MGR, NULL, 0x123, pos); assert(ncall == c0); }

    /* mixer filter with the hook as source: edges are never acted on by the filter; edges that
     * onMoved never saw -> XCHK, two in a row disarm */
    clear(); t += 3000;
    mix(t, 0, 1023, 0, 1023); mix(t += 400, 300, 1023, 0, 1023);
    at_ms(t += 600); my_tick(); drain();
    assert(ncall == 0 && find("XCHK ch=1 mixer edge without an onMoved call (1 in a row)", 0));
    mix(t += 10, 0, 1023, 0, 1023); mix(t += 400, 300, 1023, 0, 1023); at_ms(t += 600); my_tick(); drain();
    assert(disarm_why == D_ONMOVED_SILENT && find("reason=onMoved-silent-on-fader-edges", 0));
    rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();
    /* an edge both saw: no XCHK */
    clear(); t += 2000;
    mix(t, 0, 1023, 0, 1023); moved(t, 0, 0); mix(t += 400, 300, 1023, 0, 1023); moved(t, 0, 300);
    at_ms(t += 600); my_tick(); drain();
    assert(count("XCHK") == 0 && ncall == 1);

    /* 0.0.6 (deck run 20261115-205017): a SLOW fader move the hook saw all along. onMoved keeps
     * firing past edge+500 ms, so its latest call is > 500 ms after the edge; 0.0.5 logged XCHK
     * and disarmed after two such moves. Now: seen, no XCHK, no disarm. */
    for (int rep = 0; rep < 3; rep++) {
        clear(); t += 2000; D_play[0] = 1;
        mix(t, 0, 1023, 0, 1023); moved(t, 0, 0);
        t += 400; mix(t, 70, 1023, 0, 1023); moved(t - 7, 0, 70);   /* the hook a few ms first, as on the deck */
        for (int k = 1; k <= 12; k++) { t += 103; moved(t, 0, 70 + 75 * (unsigned)k); mix(t, 70 + 75 * k, 1023, 0, 1023); }
        at_ms(t += 100); my_tick(); drain();
        assert(count("XCHK") == 0 && disarm_why == 0);
    }
    D_play[0] = 0;

    /* fallback: onMoved not claimed -> the mixer filter's edge acts (same gates) */
    claimed = 0; clear(); t += 2000;
    mix(t, 0, 1023, 0, 1023); mix(t += 400, 400, 1023, 0, 1023); drain();
    assert(ncall == 1 && find("ACT ch=1 src=filter action=play", 0));
    /* the mixer filter near switch-off while it is the source: disarm */
    for (int i = 0; i < 5; i++) filt_done(F_MIXER, fs_unit_now - 300000ull);
    at_ms(t += 10); my_tick(); drain();
    assert(disarm_why == D_MIXER_FILTER && find("reason=mixer-filter-near-switch-off", 0));
    claimed = 1u << H_ONMOVED;
    rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();

    /* first sight of a fader in the hook: the mixer filter's closed time arms it */
    clear(); hk_seen[2] = 0; hk_closed_since[2] = 0; hk_armed_open[2] = 0; D_loaded[2] = 1; rowval[h_exit] = 0;
    mem_set(MGR + ONAIR_IN_OFF, 0x0000000000010000ull);   /* +0x3ba = 1 */
    t += 2000; mix(t, 1023, 1023, 0, 1023); mix(t += 10, 1023, 1023, 0, 1023);
    moved(t += 400, 2, 700); drain();
    assert(ncall == 1 && call_obj[0] == P1 + 2 * 0x620 && find("ACT ch=3", 0));
    rowval[h_exit] = 1;

    /* ---- 0.0.5: press-free CueLoop facades from the memory scan ---- */
    { static const fs_region R[] = { { 0x7f4dc20000ull, 0x7f4dc40000ull }, { 0x7f60000000ull, 0x7f60004000ull } };
      const uint64_t C3a = 0x7f4dc28500ull, C3b = 0x7f4dc2a300ull, C4 = 0x7f4dc2c100ull, DECOY = 0x7f60001000ull;
      cue_obj_at(C3a, LS3, CORE3); cue_obj_at(C3b, LS3, CORE3);       /* two for deck 3: ambiguous */
      cue_obj_at(C4, LS4, CORE4);                                    /* one for deck 4 */
      cue_obj_at(DECOY, LS4, CORE1);                                 /* deck 4's LoopState, deck 1's core: no deck */
      mem_set(0x7f4dc2e000ull, VT_CUELOOP); mem_set(0x7f4dc2e008ull, VT_CUELOOP2);   /* third vtable missing: no hit */
      fs_unit_regions = R; fs_unit_nregions = 2;
      rowval[h_en] = 0; scan_step(); assert(sc_state == 0 && sc_runs == 0);   /* master off: no scan */
      rowval[h_en] = 1;
      clear(); at_ms(t += 10);
      scan_step(); assert(sc_state == 1 && sc_runs == 1 && sc_nreg == 2);
      for (int k = 0; k < 8 && sc_state; k++) scan_step();
      drain();
      assert(sc_state == 0 && find("SCAN cueloop start: regions=2 bytes=147456", 0));
      { const char *s = find("SCAN cueloop done:", 0); assert(s && strstr(s, "vtable_hits=6 matched=1/1/2/1")); }
      assert(cue_src[0] == 1 && cue_src[1] == 1 && cue_src[2] == 0 && cue_src[3] == 3 && cue_obj[3] == C4);
      assert(find("SCAN cue deck=3 obj=0x7f4dc2a300 ambiguous", 0) && find("SCAN cue deck=4 obj=0x7f4dc2c100 ok", 0));
      assert(!find("SCAN cue deck=1", 0) && !find("SCAN cue deck=2", 0));   /* same objects as the presses */
      /* retries only after ~30 s of rounds */
      scan_step(); assert(sc_state == 0 && sc_wait == SCAN_RETRY_ROUNDS - 1);
      /* deck 4 paused with a registered loop, exit-loop on: loopExit on the scanned facade + play */
      mem_set(MGR + ONAIR_IN_OFF, 0x0000000001010000ull);           /* ch3 and ch4 input = deck */
      D_loaded[3] = 1; set_loop(LS4, 1);
      clear(); t += 2000; edge(&t, 4); drain();
      assert(ncall == 2 && !strcmp(calls[0], "loopExit") && call_obj[0] == C4 && !strcmp(calls[1], "play") &&
             call_obj[1] == P1 + 3 * 0x620 && find("cue_obj=0x7f4dc2c100(S)", 0) && find("loop=1(hasloop)", 0));
      /* deck 3: ambiguous = no CueLoop facade -> paused fallback play */
      D_loaded[2] = 1; set_loop(LS3, 1); clear(); t += 2000; edge(&t, 3); drain();
      assert(ncall == 1 && !strcmp(calls[0], "play") && find("ACT ch=3 src=hook action=play reason=fallback-no-cue-obj", 0));
      set_loop(LS3, 0); set_loop(LS4, 0);
      /* a press contradicting a scanned object: CONFLICT + disarm (a press always wins) */
      mem_set(0x7f00008000ull, VT_CUELOOP);
      erp_sel[1] = 4; press(t += 100, 1, 0x09, 3); at_ms(t += 1); my_loopexit((void *)0x7f00008000); drain();
      assert(find("LEARN cue deck=4 obj=0x7f00008000 CONFLICT", 0) && disarm_why == D_OBJ_CONFLICT && cue_src[3] == 0);
      erp_sel[1] = 2;
      rowval[h_en] = 0; at_ms(t += 10); my_tick(); rowval[h_en] = 1; at_ms(t += 10); my_tick(); drain();
      assert(disarm_why == 0); }

    /* never reloop: across every scenario above, the only reloop calls were the pass-through
     * of EP147's own one; the mod called only play / loopExit */
    assert(n_orig_reloop == 1);
    for (int i = 0; i < ncall; i++) assert(!strcmp(calls[i], "play") || !strcmp(calls[i], "loopExit"));

    /* ---- decode, LED rule, rate limit, logging gate (carried over from 0.0.2) ---- */
    mix(t += 500, 1, 2, 3, 1022);
    assert(fad(mx_fad, 0) == 1 && fad(mx_fad, 1) == 2 && fad(mx_fad, 2) == 3 && fad(mx_fad, 3) == 1022);
    tl_state[1][L_IN] = 2; tl_state[1][L_OUT] = 2; tl_state[1][L_RELOOP] = 0; assert(side_loop(1) == 0);
    tl_state[1][L_RELOOP] = 1; assert(side_loop(1) == 1);
    drain(); clear();
    for (int i = 0; i < 100; i++) { fs_rec r = { .t = fs_unit_now, .type = R_SETTLE, .a = 1, .b = i }; push(&r); }
    drain();
    assert(count("FADER_SETTLE") == 16);
    at_ms(t += 1000); { fs_rec r = { .t = fs_unit_now, .type = R_SETTLE, .a = 1, .b = 1 }; push(&r); } drain();
    assert(count("RATE dropped=84 prio_dropped=0") == 1);
    clear(); rowval[h_log] = 0;
    { fs_rec r = { .t = fs_unit_now, .type = R_SETTLE, .a = 1, .b = 1 }; push(&r); } drain();
    assert(nlog == 0 && w_gated >= 1);
    rowval[h_log] = 1;
    at_ms(t += 11000); my_tick(); drain();
    assert(count("FILTERS frames mixer=") == 1 && find("onMoved_calls=", 0));
    /* onMoved position = float 0..1.0: the exact AZ-EMU fs003a bit patterns */
    { uint32_t r = 77;
      assert(pos_to_raw(1056972808u, &r) == 0 && r == 512);    /* 0x3F002008 = 512/1023 */
      assert(pos_to_raw(1065304016u, &r) == 0 && r == 1020);   /* 0x3F7F3FD0 = 1020/1023 */
      assert(pos_to_raw(0x3F800000u, &r) == 0 && r == 1023);
      assert(pos_to_raw(0x00000000u, &r) == 0 && r == 0);
      assert(pos_to_raw(0x80000000u, &r) == 0 && r == 0);      /* -0 */
      r = 77; assert(pos_to_raw(1023u, &r) == 0 && r == 0);    /* integer 1023 = denormal float */
      assert(pos_to_raw(0x3F800001u, &r) < 0 && pos_to_raw(0x7FC00000u, &r) < 0 && pos_to_raw(0x7F800000u, &r) < 0);
      assert(pos_to_raw(0xBF800000u, &r) < 0 && pos_to_raw(0x80000001u, &r) < 0 && pos_to_raw(0x40000000u, &r) < 0);
      for (uint32_t k = 0; k <= 1023; k++) {                   /* every slider step round-trips */
          assert(pos_to_raw(fbits((float)k / 1023.0f), &r) == 0 && r == k);
      }
      for (uint32_t b = 0; b <= 0x3F800000u; b += 4099u) {     /* vs lround over the whole 0..1.0 range */
          float v; memcpy(&v, &b, 4);
          long want = lround((double)v * 1023.0);
          assert(pos_to_raw(b, &r) == 0 && (long)r == want);
      } }
    /* the logged patterns drive a real edge: closed (0.0) for 400 ms, 512/1023, 1020/1023 -> play */
    { clear(); t += 3000; D_play[0] = 0; rowval[h_exit] = 0;
      hk_seen[0] = 0; hk_closed_since[0] = 0; hk_armed_open[0] = 0;
      moved_bits(t, 0, 0x00000000u);
      moved_bits(t += 400, 0, 1056972808u);
      drain();
      assert(disarm_why == 0 && find("ACT ch=1", 0) && find("v=512", 0));
      moved_bits(t += 50, 0, 1065304016u); drain();
      assert(disarm_why == 0 && hk_raw[0] == 1020); }

    /* ---- 0.0.6: learning without a DECK SELECT press (deck run 20261115-205017) ---- */
    { const char *s;
      claimed = 1u << H_ONMOVED;
      #define FORGET() do { for (int k = 0; k < 4; k++) { play_src[k] = 0; play_obj[k] = 0; deckev_t[k] = 0; } \
                            erp_sel[0] = erp_sel[1] = 0; lp_on = 0; } while (0)
      /* side A Play press, play18 + playop on deck 1's facade, EP147's PLAY event for deck 1 18 ms later */
      FORGET(); clear(); t += 5000;
      press(t, 0, 0x0a, 1);
      at_ms(t += 1); my_play18((void *)(uintptr_t)P1, 0, (void *)0x7f7e7fac70, 0);
      at_ms(t + 2); my_playop((void *)(uintptr_t)(P1 + 8), 0, 0, 0);
      drain();
      assert(play_src[0] == 0 && lp_on && count("side-deck-unknown") == 0);   /* held, not yet known */
      at_ms(t += 18); deck_event(0, AZ_DECK_PLAY, NULL); drain();
      assert(!lp_on && erp_sel[0] == 1 && erp_sel[1] == 0);
      assert(find("DECK_SELECT side=A deck=1 (from EP147's PLAY/STOP deck event)", 0));
      assert(find("LEARN play deck=1 obj=0x7f8a34cc20 ok", 0) && find("FILL play deck=2 obj=0x7f8a34d240 ok", 0) &&
             find("FILL play deck=3 obj=0x7f8a34d860 ok", 0) && find("FILL play deck=4 obj=0x7f8a34de80 ok", 0));
      assert(play_src[0] == 1 && play_src[1] == 2 && play_src[2] == 2 && play_src[3] == 2 && count("LEARN play") == 1);
      /* ... and a fader edge on channel 1 now plays deck 1 (paused, no loop) */
      rowval[h_exit] = 1; disarm_why = 0; D_play[0] = 0; set_loop(LS1, 0);
      clear(); t += 3000; edge(&t, 1); drain();
      assert(ncall == 1 && !strcmp(calls[0], "play") && call_obj[0] == P1 && find("ACT ch=1 src=hook action=play reason=ok", 0));

      /* side B showing deck 4: its PLAY event names deck 4 */
      FORGET(); clear(); t += 5000;
      press(t, 1, 0x0a, 1);
      at_ms(t += 1); my_play18((void *)(uintptr_t)(P1 + 3 * 0x620), 0, (void *)0x7f7e7fac70, 0); drain();
      at_ms(t += 30); deck_event(3, AZ_DECK_PLAY, NULL); drain();
      assert(erp_sel[1] == 4 && find("DECK_SELECT side=B deck=4 (from", 0) && find("LEARN play deck=4 obj=0x7f8a34de80 ok", 0) &&
             find("FILL play deck=1 obj=0x7f8a34cc20 ok", 0));

      /* no deck event (or only one for the other side's deck): not learned, logged after the window */
      FORGET(); clear(); t += 5000;
      press(t, 0, 0x0a, 1);
      at_ms(t += 1); my_play18((void *)(uintptr_t)P1, 0, (void *)0x7f7e7fac70, 0); drain();
      at_ms(t + 20); deck_event(1, AZ_DECK_PLAY, NULL); drain();          /* deck 2 = side B: ignored */
      at_ms(t + 300); drain(); assert(lp_on && play_src[0] == 0);
      at_ms(t += 600); drain();
      assert(!lp_on && play_src[0] == 0 && erp_sel[0] == 0 && (s = find("LEARN play deck=0 obj=0x7f8a34cc20 side-deck-unknown", 0)) != NULL);
      /* a stale deck event from before the press does not count */
      FORGET(); clear(); t += 5000;
      at_ms(t); deck_event(0, AZ_DECK_STOP, NULL);
      press(t += 200, 0, 0x0a, 1);
      at_ms(t += 1); my_play18((void *)(uintptr_t)P1, 0, (void *)0x7f7e7fac70, 0); drain();
      at_ms(t += 600); drain();
      assert(play_src[0] == 0 && find("side-deck-unknown", 0));
      /* both decks of the side reported in the window: ambiguous, not learned */
      FORGET(); clear(); t += 5000;
      press(t, 0, 0x0a, 1);
      at_ms(t += 1); my_play18((void *)(uintptr_t)P1, 0, (void *)0x7f7e7fac70, 0);
      at_ms(t + 10); deck_event(0, AZ_DECK_PLAY, NULL); deck_event(2, AZ_DECK_STOP, NULL); drain();
      assert(play_src[0] == 0 && play_src[2] == 0 && find("ambiguous", 0));
      /* a DECK SELECT press meanwhile for the other deck of the side wins: not learned from the event */
      FORGET(); clear(); t += 5000;
      press(t, 0, 0x0a, 1);
      at_ms(t += 1); my_play18((void *)(uintptr_t)P1, 0, (void *)0x7f7e7fac70, 0); drain();
      press(t += 5, 0, 0x09, 0);                                             /* side A -> deck 3 */
      at_ms(t += 10); deck_event(0, AZ_DECK_PLAY, NULL); drain();
      assert(erp_sel[0] == 3 && play_src[0] == 0 && play_src[2] == 0 && find("ambiguous", 0));
      /* a known selection still learns at once, as in 0.0.5 */
      FORGET(); clear(); t += 5000;
      press(t, 0, 0x08, 0); press(t += 100, 0, 0x0a, 1);
      at_ms(t += 1); my_play18((void *)(uintptr_t)P1, 0, (void *)0x7f7e7fac70, 0); drain();
      assert(play_src[0] == 1 && !lp_on && find("LEARN play deck=1 obj=0x7f8a34cc20 ok", 0) && count("(from EP147") == 0);
      #undef FORGET
    }
    /* LOGGATE: lines dropped while the Logging row read off are counted once logging is back */
    { clear(); rowval[h_log] = 0; at_ms(t += 100); deck_event(0, AZ_DECK_PLAY, NULL);
      press(t += 10, 0, 0x0a, 1); drain();
      rowval[h_log] = 1; press(t += 10, 0, 0x0a, 1); drain();
      assert(find("LOGGATE ", 0) && count("LOGGATE") == 1); }

    printf("unit: all fader-start checks passed (decision gates, never-reloop, learning, actions, disarm/re-arm, fallback, float fader positions incl. AZ-EMU bit patterns; 0.0.5: has_loop via Play facade, LoopState/core cross-check, paused fallbacks, CueLoop scan, LOOPDIAG; 0.0.6: learning from the PLAY/STOP deck event without DECK SELECT, slow-move XCHK, LOGGATE)\n");
    return 0;
}
