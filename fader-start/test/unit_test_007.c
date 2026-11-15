/* TEST-ONLY: host unit test for the fader-start 0.0.7 changes (shipped config FS_SCAN=0):
 * CueLoop learning from the LOOP press (fn_1436d38, this-0xd0) and from loopExit/reloop by
 * LoopState/core match, our own calls never teach, the Play facade block-of-four check, scan off,
 * act_max_us.  gcc -std=gnu11 -g -O1 -fsanitize=address,undefined -I<sdk>/include test/unit_test_007.c */
#define FS_UNIT 1
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

static fs_hev ev(unsigned kind, uint64_t self, uint64_t lr, uint64_t ms)
{
    fs_hev e; memset(&e, 0, sizeof e);
    e.kind = kind; e.self = self; e.lr = lr; e.thr = thread_ptr(); at_ms(ms); e.t = counter();
    return e;
}
static void reset_objs(void)
{
    for (int d = 0; d < 4; d++) { play_obj[d] = cue_obj[d] = 0; play_src[d] = cue_src[d] = 0; }
}
int main(void)
{
    CORE.abi = AZ_MOD_ABI; CORE.size = sizeof CORE; CORE.log = f_log; CORE.fw_tag = f_tag;
    CORE.thread_start = f_thread; CORE.frame_filter = f_filter; CORE.claim_hook = f_claim;
    CORE.register_row = f_reg_row; CORE.row_value = f_row_value; CORE.register_screen = f_reg_screen;
    CORE.on_deck_event = f_deck_ev; CORE.deck_info = f_deck_info; CORE.banner = f_banner;
    fs_unit_read = mem_read; fs_unit_play10 = stub_play10; fs_unit_loopexit = stub_loopexit_call;
    orig_reloop = o_reloop; orig_loopexit = o_loopexit; orig_play18 = o_play18; orig_playop = o_playop; orig_onmoved = o_onmoved;
    at_ms(0);
    assert(init(&CORE) == 0 && FS_SCAN == 0);
    assert(count("loaded 0.0.7 ACTUATING") == 1);
    drain(); clear();

    mem_set(VA_PLAY10, PLAY10_WORD0 | 0x910003fd00000000ull);
    uint64_t ls[4] = { LS1, LS2, LS3, LS4 }, core[4] = { CORE1, CORE2, CORE3, CORE4 };
    for (int d = 0; d < 4; d++) {
        uint64_t p = P1 + 0x620u * (unsigned)d;
        mem_set(p, VT_PLAYPAUSE); mem_set(p + PLAY_LS_OFF, ls[d]); mem_set(p + PLAY_CORE_OFF, core[d]);
        set_loop(ls[d], 0);
    }
    cue_obj_at(CUE1, LS1, CORE1); cue_obj_at(CUE2, LS2, CORE2);
    mem_set(MGR, VT_ONAIRMGR); mem_set(MGR + ONAIR_IN_OFF, 0);
    claimed = 1u << H_ONMOVED;
    rowval[h_en] = 1;
    uint64_t t = 3000;

    /* block-of-four: deck 3's facade can not be learned as deck 1 (its +0x1240/+0x1860 slots are no
     * PlayPause facades), and a block with a missing slot is refused; nothing is disarmed */
    learn_deck(0, 0, P1 + 2 * 0x620, counter()); drain();
    assert(play_src[0] == 0 && play_obj[0] == 0 && find("LEARN play deck=1 obj=0x7f8a34d860 block-mismatch", 0));
    mem_set(P1 + 3 * 0x620, 0);
    learn_deck(0, 1, P1 + 0x620, counter()); drain();
    assert(play_src[1] == 0 && find("LEARN play deck=2 obj=0x7f8a34d240 block-mismatch", 0));
    mem_set(P1 + 3 * 0x620, VT_PLAYPAUSE);
    assert(disarm_why == 0);
    /* the right index learns and fills all four */
    clear(); learn_deck(0, 0, P1, counter()); drain();
    assert(play_src[0] == 1 && play_src[1] == 2 && play_src[2] == 2 && play_src[3] == 2 && find("FILL play deck=4", 0));

    /* scan compiled out: never starts, even with the master on and every CueLoop unknown */
    for (int k = 0; k < 2000; k++) scan_step();
    assert(sc_state == 0 && sc_runs == 0 && !find("SCAN cueloop", 0));

    /* LOOP press on deck 2, no RELOOP press, side B's deck unknown (erp_sel 0): fn_1436d38's play
     * op on CueLoop+0xd0 teaches deck 2's CueLoop facade from the LoopState/core match */
    assert(erp_sel[1] == 0);
    clear();
    { fs_hev e = ev(H_PLAYOP, CUE2 + LOOPOP_CUE_OFF, 0x1436d7cu, t += 100); learn(&e); } drain();
    assert(cue_obj[1] == CUE2 && cue_src[1] == 1 && cue_obj[0] == 0 && find("LEARN cue deck=2 obj=0x7f4dc26700 ok", 0));
    assert(!strcmp(playop_via(0x1436d7cu), "fn_1436d38"));
    /* the same press again: known, quiet */
    clear(); { fs_hev e = ev(H_PLAYOP, CUE2 + LOOPOP_CUE_OFF, 0x1436d7cu, t += 100); learn(&e); } drain();
    assert(!find("LEARN", 0) && disarm_why == 0);

    /* a CueLoop-looking object whose LoopState matches no Play facade: not learned */
    cue_obj_at(0x7f4dc28500ull, 0x7f00009000ull, CORE3);
    clear(); { fs_hev e = ev(H_PLAYOP, 0x7f4dc28500ull + LOOPOP_CUE_OFF, 0x1436d7cu, t += 100); learn(&e); } drain();
    assert(cue_obj[2] == 0 && !find("LEARN cue", 0));
    /* the right LoopState but a wrong second vtable: not learned */
    cue_obj_at(0x7f4dc2a300ull, LS3, CORE3); mem_set(0x7f4dc2a300ull + 8, 0x1234);
    clear(); { fs_hev e = ev(H_PLAYOP, 0x7f4dc2a300ull + LOOPOP_CUE_OFF, 0x1436d7cu, t += 100); learn(&e); } drain();
    assert(cue_obj[2] == 0 && !find("LEARN cue", 0));

    /* our own loopExit call (return address in this .so) teaches nothing, even with a RELOOP press */
    fs_unit_lr_any = 0;
    press(t += 100, 0, 0x09, 3);
    clear(); { fs_hev e = ev(H_LOOPEXIT, CUE1, 0x7f9667dd94ull, t += 1); learn(&e); } drain();
    assert(cue_obj[0] == 0 && !find("LEARN cue", 0));
    /* EP147's own loopExit on deck 1's facade while the RELOOP press was on side B with deck 2
     * shown: the deck comes from the LoopState, so deck 1 is learned and nothing conflicts */
    erp_sel[1] = 2;
    press(t += 100, 1, 0x09, 3);
    clear(); { fs_hev e = ev(H_LOOPEXIT, CUE1, 0x23604f8u, t += 1); learn(&e); } drain();
    assert(cue_obj[0] == CUE1 && cue_obj[1] == CUE2 && disarm_why == 0 && find("LEARN cue deck=1 obj=0x7f4dc24900 ok", 0));
    fs_unit_lr_any = 1;

    /* deck 2 paused in a registered loop: fader up = loopExit on the LOOP-learned facade, then play */
    hook_bus_thr = thread_ptr();
    set_loop(LS2, 1); D_play[1] = 0;
    stub_delay_ns = 40000;                   /* the play call takes 40 us */
    clear(); t += 2000; edge(&t, 2); drain();
    assert(ncall == 2 && !strcmp(calls[0], "loopExit") && call_obj[0] == CUE2 && !strcmp(calls[1], "play") &&
           call_obj[1] == P1 + 0x620 && find("ACT ch=2 src=hook action=exit+play reason=ok", 0));
    for (int i = 0; i < ncall; i++) assert(strcmp(calls[i], "reloop") != 0);
    assert(act_max_us >= 40 && n_orig_reloop == 0);
    /* deck 2 playing in the loop: exit only */
    D_play[1] = 1;
    clear(); t += 2000; edge(&t, 2); drain();
    assert(ncall == 1 && !strcmp(calls[0], "loopExit") && find("action=exit reason=ok", 0));
    stub_delay_ns = 0;
    /* FILTERS line carries act_max_us */
    clear(); for (int k = 0; k < 3; k++) { at_ms(t += 11000); my_tick(); mix(t, 0, 0, 0, 0); mix(t + 5, 40, 0, 0, 0); at_ms(t += 11000); my_tick(); }
    drain();
    { const char *s = find("FILTERS frames", 0); assert(s && strstr(s, "act_max_us=")); }

    /* near= now names the 4-beat button */
    { char b[96]; press(t += 100, 1, 0x0c, 2); near_press(counter(), b, sizeof b); assert(strstr(b, "B:4beat")); }

    reset_objs();
    (void)dec; (void)base_in; (void)moved_bits; (void)LF;
    printf("unit_007: all fader-start 0.0.7 checks passed (LOOP-press CueLoop learning, LoopState-matched loopExit learning, own calls ignored, block-of-four, scan off, act_max_us, 4beat near)\n");
    return 0;
}
