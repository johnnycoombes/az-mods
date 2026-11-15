/* SPDX-License-Identifier: GPL-3.0-only */
/* fader-start 0.0.7: channel-fader start for the XDJ-AZ. ACTUATING, OFF BY DEFAULT.
 *
 * Raising a closed channel fader (closed <= 16/1023 for >= 250 ms, then >= 64/1023) starts
 * the deck on that channel (channel n = deck n) from where it stands, exactly as EP147's
 * own CDJ-mode PLAY does. If the deck plays a loop and "Exit loop on fader up" is on (the
 * default), the loop is exited first (loopExit), and play is called only if the deck is
 * paused. Reloop is never called. Master row "Fader Start" is off by default: then every
 * fader edge is still logged with what it would have done.
 *
 * Evidence (deck runs 1 and 2 with 0.0.1/0.0.2, static analysis of EP147_bootb_8daa1de5):
 *   - EP147 calls the facades on its bus thread (the thread the frame filters run on).
 *     PlayPauseControlFacade::play(start, slip) (+0x10, 0x14415a8) and
 *     CueLoopControlFacade::loopExit() (0x142b198) only post an async task to the player
 *     (dj_player AsyncTaskBox); loopExit's task does nothing when there is no loop
 *     (has_loop 0x149f090 first).
 *   - jog mode picks the slot: one mode calls +0x18 (needs a JogState that EP147 builds on
 *     its stack per call), the other +0x10 with start=0 slip=0: this mod calls +0x10 only.
 *   - OnAirManager::onMoved(IHuiSlider&, HuiComponentId, const HuiSliderPosition&) 0x10aba58
 *     sees every channel-fader move; ids 0x02b1ffa6b4312d00..03 = channel 1..4 faders, and
 *     The position's first 32-bit word is a float 0..1.0 (0.0.3 read it as a u32 raw and
 *     disarmed; AZ-EMU fs003a logged 0x3F002008 = 512/1023 and 0x3F7F3FD0 = 1020/1023).
 *     EP147 itself only copies that word (task 0x10aa6e0 -> OnAirManager +0x380 vector) and
 *     never reads the next word, so neither do we. raw = round(value * 1023), integer-only
 *     (no FP in the hook path); NaN, inf, < 0 or > 1.0 = disarm. A hook is never timed or switched off
 *     by the core, unlike a frame filter (deck run 2: LED filter off after 31 s, mixer 6/8).
 *   - OnAirManager +0x3ba/+0x3bb = channel 3/4 input flag; EP147 only puts channel 3/4 on
 *     air when it is 1 (0x10ad3e8), so 1 is taken as "DECK" input.
 *   - Loop state (0.0.5; 0.0.4's "LoopState +8 = looping" was wrong: AZ-EMU fs004a read 0 in a
 *     visible loop and 1 after EXIT+CUE). +8 is a cue-play flag: set to 1 only by the hot-cue
 *     call task (0x1425f08) and ActiveLoop engage (0x14b3078), cleared by every play/pause/
 *     reloop/retrigger task (0x1441410, 0x1441498, 0x1422944, ...). What loopExit itself needs
 *     is "a loop is registered": its task 0x14299d8 returns at once unless has_loop 0x149f090
 *     (CueProperty at LoopState+0x160 valid, 0x14a7668: +0x30 != 0, +0x34 & 0x1f, +0x36 != 0,
 *     +0x38 != 0, +0x40 != INT64_MAX) is true. So loop = has_loop, refined by the LED rule
 *     (IN/OUT blinking) while the LED filter runs; loopExit on a registered but inactive loop
 *     keeps the deck where it is (fs004a t=173915: exit+play played on).
 *   - LoopState* is reachable from the Play facade: every facade of a deck is built by
 *     0x1565b88 on one base (0x155f5e8) whose +0x58 is the deck's LoopState and +0x10 the deck
 *     core+0x100; PlayPause base = facade+8 (LoopState +0x60, core +0x18), CueLoop base =
 *     facade+0x10 (LoopState +0x68 = what loopExit reads at 0x14299f0, core +0x20). So the loop
 *     state needs no RELOOP press, and a CueLoop facade must share both pointers with its deck's
 *     Play facade (checked before every call).
 *   - The CueLoop facades are found without a press: a one-off, read-only, rate-limited scan
 *     of EP147's private anonymous memory for the CueLoop vtable triple (0x2bc8128/0x2bc8198/
 *     0x2bc81c0, ctor 0x1565ce8) whose LoopState and core match a known Play facade.
 *   - Play facades of decks 1..4 sit 0x620 apart (deck run 2: ..cc20/d240/d860/de80).
 *   - vtables: PlayPause 0x2bce368, CueLoop 0x2bc8128, OnAirManager 0x2b68960 (non-PIE,
 *     each slot target appears in exactly one vtable), checked before every call.
 *
 * Safety design:
 *   - edges come from a pass-through hook on onMoved (EP147 first, then us, same thread);
 *     the mixer frame filter decodes the same edges and acts only when the onMoved hook could
 *     not be claimed (the filter can only act while the core still calls it);
 *   - the calls are made on the bus thread only: the hook's thread must equal the thread on
 *     which EP147 itself called play18/loopExit (or, until one was seen, the ERP filter's);
 *   - objects are learned from physical presses (PLAY -> play18 / playop this-8; RELOOP/EXIT
 *     -> loopExit / reloop) on the side whose deck selection is known; the other Play
 *     facades are filled in from the 0x620 spacing; every object's vtable pointer is read
 *     with process_vm_readv and compared before every call; unknown or invalid = skip + log;
 *   - gates: master row, a disarm latch (any failed health check; cleared by switching the
 *     master off and on), automix's hook on 0x14415a8 (first word changed = refuse), bus
 *     thread, 2 s after mod load and after a track load, deck loaded, channel 3/4 input
 *     flag, play state known, no PLAY press on that deck in the last 500 ms, loop state from
 *     LoopState has_loop (via the Play facade), refined by the LED loop rule while the LED
 *     filter is alive and that deck is the side's selected one; a loop to exit needs a CueLoop
 *     facade; when the loop state is unknown or there is no CueLoop facade, a PAUSED deck is
 *     still played (logged as reason=fallback-...), a playing one is left alone; one action per
 *     edge and 1 s per channel;
 *   - health checks that disarm everything: an object failing its vtable check, a facade call
 *     over 2 ms, a learned object contradicting the 0x620 spacing, a CueLoop facade whose
 *     LoopState/core differ from its deck's Play facade, onMoved on another thread
 *     than EP147's facade calls, an onMoved position that is not a finite 0..1.0 float, an OnAirManager vtable
 *     mismatch, onMoved silent for two filter-seen edges, the mixer filter near switch-off
 *     while it is the edge source;
 *   - no conflicts: nothing within 16 bytes of active-loop, automix or tempo-range hooks or
 *     the core's own (mixch 0x0abdd18/0x0abec10/0x0abf648); 0x14415a8 is called, not hooked.
 *
 * 0.0.7 (deck run D:\MODS\logs\20261115-212912, 0.0.6):
 *   - the CueLoop memory scan found nothing on the real deck (3 runs, regions capped at 384,
 *     3 GiB read, vtable_hits=0, ~45 s each) and every FILTER_SLOW line (LED filter switched off
 *     at 66.7 s, mixer filter at 208.8 s) fell inside a scan run: the scan is now compiled out
 *     unless FS_SCAN=1;
 *   - instead the CueLoop facade is learned from EP147's own LOOP (4-beat) press: fn_1436d38
 *     calls the play op with this = CueLoop facade + 0xd0 (deck 0x7f555b59d0/0x7f555b77d0 =
 *     learned 0x7f555b5900/0x7f555b7700 + 0xd0; AZ-EMU fs006a the same), and from loopExit/
 *     reloop. The deck is the one whose Play facade shares the facade's LoopState and core
 *     (no press-side or DECK SELECT needed); the vtable triple is checked first;
 *   - hook events whose return address is not in EP147's text (our own loopExit calls) never
 *     teach objects;
 *   - a learned Play facade must sit in a full block of four (all four 0x620 slots pass the
 *     vtable check for the deck index it is learned as), else it is not learned: a deck-3
 *     facade can no longer be taken for deck 1 from a stray deck event;
 *   - FILTERS lines carry act_max_us (the longest fs_act in the onMoved hook).
 *
 * Firmware: ep147-9e9454e357389ece (2.00/2.01) only. VA = file offset + 0x400000.
 * Hooks (all inline claim_hook, record-only, pass-through):
 *   0x1443560 play +0x18, 0x142b198 loopExit, 0x1422eb8 reloop (never called by us),
 *   0x147cd50 internal play op (its return address tells the slot), 0x10aba58 onMoved,
 *   0x10ac848 onAirStateChanged (row, default off).
 */
#define _GNU_SOURCE
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include "azmod-mod.h"

#define MOD_ID      "fader-start"
#define MOD_VERSION "0.0.7"
#define FW_TAG      "ep147-9e9454e357389ece"

/* EP147 addresses and layouts (see the header comment) */
#define VA_PLAY10     0x14415a8u   /* PlayPauseControlFacade::play(start, slip): CALLED, never hooked */
#define VA_PLAY18     0x1443560u
#define VA_LOOPEXIT   0x142b198u   /* CueLoopControlFacade::loopExit(): called */
#define VA_RELOOP     0x1422eb8u   /* hooked record-only; never called */
#define VA_PLAYOP     0x147cd50u
#define VA_ONMOVED    0x10aba58u
#define VA_ONAIR      0x10ac848u
#define VT_PLAYPAUSE  0x2bce368u
#define VT_CUELOOP    0x2bc8128u
#define VT_ONAIRMGR   0x2b68960u
#define PLAY10_WORD0  0xa9b27bfdu  /* stp x29,x30,[sp,#-0xe0]! : anything else = hooked (automix) */
#define PLAY_STRIDE   0x620
#define VT_CUELOOP2   0x2bc8198u   /* CueLoop vtables at +8 / +0x10 (ctor 0x1565ce8/0x1565cf4) */
#define VT_CUELOOP3   0x2bc81c0u
#define CUE_LS_OFF    0x68u        /* CueLoop facade -> LoopState* (loopExit task 0x14299f0) */
#define CUE_CORE_OFF  0x20u        /* CueLoop facade -> deck core+0x100 (base 0x155f5e8 +0x10) */
#define PLAY_LS_OFF   0x60u        /* PlayPause facade -> the same LoopState* (play task 0x144146c) */
#define PLAY_CORE_OFF 0x18u        /* PlayPause facade -> the same core+0x100 */
#define LS_CUEPLAY    0x8u         /* LoopState +8: cue-play flag (NOT looping; diagnostics only) */
#define LS_LOOPCUE    0x160u       /* LoopState +0x160: the registered loop's CueProperty */
#define ONAIR_IN_OFF  0x3b8u       /* u64 holding +0x3ba (ch3) and +0x3bb (ch4) */
#define FADER_ID0     0x02b1ffa6b4312d00ull
#define LOOPOP_CUE_OFF 0xd0u       /* fn_1436d38 (LOOP press) calls the play op on CueLoop facade + 0xd0 */
#ifndef FS_SCAN
#define FS_SCAN 0                  /* 0.0.7: the CueLoop memory scan is off (deck run 20261115-212912) */
#endif

static const az_core_v1 *C;

/* ---- clock ------------------------------------------------------------------------------ */
#ifdef FS_UNIT
static uint64_t fs_unit_now;                        /* TEST-ONLY: driven by test/unit_test.c */
static uint64_t counter(void) { return __atomic_load_n(&fs_unit_now, __ATOMIC_RELAXED); }
static uint64_t frequency(void) { return 1000000000ull; }
#elif defined(__aarch64__) && !defined(AZ_HARNESS)
static uint64_t counter(void) { uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v; }
static uint64_t frequency(void) { uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }
#else
static uint64_t counter(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}
static uint64_t frequency(void) { return 1000000000ull; }
#endif
#if defined(__aarch64__) && !defined(AZ_HARNESS) && !defined(FS_UNIT)
static inline uint64_t thread_ptr(void) { uint64_t v; __asm__ volatile("mrs %0, tpidr_el0" : "=r"(v)); return v; }
#else
#include <pthread.h>
static inline uint64_t thread_ptr(void) { return (uint64_t)(uintptr_t)pthread_self(); }
#endif
static long kernel_tid(void) { return (long)syscall(SYS_gettid); }   /* never from a hook */
static uint64_t hz = 1000000000ull, t0;
static uint64_t ms_to_ticks(uint64_t ms) { return ms * (hz / 1000u); }
static uint64_t ticks_to_us(uint64_t t) { return hz ? t / (hz / 1000000u ? hz / 1000000u : 1u) : 0; }
static long rel_ms(uint64_t t) { return t ? (long)((int64_t)(t - t0) / (int64_t)(hz / 1000u)) : -1; }

/* ---- reading EP147 memory without faulting --------------------------------------------- */
#if defined(FS_UNIT)
static int (*fs_unit_read)(uintptr_t a, uint64_t *v);         /* TEST-ONLY fake memory */
static int read_u64(uintptr_t a, uint64_t *v) { return fs_unit_read ? fs_unit_read(a, v) : -1; }
static int vm_ok = 1;
static void probe_vm(void) {}
#elif defined(AZ_HARNESS)
static int read_u64(uintptr_t a, uint64_t *v) { (void)a; (void)v; return -1; }   /* no EP147 */
static int vm_ok;
static void probe_vm(void) {}
#else
static int vm_ok;
static long my_pid;
static int read_u64(uintptr_t a, uint64_t *v)
{
    if (!vm_ok || a < 0x10000u || a >= ((uintptr_t)1 << 48) || (a & 7u)) return -1;
    struct iovec l = { v, 8 }, r = { (void *)a, 8 };
    return syscall(SYS_process_vm_readv, my_pid, &l, 1L, &r, 1L, 0L) == 8 ? 0 : -1;
}
static void probe_vm(void)
{
    static volatile uint64_t probe = 0x5a5a5a5a5a5a5a5aull;
    uint64_t v = 0;
    my_pid = (long)getpid();
    vm_ok = 1;
    if (read_u64((uintptr_t)&probe, &v) != 0 || v != probe) vm_ok = 0;
}
#endif
/* the object's vtable pointer is the one EP147 uses for that class: 1 ok, -1 bad, 0 none */
static int obj_check(uint64_t obj, uint64_t vt)
{
    uint64_t v = 0;
    if (!obj) return 0;
    return read_u64((uintptr_t)obj, &v) == 0 && v == vt ? 1 : -1;
}

/* ---- record queue (tick, filters, deck events -> worker) --------------------------------
 * MPSC bounded slots, one CAS attempt, a busy slot is a counted loss (automix queue.h). */
enum { R_FADER = 1, R_SETTLE, R_FIRE, R_RATE, R_LED, R_SEL, R_PRESS, R_DECK, R_HOOK, R_NOTE, R_THREAD,
       R_SLOW, R_SILENT, R_STATUS, R_LEARN, R_ARM, R_XCHK, R_INIT, R_SCAN };
typedef struct {
    uint64_t t, x[8];
    int type, a, b, c, d, e, f, g;
} fs_rec;
typedef struct { unsigned state; fs_rec r; } fs_slot;
#define QN 256u
typedef struct { fs_slot s[QN]; unsigned w, rd, lost; } fs_queue;
static fs_queue Q;

static int q_push(fs_queue *q, const fs_rec *r)
{
    unsigned i = __atomic_fetch_add(&q->w, 1, __ATOMIC_RELAXED) % QN, exp = 0;
    if (!__atomic_compare_exchange_n(&q->s[i].state, &exp, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_fetch_add(&q->lost, 1, __ATOMIC_RELAXED);
        return 0;
    }
    q->s[i].r = *r;
    __atomic_store_n(&q->s[i].state, 2, __ATOMIC_RELEASE);
    return 1;
}
static int q_pop(fs_queue *q, fs_rec *r)             /* worker only */
{
    for (unsigned n = 0; n < QN; n++) {
        unsigned i = q->rd++ % QN;
        if (__atomic_load_n(&q->s[i].state, __ATOMIC_ACQUIRE) != 2) continue;
        *r = q->s[i].r;
        __atomic_store_n(&q->s[i].state, 0, __ATOMIC_RELEASE);
        return 1;
    }
    return 0;
}

/* ---- hook ring: written from EP147's threads inside a hook ------------------------------
 * Fields are stored one by one (no struct copy, so no memcpy and no SIMD registers). */
enum { H_PLAY18 = 0, H_LOOPEXIT, H_RELOOP, H_PLAYOP, H_ONMOVED, H_ONAIR, H_N };
static const char *const HNAME[H_N] = { "play18", "loopExit", "reloop", "playop", "onMoved", "onair" };
#define HN 128u
typedef struct { unsigned state, kind; uint64_t t, self, a1, a2, a3, lr, thr; } fs_hev;
static fs_hev HR[HN];
static unsigned hr_w, hr_rd, hr_lost, hook_calls[H_N];

#if defined(__aarch64__) && !defined(AZ_HARNESS) && !defined(FS_UNIT)
#define HOOK_ATTR __attribute__((target("general-regs-only"), noinline))
#else
#define HOOK_ATTR __attribute__((noinline))
#endif

HOOK_ATTR static void h_record(unsigned kind, uint64_t self, uint64_t a1, uint64_t a2, uint64_t a3,
                               uint64_t lr)
{
    __atomic_fetch_add(&hook_calls[kind], 1, __ATOMIC_RELAXED);
    unsigned i = __atomic_fetch_add(&hr_w, 1, __ATOMIC_RELAXED) % HN, exp = 0;
    if (!__atomic_compare_exchange_n(&HR[i].state, &exp, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_fetch_add(&hr_lost, 1, __ATOMIC_RELAXED);
        return;
    }
    __atomic_store_n(&HR[i].kind, kind, __ATOMIC_RELAXED);
    __atomic_store_n(&HR[i].t, counter(), __ATOMIC_RELAXED);
    __atomic_store_n(&HR[i].self, self, __ATOMIC_RELAXED);
    __atomic_store_n(&HR[i].a1, a1, __ATOMIC_RELAXED);
    __atomic_store_n(&HR[i].a2, a2, __ATOMIC_RELAXED);
    __atomic_store_n(&HR[i].a3, a3, __ATOMIC_RELAXED);
    __atomic_store_n(&HR[i].lr, lr, __ATOMIC_RELAXED);
    __atomic_store_n(&HR[i].thr, thread_ptr(), __ATOMIC_RELAXED);
    __atomic_store_n(&HR[i].state, 2, __ATOMIC_RELEASE);
}
static int h_pop(fs_hev *e)                          /* worker only */
{
    for (unsigned n = 0; n < HN; n++) {
        unsigned i = hr_rd++ % HN;
        if (__atomic_load_n(&HR[i].state, __ATOMIC_ACQUIRE) != 2) continue;
        e->kind = __atomic_load_n(&HR[i].kind, __ATOMIC_RELAXED);
        e->t = __atomic_load_n(&HR[i].t, __ATOMIC_RELAXED);
        e->self = __atomic_load_n(&HR[i].self, __ATOMIC_RELAXED);
        e->a1 = __atomic_load_n(&HR[i].a1, __ATOMIC_RELAXED);
        e->a2 = __atomic_load_n(&HR[i].a2, __ATOMIC_RELAXED);
        e->a3 = __atomic_load_n(&HR[i].a3, __ATOMIC_RELAXED);
        e->lr = __atomic_load_n(&HR[i].lr, __ATOMIC_RELAXED);
        e->thr = __atomic_load_n(&HR[i].thr, __ATOMIC_RELAXED);
        __atomic_store_n(&HR[i].state, 0, __ATOMIC_RELEASE);
        return 1;
    }
    return 0;
}

/* ---- action ring: one record per fader edge (acted or skipped), same rules ------------- */
enum { A_NONE = 0, A_PLAY, A_EXIT, A_EXIT_PLAY };
static const char *const ANAME[] = { "none", "play", "exit", "exit+play" };
enum { RS_OK = 0, RS_OFF, RS_DISARMED, RS_NO_SAFE_READ, RS_AUTOMIX, RS_THREAD_UNKNOWN, RS_THREAD_MISMATCH,
       RS_MOD_LOAD, RS_COOLDOWN, RS_NO_DECK_INFO, RS_DECK_EMPTY, RS_TRACK_LOAD, RS_INPUT_NOT_DECK,
       RS_INPUT_UNKNOWN, RS_PLAY_OBJ_UNKNOWN, RS_PLAY_OBJ_INVALID, RS_PLAY_STATE_UNKNOWN, RS_RECENT_PRESS,
       RS_LOOP_UNKNOWN, RS_CUE_OBJ_UNKNOWN, RS_CUE_OBJ_INVALID, RS_ALREADY_PLAYING, RS_BUSY,
       RS_FB_LOOP_UNKNOWN, RS_FB_NO_CUE_OBJ, RS_N };
static const char *const RSNAME[RS_N] = { "ok", "off", "disarmed", "no-safe-read", "automix-hook",
    "thread-unknown", "thread-mismatch", "mod-load-guard", "cooldown", "no-deck-info", "deck-empty",
    "track-load-guard", "input-not-deck", "input-unknown", "play-obj-unknown", "play-obj-invalid",
    "play-state-unknown", "recent-press", "loop-unknown", "cue-obj-unknown", "cue-obj-invalid",
    "already-playing", "busy", "fallback-loop-unknown", "fallback-no-cue-obj" };
enum { SRC_HOOK = 0, SRC_FILTER };
#define AN 64u
#define NOCALL 0xffffffffu
typedef struct { unsigned state; uint64_t t, w0, w1, w2, w3, w4, w5; } fs_aev;
static fs_aev AR[AN];
static unsigned ar_w, ar_rd, ar_lost;
HOOK_ATTR static void ar_push(uint64_t t, uint64_t w0, uint64_t w1, uint64_t w2, uint64_t w3, uint64_t w4,
                              uint64_t w5)
{
    unsigned i = __atomic_fetch_add(&ar_w, 1, __ATOMIC_RELAXED) % AN, exp = 0;
    if (!__atomic_compare_exchange_n(&AR[i].state, &exp, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_fetch_add(&ar_lost, 1, __ATOMIC_RELAXED);
        return;
    }
    __atomic_store_n(&AR[i].t, t, __ATOMIC_RELAXED);
    __atomic_store_n(&AR[i].w0, w0, __ATOMIC_RELAXED);
    __atomic_store_n(&AR[i].w1, w1, __ATOMIC_RELAXED);
    __atomic_store_n(&AR[i].w2, w2, __ATOMIC_RELAXED);
    __atomic_store_n(&AR[i].w3, w3, __ATOMIC_RELAXED);
    __atomic_store_n(&AR[i].w4, w4, __ATOMIC_RELAXED);
    __atomic_store_n(&AR[i].w5, w5, __ATOMIC_RELAXED);
    __atomic_store_n(&AR[i].state, 2, __ATOMIC_RELEASE);
}
static int a_pop(fs_aev *e)                          /* worker only */
{
    for (unsigned n = 0; n < AN; n++) {
        unsigned i = ar_rd++ % AN;
        if (__atomic_load_n(&AR[i].state, __ATOMIC_ACQUIRE) != 2) continue;
        e->t = __atomic_load_n(&AR[i].t, __ATOMIC_RELAXED);
        e->w0 = __atomic_load_n(&AR[i].w0, __ATOMIC_RELAXED);
        e->w1 = __atomic_load_n(&AR[i].w1, __ATOMIC_RELAXED);
        e->w2 = __atomic_load_n(&AR[i].w2, __ATOMIC_RELAXED);
        e->w3 = __atomic_load_n(&AR[i].w3, __ATOMIC_RELAXED);
        e->w4 = __atomic_load_n(&AR[i].w4, __ATOMIC_RELAXED);
        e->w5 = __atomic_load_n(&AR[i].w5, __ATOMIC_RELAXED);
        __atomic_store_n(&AR[i].state, 0, __ATOMIC_RELEASE);
        return 1;
    }
    return 0;
}

/* ---- loop diagnostics ring: one record per fader edge (log-only, settles the loop field) --
 * v0 = ch | deck<<4 | (hasloop+1)<<8 | (led_loop+1)<<12 | sel<<16 | loopsrc<<20 | unread<<32 */
enum { DV_META, DV_LSP, DV_LSC, DV_LS08, DV_LS10, DV_LC30, DV_LC38, DV_LC40, DV_LS368, DV_LS370,
       DV_O48_98, DV_O50_08, DV_O50_60, DV_O58_78, DV_COREP, DV_COREC, DV_LED, DV_N };
#define DN 32u
typedef struct { unsigned state; uint64_t t, v[DV_N]; } fs_dev;
static fs_dev DR[DN];
static unsigned dr_w, dr_rd, dr_lost;
HOOK_ATTR static void dr_push(uint64_t t, const uint64_t *v)
{
    unsigned i = __atomic_fetch_add(&dr_w, 1, __ATOMIC_RELAXED) % DN, exp = 0;
    if (!__atomic_compare_exchange_n(&DR[i].state, &exp, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_fetch_add(&dr_lost, 1, __ATOMIC_RELAXED);
        return;
    }
    __atomic_store_n(&DR[i].t, t, __ATOMIC_RELAXED);
    for (int k = 0; k < DV_N; k++) __atomic_store_n(&DR[i].v[k], v[k], __ATOMIC_RELAXED);
    __atomic_store_n(&DR[i].state, 2, __ATOMIC_RELEASE);
}
static int d_pop(fs_dev *e)                          /* worker only */
{
    for (unsigned n = 0; n < DN; n++) {
        unsigned i = dr_rd++ % DN;
        if (__atomic_load_n(&DR[i].state, __ATOMIC_ACQUIRE) != 2) continue;
        e->t = __atomic_load_n(&DR[i].t, __ATOMIC_RELAXED);
        for (int k = 0; k < DV_N; k++) e->v[k] = __atomic_load_n(&DR[i].v[k], __ATOMIC_RELAXED);
        __atomic_store_n(&DR[i].state, 0, __ATOMIC_RELEASE);
        return 1;
    }
    return 0;
}

/* ---- the decision (pure; every gate, in order) ------------------------------------------ */
typedef struct {
    int en, exit_on, disarmed, safe_read, automix;
    int thread;          /* 1 bus thread, 0 unknown, -1 another thread */
    int mod_guard, cooldown, info_ok, loaded, track_guard;
    int input;           /* 1 deck, 0 other, -1 n/a (ch1/2), -2 unknown */
    int play_obj;        /* 1 valid, 0 unknown, -1 invalid */
    int playing;         /* 1, 0, -1 unknown */
    int recent_press;
    int loop;            /* 1 a loop to exit (registered; LED-active if the LED is usable), 0, -1 unknown */
    int cue_obj;         /* 1 valid, 0 unknown, -1 invalid */
} fs_in;
HOOK_ATTR static int fs_decide(const fs_in *in, int *reason)
{
#define SKIP(r) do { *reason = (r); return A_NONE; } while (0)
    if (!in->en) SKIP(RS_OFF);
    if (in->disarmed) SKIP(RS_DISARMED);
    if (!in->safe_read) SKIP(RS_NO_SAFE_READ);
    if (in->automix) SKIP(RS_AUTOMIX);
    if (in->thread == 0) SKIP(RS_THREAD_UNKNOWN);
    if (in->thread < 0) SKIP(RS_THREAD_MISMATCH);
    if (in->mod_guard) SKIP(RS_MOD_LOAD);
    if (in->cooldown) SKIP(RS_COOLDOWN);
    if (!in->info_ok) SKIP(RS_NO_DECK_INFO);
    if (in->loaded != 1) SKIP(RS_DECK_EMPTY);
    if (in->track_guard) SKIP(RS_TRACK_LOAD);
    if (in->input == 0) SKIP(RS_INPUT_NOT_DECK);
    if (in->input == -2) SKIP(RS_INPUT_UNKNOWN);
    if (in->play_obj == 0) SKIP(RS_PLAY_OBJ_UNKNOWN);
    if (in->play_obj < 0) SKIP(RS_PLAY_OBJ_INVALID);
    if (in->playing < 0) SKIP(RS_PLAY_STATE_UNKNOWN);
    if (in->recent_press) SKIP(RS_RECENT_PRESS);
    if (in->exit_on) {
        if (in->loop < 0) {                             /* unknown loop: a paused deck still plays */
            if (in->playing) SKIP(RS_LOOP_UNKNOWN);
            *reason = RS_FB_LOOP_UNKNOWN;
            return A_PLAY;
        }
        if (in->loop == 1) {
            if (in->cue_obj < 0) SKIP(RS_CUE_OBJ_INVALID);
            if (in->cue_obj == 0) {                     /* cannot exit: a paused deck still plays */
                if (in->playing) SKIP(RS_CUE_OBJ_UNKNOWN);
                *reason = RS_FB_NO_CUE_OBJ;
                return A_PLAY;
            }
            *reason = RS_OK;
            return in->playing ? A_EXIT : A_EXIT_PLAY;  /* play only if paused; never reloop */
        }
    }
    if (in->playing) SKIP(RS_ALREADY_PLAYING);
    *reason = RS_OK;
    return A_PLAY;
#undef SKIP
}

/* ---- hooks: record (or decide), then the original with the same arguments --------------- */
typedef void (*fn_play18)(void *self, uint64_t start, void *jog, uint64_t slip);
typedef void (*fn_loopexit)(void *self);
typedef void (*fn_reloop)(void *self, uint64_t slip);
typedef void (*fn_playop)(void *self, uint64_t a1, uint64_t a2, uint64_t a3);
typedef void (*fn_onmoved)(void *self, void *slider, uint64_t id, const void *pos);
typedef void (*fn_onair)(void *self, uint64_t ch, uint64_t on);
typedef void (*fn_play10)(void *self, uint32_t start, uint32_t slip);
static fn_play18 orig_play18;
static fn_loopexit orig_loopexit;
static fn_reloop orig_reloop;
static fn_playop orig_playop;
static fn_onmoved orig_onmoved;
static fn_onair orig_onair;
#define LR() ((uint64_t)(uintptr_t)__builtin_return_address(0))

/* The only two calls into EP147 this mod makes (never reloop). */
#ifdef FS_UNIT
static void (*fs_unit_play10)(void *self, uint32_t start, uint32_t slip);   /* TEST-ONLY */
static void (*fs_unit_loopexit)(void *self);
#define CALL_PLAY10(o)   fs_unit_play10((void *)(uintptr_t)(o), 0u, 0u)
#define CALL_LOOPEXIT(o) fs_unit_loopexit((void *)(uintptr_t)(o))
#else
#define CALL_PLAY10(o)   ((fn_play10)(uintptr_t)VA_PLAY10)((void *)(uintptr_t)(o), 0u, 0u)
#define CALL_LOOPEXIT(o) ((fn_loopexit)(uintptr_t)VA_LOOPEXIT)((void *)(uintptr_t)(o))
#endif

static void fs_onmoved(uint64_t self, int ch, uint32_t bits);
/* IEEE-754 single 0..1.0 -> 0..1023, round half up, integer-only. -1 = not a finite 0..1.0 */
static int pos_to_raw(uint32_t bits, uint32_t *raw)
{
    uint32_t e = (bits >> 23) & 0xffu, m = bits & 0x7fffffu;
    if (e == 0xffu) return -1;                       /* inf / NaN */
    if ((bits & 0x7fffffffu) == 0 || e == 0) {        /* +-0 and denormals (< 1e-38) = 0 */
        if ((bits >> 31) && (bits & 0x7fffffffu)) return -1;
        *raw = 0; return 0;
    }
    if (bits >> 31) return -1;                       /* negative */
    if (e > 127u || (e == 127u && m)) return -1;      /* > 1.0 */
    uint64_t mant = (uint64_t)(m | 0x800000u) * 1023u;   /* value = mant / 2^(150 - e) / 1023 * 1023 */
    unsigned sh = 150u - e;                           /* 23..149 */
    *raw = sh >= 40u ? 0u : (uint32_t)((mant + (1ull << (sh - 1))) >> sh);
    return 0;
}

HOOK_ATTR static void my_play18(void *self, uint64_t start, void *jog, uint64_t slip)
{
    h_record(H_PLAY18, (uintptr_t)self, start, (uintptr_t)jog, slip, LR());
    orig_play18(self, start, jog, slip);
}
HOOK_ATTR static void my_loopexit(void *self)
{
    h_record(H_LOOPEXIT, (uintptr_t)self, 0, 0, 0, LR());
    orig_loopexit(self);
}
HOOK_ATTR static void my_reloop(void *self, uint64_t slip)
{
    h_record(H_RELOOP, (uintptr_t)self, slip, 0, 0, LR());
    orig_reloop(self, slip);
}
HOOK_ATTR static void my_playop(void *self, uint64_t a1, uint64_t a2, uint64_t a3)
{
    h_record(H_PLAYOP, (uintptr_t)self, a1, a2, a3, LR());
    orig_playop(self, a1, a2, a3);
}
/* EP147 first (On-Air keeps its own timing), then the edge check on the same thread */
HOOK_ATTR static void my_onmoved(void *self, void *slider, uint64_t id, const void *pos)
{
    orig_onmoved(self, slider, id, pos);
    __atomic_fetch_add(&hook_calls[H_ONMOVED], 1, __ATOMIC_RELAXED);
    if (id - FADER_ID0 < 4u && pos) {
        const uint32_t *p = (const uint32_t *)pos;   /* word 0 only: a float 0..1.0 */
        fs_onmoved((uintptr_t)self, (int)(id - FADER_ID0), p[0]);
    }
}
HOOK_ATTR static void my_onair(void *self, uint64_t ch, uint64_t on)
{
    h_record(H_ONAIR, (uintptr_t)self, ch, on, 0, LR());
    orig_onair(self, ch, on);
}

/* ---- hook table, conflict list, first-instruction signatures ---------------------------- */
struct hook_def {
    const char *name;
    uintptr_t addr;
    uint32_t sig[4];                /* the first four words in EP147_bootb_8daa1de5 */
    void *fn;
    void **orig;
};
static const struct hook_def HOOKS[H_N] = {
    { "play18", VA_PLAY18, { 0xa9b27bfdu, 0x910003fdu, 0xa90153f3u, 0xaa0003f3u },
      (void *)my_play18, (void **)&orig_play18 },
    { "loopExit", VA_LOOPEXIT, { 0xa9b37bfdu, 0x910003fdu, 0xf9401401u, 0xf90037e0u },
      (void *)my_loopexit, (void **)&orig_loopexit },
    { "reloop", VA_RELOOP, { 0xa9b37bfdu, 0x910003fdu, 0xa90153f3u, 0xaa0003f3u },
      (void *)my_reloop, (void **)&orig_reloop },
    { "playop", VA_PLAYOP, { 0xd10a83ffu, 0xa9007bfdu, 0x910003fdu, 0xa90153f3u },
      (void *)my_playop, (void **)&orig_playop },
    { "onMoved", VA_ONMOVED, { 0xa9b27bfdu, 0x910003fdu, 0xa9025bf5u, 0xaa0003f5u },
      (void *)my_onmoved, (void **)&orig_onmoved },
    { "onair", VA_ONAIR, { 0xa9b17bfdu, 0x90ffb123u, 0x912e0063u, 0x910003fdu },
      (void *)my_onair, (void **)&orig_onair },
};
/* Addresses other mods (and the core) claim: never claim within 16 bytes of one, whether or
 * not that mod is installed. */
struct foreign { uintptr_t addr; const char *owner; };
static const struct foreign FOREIGN[] = {
    { 0x86b410u, "active-loop" },  { 0x142da18u, "active-loop/automix" },
    { 0x142d3d0u, "active-loop/automix" }, { 0x1437c20u, "active-loop" },
    { 0x14b37a0u, "active-loop/automix" }, { 0x861218u, "active-loop" },
    { 0x1d6c8a8u, "active-loop" }, { 0x1d6c380u, "active-loop" },
    { 0x14415a8u, "automix" },     { 0x1423488u, "automix" },
    { 0x141f358u, "automix" },     { 0x149c230u, "automix" },
    { 0x9b3bb0u, "automix" },      { 0x149afe8u, "tempo-range" },
    { 0x2335660u, "tempo-range" }, { 0x0abdd18u, "AZ-MODS core" },
    { 0x0abec10u, "AZ-MODS core" }, { 0x0abf648u, "AZ-MODS core" },
};
static const char *conflict_owner(uintptr_t a)
{
    for (size_t i = 0; i < sizeof FOREIGN / sizeof FOREIGN[0]; i++) {
        uintptr_t f = FOREIGN[i].addr;
        if ((a > f ? a - f : f - a) < 16u) return FOREIGN[i].owner;
    }
    return NULL;
}
/* where a play-op return address sits: which Play slot (or its queued task) called it */
static const char *playop_via(uint64_t lr)
{
    static const struct { uint64_t lo, n; const char *name; } R[] = {
        { 0x14415a8u, 1176, "slot+0x10" }, { 0x1441418u, 132, "slot+0x10-task" },
        { 0x1443560u, 1256, "slot+0x18" }, { 0x14440a8u, 192, "slot+0x18-task" },
        { 0x1436d38u, 872, "fn_1436d38" }, { 0x14424c8u, 136, "fn_14424c8" },
    };
    for (size_t i = 0; i < sizeof R / sizeof R[0]; i++)
        if (lr > R[i].lo && lr <= R[i].lo + R[i].n) return R[i].name;
    return "other";
}
static int playop_is_play(uint64_t lr) { return strncmp(playop_via(lr), "slot+", 5) == 0; }

/* ---- shared state (filters / hooks / tick / worker): plain words with atomics ---------- */
#define NCH 4
#define CLOSED_MAX   16             /* <= 16/1023 is closed */
#define OPEN_MIN     64             /* >= 64/1023 is open */
#define ARM_MS       250            /* closed this long before an open counts */
#define DEADBAND     32             /* fader lines: a change of >= 32/1023 ... */
#define FADER_GAP_MS 250            /* ... at most every 250 ms per channel */
#define SETTLE_MS    400            /* and one SETTLE line once it rests */
#define RATE_WIN_MS  10000
#define GUARD_MS     2000           /* no action within 2 s of mod load or a track load */
#define COOLDOWN_MS  1000           /* one action per channel per second at most */
#define PRESS_MS     500            /* no action within 500 ms of a PLAY press on that deck */
#define SLOW_CALL_US 2000u          /* a facade call longer than this disarms */
#define LEARN_MS     150            /* a hook within 150 ms of one press on one side */
#define NB 9                        /* frame-interval buckets */
static const unsigned BUCKET_MS[NB - 1] = { 1, 2, 5, 10, 20, 50, 100, 500 };

static uint64_t mx_fad;             /* 4 x 16-bit fader values, ch1 low */
static unsigned mx_xf;              /* crossfader 0..1023 */
static unsigned mx_frames, mx_move_frames, mx_hist[2][NB];   /* cumulative, filter-written */
static unsigned mx_edge_seq[NCH];
static uint64_t mx_edge_t[NCH], mx_closed_pub[NCH];          /* filter -> tick / hook */
/* filter-private (one bus thread) */
static uint64_t mf_last_t, mf_closed_since[NCH];
static unsigned mf_prev[NCH], mf_seen, mf_armed_open[NCH];

static unsigned erp_sel[2] = { 0, 0 };      /* 0 = unknown, else deck 1..4 */
static unsigned erp_last_bits[2];           /* baseline all released: the first press counts */
static unsigned erp_frames;
static uint64_t erp_last_t, mx_last_t;      /* last frame per input bus (heartbeat) */
static uint64_t erp_press_t[2][6];          /* play, cue, reloop, loopIn, loopOut, 4beat */
static const char *const BTN[] = { "play", "cue", "reloop", "loopIn", "loopOut", "4beat" };
enum { B_PLAY = 0, B_CUE, B_RELOOP };

static uint64_t led_word[2];                /* bytes 0x08..0x0c of each side */
static unsigned led_frames;
static int led_loop_pub[2] = { -1, -1 };    /* tick -> hook: the side's LED loop state, -1 = unusable */

/* threads: one line per (context, thread) */
enum { X_TICK, X_MIXER, X_ERP, X_LED, X_WORKER, X_DECKEV, X_ONMOVED, NX };
static const char *const XNAME[NX] = { "tick", "mixer-filter", "erp-filter", "led-filter", "worker", "deck-event",
                                       "onMoved-hook" };
static uint64_t x_thr[NX];
static void note_thread(int x, uint64_t t)
{
    uint64_t thr = thread_ptr();
    if (__atomic_load_n(&x_thr[x], __ATOMIC_RELAXED) == thr) return;
    __atomic_store_n(&x_thr[x], thr, __ATOMIC_RELAXED);
    fs_rec r;
    memset(&r, 0, sizeof r);
    r.t = t; r.type = R_THREAD; r.a = x; r.b = x == X_ONMOVED ? -1 : (int)kernel_tid(); r.x[0] = thr;
    q_push(&Q, &r);
}

/* filter self-timing: calls over 200 us (the core counts 8 and switches the filter off) */
enum { F_MIXER, F_ERP, F_LED, NF };
static const char *const FNAME[NF] = { "mixer", "erp", "led" };
#define SLOW_US 200u
#define MIXER_SLOW_DISARM 5u        /* deck run 2: the core switched LED off when we had seen 5 */
static unsigned f_slow[NF], f_max_us[NF];
static void filt_done(int f, uint64_t t_in)
{
    uint64_t us = ticks_to_us(counter() - t_in);
    if (us > __atomic_load_n(&f_max_us[f], __ATOMIC_RELAXED)) __atomic_store_n(&f_max_us[f], (unsigned)us, __ATOMIC_RELAXED);
    if (us <= SLOW_US) return;
    unsigned n = __atomic_add_fetch(&f_slow[f], 1, __ATOMIC_RELAXED);
    fs_rec r;
    memset(&r, 0, sizeof r);
    r.t = t_in; r.type = R_SLOW; r.a = f; r.b = (int)n; r.x[0] = us;
    q_push(&Q, &r);
}

static uint64_t last_activity_t;            /* last fader edge or deck-button press */
static unsigned deck_ev_quiet;
static uint64_t load_t[NCH];                /* deck-event LOADED time per deck */
static uint64_t deckev_t[NCH];                 /* 0.0.6: last PLAY/STOP deck event per deck */

static int h_log = -1, h_onair = -1, h_en = -1, h_exit = -1, n_rows;
static unsigned claimed, refused, conflicted, sig_failed;
static int init_done, filters_on;

/* ---- arming state ----------------------------------------------------------------------- */
enum { D_NONE = 0, D_OBJ_INVALID, D_SLOW_CALL, D_OBJ_CONFLICT, D_THREAD, D_POS_RANGE, D_ONAIR_VT,
       D_ONMOVED_SILENT, D_MIXER_FILTER, D_LS_MISMATCH, D_N };
static const char *const DNAME[D_N] = { "none", "object-vtable-check-failed", "facade-call-over-2ms",
    "object-spacing-conflict", "onMoved-not-on-bus-thread", "fader-position-not-a-0..1-float",
    "onair-manager-vtable-mismatch", "onMoved-silent-on-fader-edges", "mixer-filter-near-switch-off",
    "cueloop-loopstate-or-core-differs-from-play-facade" };
/* banner text must be static storage (the core queues it) */
static const char *const DBANNER[D_N] = { "Fader Start", "Fader Start disarmed: object check failed",
    "Fader Start disarmed: slow deck call", "Fader Start disarmed: object conflict",
    "Fader Start disarmed: wrong thread", "Fader Start disarmed: bad fader value",
    "Fader Start disarmed: On-Air check failed", "Fader Start disarmed: fader hook silent",
    "Fader Start disarmed: mixer filter failing", "Fader Start disarmed: loop object mismatch" };
static int disarm_why;                      /* latched; cleared by switching the master off and on */
static uint64_t disarm_t;
HOOK_ATTR static void disarm(int why)
{
    int exp = 0;
    if (__atomic_compare_exchange_n(&disarm_why, &exp, why, 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED))
        __atomic_store_n(&disarm_t, counter(), __ATOMIC_RELAXED);
}
static uint64_t play_obj[NCH], cue_obj[NCH];        /* worker writes, hook reads */
static unsigned play_src[NCH], cue_src[NCH];        /* 0 none, 1 learned from a press, 2 0x620 fill-in,
                                                     * 3 found by the memory scan (CueLoop only) */
static uint64_t hook_bus_thr;                       /* where EP147 itself called play18/loopExit */
static uint64_t onair_mgr, onair_seen;              /* onMoved's this, once its vtable checked */
static uint64_t act_t[NCH];                         /* last executed action per channel */
static unsigned act_busy, act_n, skip_n;
static unsigned last_act_w0, last_act_us, act_max_us;   /* act_max_us: longest fs_act (0.0.7) */

static int en_row(void) { return h_en >= 0 && AZ_HAS(C, row_value) && C->row_value(h_en) == 1; }
static int exit_row(void) { return h_exit < 0 || !AZ_HAS(C, row_value) || C->row_value(h_exit) == 1; }
static int act_src(void) { return (__atomic_load_n(&claimed, __ATOMIC_RELAXED) >> H_ONMOVED) & 1u ? SRC_HOOK : SRC_FILTER; }
static uint64_t bus_thread(void)
{
    uint64_t t = __atomic_load_n(&hook_bus_thr, __ATOMIC_RELAXED);
    if (!t) t = __atomic_load_n(&x_thr[X_ERP], __ATOMIC_RELAXED);
    if (!t) t = __atomic_load_n(&x_thr[X_MIXER], __ATOMIC_RELAXED);
    return t;
}
/* automix (or anyone) hooked play +0x10: its first instruction is no longer EP147's */
static int automix_hooked(void)
{
    uint64_t w = 0;
    if (read_u64(VA_PLAY10, &w) != 0) return -1;
    return (uint32_t)w != PLAY10_WORD0;
}

/* LoopState has_loop (0x149f090 -> CueProperty valid 0x14a7668 on LoopState+0x160), read from
 * memory exactly as EP147 tests it: 1 a loop is registered (loopExit acts), 0 none (loopExit's
 * task returns at 0x14299fc), -1 unreadable */
HOOK_ATTR static int ls_has_loop(uint64_t ls)
{
    uint64_t a = 0, b = 0, c = 0;
    if (!ls || read_u64((uintptr_t)(ls + LS_LOOPCUE + 0x30), &a) != 0 ||
        read_u64((uintptr_t)(ls + LS_LOOPCUE + 0x38), &b) != 0 || read_u64((uintptr_t)(ls + LS_LOOPCUE + 0x40), &c) != 0)
        return -1;
    return (a & 0xffu) && (a >> 32 & 0x1fu) && (a >> 48 & 0xffu) && (uint32_t)b && c != 0x7fffffffffffffffull;
}
/* one diagnostic word; a failed read sets its bit in *mask and gives 0 */
HOOK_ATTR static uint64_t dq(uint64_t a, int bit, unsigned *mask)
{
    uint64_t v = 0;
    if (!a || read_u64((uintptr_t)a, &v) != 0) { *mask |= 1u << bit; return 0; }
    return v;
}

/* ---- the action: called on the bus thread from the onMoved hook (or the mixer filter) ---- */
HOOK_ATTR static void fs_act(int ch, int src, unsigned v, uint64_t closed, uint64_t t)
{
    unsigned exp = 0;
    int d = ch, s = ch & 1;                  /* channel n = deck n; decks 1/3 side A, 2/4 side B */
    uint64_t thr = thread_ptr(), t_in = counter();
    if (!__atomic_compare_exchange_n(&act_busy, &exp, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        ar_push(t, (uint64_t)ch | (uint64_t)src << 4 | (uint64_t)RS_BUSY << 16, 0, 0, 0, 0, thr);
        return;
    }
    fs_in in;
    in.en = en_row();
    in.exit_on = exit_row();
    in.disarmed = __atomic_load_n(&disarm_why, __ATOMIC_ACQUIRE) != 0;   /* before this edge: its own failure shows its reason */
    in.safe_read = vm_ok;
    in.automix = automix_hooked() != 0;     /* unreadable counts as hooked */
    uint64_t bus = bus_thread();
    in.thread = !bus ? 0 : thr == bus ? 1 : -1;
    if (in.thread < 0 && src == SRC_HOOK && __atomic_load_n(&hook_bus_thr, __ATOMIC_RELAXED)) disarm(D_THREAD);
    in.mod_guard = t - t0 < ms_to_ticks(GUARD_MS);
    uint64_t at = __atomic_load_n(&act_t[ch], __ATOMIC_RELAXED);
    in.cooldown = at && t - at < ms_to_ticks(COOLDOWN_MS);
    az_deck_info di;
    di.size = sizeof di; di.loaded = -1; di.playing = -1;
    in.info_ok = AZ_HAS(C, deck_info) && C->deck_info(d, &di) == 0;
    in.loaded = in.info_ok ? di.loaded : -1;
    in.playing = in.info_ok ? di.playing : -1;
    uint64_t lt = __atomic_load_n(&load_t[d], __ATOMIC_RELAXED);
    in.track_guard = lt && t - lt < ms_to_ticks(GUARD_MS);
    /* channel 3/4 input flag (EP147's On-Air eligibility); channels 1/2 have none */
    in.input = -1;
    if (ch >= 2) {
        uint64_t m = __atomic_load_n(&onair_mgr, __ATOMIC_RELAXED), w = 0;
        in.input = -2;
        if (m && obj_check(m, VT_ONAIRMGR) == 1 && read_u64((uintptr_t)(m + ONAIR_IN_OFF), &w) == 0)
            in.input = (int)((w >> (8 * (ch == 2 ? 2 : 3))) & 0xffu) == 1 ? 1 : 0;
    }
    uint64_t po = __atomic_load_n(&play_obj[d], __ATOMIC_ACQUIRE), co = __atomic_load_n(&cue_obj[d], __ATOMIC_ACQUIRE);
    in.play_obj = obj_check(po, VT_PLAYPAUSE);
    in.cue_obj = obj_check(co, VT_CUELOOP);
    if (in.play_obj < 0 || in.cue_obj < 0) disarm(D_OBJ_INVALID);
    uint64_t pt = __atomic_load_n(&erp_press_t[s][B_PLAY], __ATOMIC_RELAXED);
    in.recent_press = pt && t - pt < ms_to_ticks(PRESS_MS) && __atomic_load_n(&erp_sel[s], __ATOMIC_RELAXED) == (unsigned)(d + 1);
    /* loop state (0.0.5): the deck's LoopState through its Play facade (no RELOOP press needed,
     * no filter that can be switched off): has_loop = loopExit has something to exit. The LED
     * rule, while usable for this deck, tells an active loop from a registered inactive one. A
     * CueLoop facade must share the Play facade's LoopState and deck core, else it is not this
     * deck's: invalid + disarm. */
    int loopsrc = 0, has = -1, ledl = -1;    /* loopsrc 0 none, 1 has_loop, 2 LED, 3 has_loop+LED */
    uint64_t lsp = 0, lsc = 0, corep = 0, corec = 0;
    int okp = 0;
    in.loop = -1;
    if (in.play_obj == 1) {
        okp = read_u64((uintptr_t)(po + PLAY_LS_OFF), &lsp) == 0 && lsp && read_u64((uintptr_t)(po + PLAY_CORE_OFF), &corep) == 0;
        if (okp) has = ls_has_loop(lsp);
    }
    if (in.cue_obj == 1) {
        int okc = read_u64((uintptr_t)(co + CUE_LS_OFF), &lsc) == 0 && read_u64((uintptr_t)(co + CUE_CORE_OFF), &corec) == 0;
        if (okp && (!okc || lsc != lsp || corec != corep)) { in.cue_obj = -1; disarm(D_LS_MISMATCH); }
    }
    unsigned sel = __atomic_load_n(&erp_sel[s], __ATOMIC_RELAXED);
    if (sel == (unsigned)(d + 1)) ledl = __atomic_load_n(&led_loop_pub[s], __ATOMIC_ACQUIRE);
    if (has == 0) { in.loop = 0; loopsrc = 1; }
    else if (has == 1) { if (ledl >= 0) { in.loop = ledl; loopsrc = 3; } else { in.loop = 1; loopsrc = 1; } }
    else if (ledl >= 0) { in.loop = ledl; loopsrc = 2; }
    if (in.play_obj == 1) {                  /* log-only: the candidate fields, before any call */
        uint64_t v[DV_N];                    /* every element is assigned below (no memset in a hook) */
        unsigned mask = 0;
        v[DV_LSP] = lsp; v[DV_LSC] = lsc; v[DV_COREP] = corep; v[DV_COREC] = corec;
        v[DV_LS08] = dq(lsp ? lsp + LS_CUEPLAY : 0, 0, &mask);
        v[DV_LS10] = dq(lsp ? lsp + 0x10 : 0, 1, &mask);
        v[DV_LC30] = dq(lsp ? lsp + LS_LOOPCUE + 0x30 : 0, 2, &mask);
        v[DV_LC38] = dq(lsp ? lsp + LS_LOOPCUE + 0x38 : 0, 3, &mask);
        v[DV_LC40] = dq(lsp ? lsp + LS_LOOPCUE + 0x40 : 0, 4, &mask);
        v[DV_LS368] = dq(lsp ? lsp + 0x368 : 0, 5, &mask);
        v[DV_LS370] = dq(lsp ? lsp + 0x370 : 0, 6, &mask);
        uint64_t o48 = dq(po + 0x48, 7, &mask), o50 = dq(po + 0x50, 8, &mask), o58 = dq(po + 0x58, 9, &mask);
        v[DV_O48_98] = dq(o48 ? o48 + 0x98 : 0, 10, &mask);
        v[DV_O50_08] = dq(o50 ? o50 + 0x08 : 0, 11, &mask);
        v[DV_O50_60] = dq(o50 ? o50 + 0x60 : 0, 12, &mask);
        v[DV_O58_78] = dq(o58 ? o58 + 0x78 : 0, 13, &mask);
        v[DV_LED] = __atomic_load_n(&led_word[s], __ATOMIC_RELAXED);
        v[DV_META] = (uint64_t)ch | (uint64_t)d << 4 | (uint64_t)(has + 1) << 8 | (uint64_t)(ledl + 1) << 12 |
                     (uint64_t)(sel & 15u) << 16 | (uint64_t)loopsrc << 20 | (uint64_t)mask << 32;
        dr_push(t, v);
    }
    int reason = RS_OK, act = fs_decide(&in, &reason), would = A_NONE, wreason = RS_OK;
    if (reason == RS_OFF || reason == RS_DISARMED) {
        int sen = in.en, sdis = in.disarmed;   /* no struct copy here: it would be a memcpy call */
        in.en = 1; in.disarmed = 0;
        would = fs_decide(&in, &wreason);
        in.en = sen; in.disarmed = sdis;
    }
    uint32_t us_exit = NOCALL, us_play = NOCALL;
    if (act == A_EXIT || act == A_EXIT_PLAY) {
        uint64_t a = counter();
        CALL_LOOPEXIT(co);
        us_exit = (uint32_t)ticks_to_us(counter() - a);
    }
    if (act == A_PLAY || act == A_EXIT_PLAY) {
        uint64_t a = counter();
        CALL_PLAY10(po);
        us_play = (uint32_t)ticks_to_us(counter() - a);
    }
    if (act != A_NONE) {
        __atomic_store_n(&act_t[ch], t, __ATOMIC_RELAXED);
        __atomic_fetch_add(&act_n, 1, __ATOMIC_RELAXED);
        if ((us_exit != NOCALL && us_exit > SLOW_CALL_US) || (us_play != NOCALL && us_play > SLOW_CALL_US))
            disarm(D_SLOW_CALL);
    } else {
        __atomic_fetch_add(&skip_n, 1, __ATOMIC_RELAXED);
    }
    uint64_t w0 = (uint64_t)ch | (uint64_t)src << 4 | (uint64_t)act << 8 | (uint64_t)reason << 16 |
                  (uint64_t)(in.loaded + 1) << 24 | (uint64_t)(in.playing + 1) << 28 |
                  (uint64_t)(in.loop + 1) << 32 | (uint64_t)loopsrc << 36 | (uint64_t)(in.input + 2) << 40 |
                  (uint64_t)would << 44 | (uint64_t)wreason << 48 | (uint64_t)(in.exit_on & 1) << 56 |
                  (uint64_t)(__atomic_load_n(&play_src[d], __ATOMIC_RELAXED) & 3u) << 58 |
                  (uint64_t)(__atomic_load_n(&cue_src[d], __ATOMIC_RELAXED) & 3u) << 60;
    uint64_t c_ms = closed / (hz / 1000u);
    ar_push(t, w0, (c_ms > 0xffffffffu ? 0xffffffffu : c_ms) | (uint64_t)v << 32,
            (uint64_t)us_exit | (uint64_t)us_play << 32, po, co, thr);
    if (act != A_NONE) {
        __atomic_store_n(&last_act_w0, (unsigned)(w0 & 0xffffu), __ATOMIC_RELAXED);
        __atomic_store_n(&last_act_us, us_play != NOCALL ? us_play : us_exit, __ATOMIC_RELAXED);
    }
    {
        uint64_t took = ticks_to_us(counter() - t_in);
        unsigned tu = took > 0xffffffffu ? 0xffffffffu : (unsigned)took;
        if (tu > __atomic_load_n(&act_max_us, __ATOMIC_RELAXED)) __atomic_store_n(&act_max_us, tu, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&act_busy, 0, __ATOMIC_RELEASE);
}

/* ---- onMoved hook body: channel-fader edges (hysteresis as the mixer filter) ------------ */
static uint64_t hk_closed_since[NCH], hk_last_t[NCH];
static unsigned hk_armed_open[NCH], hk_seen[NCH], hk_busy, hk_busy_skips, hk_raw[NCH], hk_bad_bits;
HOOK_ATTR static void fs_onmoved(uint64_t self, int ch, uint32_t bits)
{
    uint64_t t = counter();
    uint32_t raw = 0;
    int pos_ok = pos_to_raw(bits, &raw) == 0;
    if (!pos_ok) raw = 0xffffffffu;
    unsigned exp = 0;
    __atomic_store_n(&hk_last_t[ch], t, __ATOMIC_RELAXED);
    __atomic_store_n(&hk_raw[ch], raw, __ATOMIC_RELAXED);
    if (!__atomic_compare_exchange_n(&hk_busy, &exp, 1, 0, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED)) {
        __atomic_fetch_add(&hk_busy_skips, 1, __ATOMIC_RELAXED);
        return;
    }
    if (!__atomic_load_n(&x_thr[X_ONMOVED], __ATOMIC_RELAXED)) {
        __atomic_store_n(&x_thr[X_ONMOVED], thread_ptr(), __ATOMIC_RELAXED);
        h_record(H_ONMOVED, self, (uint64_t)ch, raw, bits, 0);  /* once: its thread for the log */
    }
    if (self != __atomic_load_n(&onair_seen, __ATOMIC_RELAXED)) {
        __atomic_store_n(&onair_seen, self, __ATOMIC_RELAXED);
        if (obj_check(self, VT_ONAIRMGR) == 1) __atomic_store_n(&onair_mgr, self, __ATOMIC_RELEASE);
        else { __atomic_store_n(&onair_mgr, 0, __ATOMIC_RELEASE); if (vm_ok) disarm(D_ONAIR_VT); }
    }
    if (!pos_ok) {
        __atomic_store_n(&hk_bad_bits, bits, __ATOMIC_RELAXED);
        disarm(D_POS_RANGE);
    } else if (raw <= CLOSED_MAX) {
        if (!hk_closed_since[ch]) hk_closed_since[ch] = t;
        hk_armed_open[ch] = 0;
        hk_seen[ch] = 1;
    } else if (raw >= OPEN_MIN) {
        if (!hk_seen[ch]) {
            /* first sight of this fader: the mixer filter may have seen it closed already */
            uint64_t c = __atomic_load_n(&mx_closed_pub[ch], __ATOMIC_RELAXED);
            if (c && c < t) hk_closed_since[ch] = c;
        }
        if (!hk_armed_open[ch] && hk_closed_since[ch] && t - hk_closed_since[ch] >= ms_to_ticks(ARM_MS)) {
            __atomic_store_n(&last_activity_t, t, __ATOMIC_RELAXED);
            fs_act(ch, SRC_HOOK, raw, t - hk_closed_since[ch], t);
        }
        hk_armed_open[ch] = 1;
        hk_closed_since[ch] = 0;
        hk_seen[ch] = 1;
    } else {
        hk_seen[ch] = 1;                     /* between the thresholds: neither arms nor fires */
    }
    __atomic_store_n(&hk_busy, 0, __ATOMIC_RELEASE);
}

static unsigned fad(uint64_t w, int ch) { return (unsigned)(w >> (16 * ch)) & 0xffffu; }

/* ---- mixer filter: decode, rate, hysteresis (read-only); acts only without the hook ---- */
static int on_mixer(int bus, unsigned char *f, int len, void *user)
{
    (void)bus; (void)user;
    if (!f || len < 0x11) return AZ_FRAME_OK;
    uint64_t t = counter();
    note_thread(X_MIXER, t);
    unsigned v[NCH], lo = f[0x0e];
    uint64_t w = 0;
    int moving = 0;
    for (int i = 0; i < NCH; i++) {
        v[i] = ((unsigned)f[0x0a + i] << 2) | ((lo >> (6 - 2 * i)) & 3u);
        w |= (uint64_t)v[i] << (16 * i);
        if (mf_seen && (v[i] > mf_prev[i] ? v[i] - mf_prev[i] : mf_prev[i] - v[i]) >= 2) moving = 1;
    }
    unsigned xf = ((unsigned)f[0x0f] << 2) | (f[0x10] & 3u);
    if (mf_seen) {
        uint64_t dms = (t - mf_last_t) / (hz / 1000u);
        unsigned b = 0;
        while (b < NB - 1 && dms >= BUCKET_MS[b]) b++;
        __atomic_fetch_add(&mx_hist[moving][b], 1, __ATOMIC_RELAXED);
        if (moving) __atomic_fetch_add(&mx_move_frames, 1, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&mx_fad, w, __ATOMIC_RELAXED);
    __atomic_store_n(&mx_xf, xf, __ATOMIC_RELAXED);
    for (int i = 0; i < NCH; i++) {
        if (v[i] <= CLOSED_MAX) {
            if (!mf_seen || !mf_closed_since[i]) mf_closed_since[i] = t;
            mf_armed_open[i] = 0;
        } else if (v[i] >= OPEN_MIN) {
            /* one edge per closed->open move: armed only after ARM_MS closed; the first frame
             * after load only starts the closed timer, so a fader already up never fires */
            if (!mf_armed_open[i] && mf_closed_since[i] && t - mf_closed_since[i] >= ms_to_ticks(ARM_MS)) {
                uint64_t closed = t - mf_closed_since[i];
                __atomic_store_n(&mx_edge_t[i], t, __ATOMIC_RELAXED);
                __atomic_fetch_add(&mx_edge_seq[i], 1, __ATOMIC_RELEASE);
                __atomic_store_n(&last_activity_t, t, __ATOMIC_RELAXED);
                if (act_src() == SRC_FILTER) fs_act(i, SRC_FILTER, v[i], closed, t);
            }
            mf_armed_open[i] = 1;
            mf_closed_since[i] = 0;
        } /* between the thresholds: neither arms nor fires (hysteresis) */
        __atomic_store_n(&mx_closed_pub[i], mf_closed_since[i], __ATOMIC_RELAXED);
        mf_prev[i] = v[i];
    }
    mf_last_t = t;
    mf_seen = 1;
    __atomic_store_n(&mx_last_t, t, __ATOMIC_RELAXED);
    __atomic_fetch_add(&mx_frames, 1, __ATOMIC_RELEASE);
    filt_done(F_MIXER, t);
    return AZ_FRAME_OK;
}

/* ---- ERP_IN filter: deck select and button presses (read-only, RAW) -------------------- */
#define ERP_O(s, b) ((b) + 8 * (s))
static unsigned erp_bits(const unsigned char *f, int s)
{
    unsigned b = 0;
    b |= (f[ERP_O(s, 0x0a)] >> 1 & 1u) << 0;       /* play */
    b |= (f[ERP_O(s, 0x0a)] >> 2 & 1u) << 1;       /* cue */
    b |= (f[ERP_O(s, 0x09)] >> 3 & 1u) << 2;       /* reloop/exit */
    b |= (f[ERP_O(s, 0x08)] >> 6 & 1u) << 3;       /* loop in */
    b |= (f[ERP_O(s, 0x08)] >> 5 & 1u) << 4;       /* loop out */
    b |= (f[ERP_O(s, 0x0c)] >> 2 & 1u) << 5;       /* 4-beat loop */
    b |= (f[ERP_O(s, 0x08)] & 1u) << 6;            /* deck select primary (1 / 2) */
    b |= (f[ERP_O(s, 0x09)] & 1u) << 7;            /* deck select secondary (3 / 4) */
    b |= (f[ERP_O(s, 0x09)] >> 7 & 1u) << 8;       /* shift */
    return b;
}
static int on_erp(int bus, unsigned char *f, int len, void *user)
{
    (void)bus; (void)user;
    if (!f || len < 0x15) return AZ_FRAME_OK;
    uint64_t t = counter();
    note_thread(X_ERP, t);
    __atomic_store_n(&erp_last_t, t, __ATOMIC_RELAXED);
    __atomic_fetch_add(&erp_frames, 1, __ATOMIC_RELAXED);
    for (int s = 0; s < 2; s++) {
        unsigned b = erp_bits(f, s), old = erp_last_bits[s];
        erp_last_bits[s] = b;
        unsigned chg = b ^ old;
        if (!chg) continue;
        if ((chg & b) & (1u << 6)) __atomic_store_n(&erp_sel[s], (unsigned)(1 + s), __ATOMIC_RELAXED);
        if ((chg & b) & (1u << 7)) __atomic_store_n(&erp_sel[s], (unsigned)(3 + s), __ATOMIC_RELAXED);
        if ((chg & b) & (3u << 6)) {
            fs_rec r = { .t = t, .type = R_SEL, .a = s,
                         .b = (int)__atomic_load_n(&erp_sel[s], __ATOMIC_RELAXED) };
            q_push(&Q, &r);
        }
        for (int k = 0; k < 6; k++) {
            if (!(chg & (1u << k))) continue;
            int down = (b >> k) & 1u;
            if (down) __atomic_store_n(&erp_press_t[s][k], t, __ATOMIC_RELAXED);
            if (down) __atomic_store_n(&last_activity_t, t, __ATOMIC_RELAXED);
            if (!down && k > 2) continue;           /* releases only for play / cue / reloop */
            fs_rec r = { .t = t, .type = R_PRESS, .a = s, .b = k, .c = down, .d = (int)(b >> 8 & 1u),
                         .e = (int)__atomic_load_n(&erp_sel[s], __ATOMIC_RELAXED) };
            q_push(&Q, &r);
        }
    }
    filt_done(F_ERP, t);
    return AZ_FRAME_OK;
}

/* ---- ERP_LED filter: copy the deck-button LED bytes (never edits; as short as possible) - */
#define LEDN 0x78
static int on_led(int bus, unsigned char *f, int len, void *user)
{
    (void)bus; (void)user;
    if (!f || len < LEDN) return AZ_FRAME_OK;
    uint64_t t = counter();
    note_thread(X_LED, t);
    for (int s = 0; s < 2; s++) {
        const unsigned char *p = f + 0x38 * s;
        uint64_t w = (uint64_t)p[0x08] | (uint64_t)p[0x09] << 8 | (uint64_t)p[0x0a] << 16 |
                     (uint64_t)p[0x0b] << 24 | (uint64_t)p[0x0c] << 32;
        __atomic_store_n(&led_word[s], w, __ATOMIC_RELAXED);
    }
    __atomic_fetch_add(&led_frames, 1, __ATOMIC_RELEASE);
    filt_done(F_LED, t);
    return AZ_FRAME_OK;
}
/* 2-bit levels: PLAY 0x0b<<0, CUE 0x0b<<2, IN 0x0a<<6, OUT 0x0a<<4, RELOOP 0x09<<0,
 * JOG MODE 0x0c<<4, VINYL SPEED 0x0c<<6 (w = bytes 0x08..0x0c, 0x08 lowest) */
enum { L_PLAY, L_CUE, L_IN, L_OUT, L_RELOOP, L_JOG, L_VSPD, NL };
static unsigned led_level(uint64_t w, int l)
{
    switch (l) {
    case L_PLAY: return (unsigned)(w >> 24) & 3u;
    case L_CUE: return (unsigned)(w >> 26) & 3u;
    case L_IN: return (unsigned)(w >> 22) & 3u;
    case L_OUT: return (unsigned)(w >> 20) & 3u;
    case L_RELOOP: return (unsigned)(w >> 8) & 3u;
    case L_JOG: return (unsigned)(w >> 36) & 3u;
    default: return (unsigned)(w >> 38) & 3u;
    }
}
/* derived per LED: 0 off, 1 on, 2 blinking */
#define BLINK_MS 1200            /* a blink = two level steps within 1.2 s */
static unsigned tl_level[2][NL], tl_state[2][NL];
static uint64_t tl_change[2][NL], tl_prev_change[2][NL];
static int tl_seen;
static const char ST[] = "-+*";
/* the deck shown on side s plays a loop: IN and OUT blink together and RELOOP is lit (deck
 * run 1: IN alone blinks on load and after CUE; after loopExit IN/OUT are steady, RELOOP lit) */
static int side_loop(int s)
{
    return tl_state[s][L_IN] == 2 && tl_state[s][L_OUT] == 2 && tl_state[s][L_RELOOP] != 0;
}
static int side_stored(int s) { return tl_state[s][L_RELOOP] != 0; }

/* ---- tick: fader lines, LED state, arming, cross-checks, rate stats (no logging here) --- */
static unsigned tk_logged[NCH], tk_last_v[NCH], tk_settled[NCH], tk_edge_seen[NCH];
static uint64_t tk_logged_t[NCH], tk_change_t[NCH], tk_rate_t, tk_rate_idle_t;
static unsigned tk_frames0, tk_move0, tk_hist0[2][NB], tk_have_fad;

static void push(fs_rec *r) { q_push(&Q, r); }
static int drain_once(void);

static void tick_faders(uint64_t now)
{
    unsigned frames = __atomic_load_n(&mx_frames, __ATOMIC_ACQUIRE);
    if (!frames) return;
    uint64_t w = __atomic_load_n(&mx_fad, __ATOMIC_RELAXED);
    for (int i = 0; i < NCH; i++) {
        unsigned v = fad(w, i);
        if (!tk_have_fad) { tk_logged[i] = tk_last_v[i] = v; tk_settled[i] = 1; tk_change_t[i] = now; continue; }
        if (v != tk_last_v[i]) { tk_last_v[i] = v; tk_change_t[i] = now; tk_settled[i] = 0; }
        unsigned d = v > tk_logged[i] ? v - tk_logged[i] : tk_logged[i] - v;
        if (d >= DEADBAND && now - tk_logged_t[i] >= ms_to_ticks(FADER_GAP_MS)) {
            fs_rec r = { .t = now, .type = R_FADER, .a = i + 1, .b = (int)v, .c = (int)tk_logged[i],
                         .d = (int)__atomic_load_n(&mx_xf, __ATOMIC_RELAXED),
                         .e = (int)__atomic_load_n(&hk_raw[i], __ATOMIC_RELAXED) };
            push(&r);
            tk_logged[i] = v; tk_logged_t[i] = now;
        } else if (!tk_settled[i] && now - tk_change_t[i] >= ms_to_ticks(SETTLE_MS)) {
            tk_settled[i] = 1;
            if (v != tk_logged[i]) {
                fs_rec r = { .t = now, .type = R_SETTLE, .a = i + 1, .b = (int)v,
                             .e = (int)__atomic_load_n(&hk_raw[i], __ATOMIC_RELAXED) };
                push(&r);
                tk_logged[i] = v; tk_logged_t[i] = now;
            }
        }
    }
    if (!tk_have_fad) {
        fs_rec r = { .t = now, .type = R_NOTE, .a = 1, .b = (int)fad(w, 0), .c = (int)fad(w, 1),
                     .d = (int)fad(w, 2), .e = (int)fad(w, 3) };
        push(&r);
    }
    tk_have_fad = 1;
}

/* the mixer filter saw an edge: did onMoved see that fader move too? (hook source only) */
static uint64_t xk_pending_t[NCH];
static unsigned xk_silent;
static void tick_xcheck(uint64_t now)
{
    for (int i = 0; i < NCH; i++) {
        unsigned seq = __atomic_load_n(&mx_edge_seq[i], __ATOMIC_ACQUIRE);
        if (seq != tk_edge_seen[i]) {
            tk_edge_seen[i] = seq;
            xk_pending_t[i] = act_src() == SRC_HOOK ? __atomic_load_n(&mx_edge_t[i], __ATOMIC_RELAXED) : 0;
        }
        uint64_t et = xk_pending_t[i];
        if (!et || now - et < ms_to_ticks(500)) continue;
        xk_pending_t[i] = 0;
        uint64_t h = __atomic_load_n(&hk_last_t[i], __ATOMIC_RELAXED);
        /* 0.0.6: any onMoved call from 500 ms before the edge on counts. hk_last_t is the LATEST
         * call, so a slow move (deck run 20261115-205017: still moving 500 ms after the edge)
         * put it more than 500 ms after the edge and 0.0.5 disarmed with the hook working. */
        int seen = h && h + ms_to_ticks(500) >= et;
        if (seen) { xk_silent = 0; continue; }
        xk_silent++;
        fs_rec r = { .t = et, .type = R_XCHK, .a = i + 1, .b = (int)xk_silent };
        push(&r);
        if (xk_silent >= 2) disarm(D_ONMOVED_SILENT);
    }
}

static void tick_rate(uint64_t now)
{
    if (!tk_rate_t) { tk_rate_t = tk_rate_idle_t = now; return; }
    if (now - tk_rate_t < ms_to_ticks(RATE_WIN_MS)) return;
    unsigned frames = __atomic_load_n(&mx_frames, __ATOMIC_ACQUIRE);
    unsigned mv = __atomic_load_n(&mx_move_frames, __ATOMIC_RELAXED);
    unsigned df = frames - tk_frames0, dm = mv - tk_move0;
    /* every window with fader movement, else once a minute */
    if (dm || now - tk_rate_idle_t >= ms_to_ticks(60000)) {
        fs_rec r = { .t = now, .type = R_RATE, .a = (int)df, .b = (int)dm };
        r.x[0] = now - tk_rate_t;
        for (int m = 0; m < 2; m++)             /* 16-bit saturating counts, 4 a word */
            for (int b = 0; b < NB; b++) {
                unsigned h = __atomic_load_n(&mx_hist[m][b], __ATOMIC_RELAXED);
                unsigned dh = h - tk_hist0[m][b];
                tk_hist0[m][b] = h;
                r.x[1 + 3 * m + b / 4] |= (uint64_t)(dh > 65535u ? 65535u : dh) << (16 * (b % 4));
            }
        r.c = (int)__atomic_load_n(&led_frames, __ATOMIC_RELAXED);
        r.d = (int)deck_ev_quiet;
        push(&r);
        fs_rec q = { .t = now, .type = R_STATUS };
        q.x[0] = __atomic_load_n(&mx_frames, __ATOMIC_RELAXED);
        q.x[1] = __atomic_load_n(&erp_frames, __ATOMIC_RELAXED);
        q.x[2] = __atomic_load_n(&led_frames, __ATOMIC_RELAXED);
        for (int f = 0; f < NF; f++)
            q.x[3 + f] = (uint64_t)__atomic_load_n(&f_slow[f], __ATOMIC_RELAXED) << 32 |
                         __atomic_load_n(&f_max_us[f], __ATOMIC_RELAXED);
        q.x[6] = __atomic_load_n(&hook_calls[H_ONMOVED], __ATOMIC_RELAXED);
        q.a = (int)__atomic_load_n(&act_n, __ATOMIC_RELAXED);
        q.b = (int)__atomic_load_n(&skip_n, __ATOMIC_RELAXED);
        q.c = (int)__atomic_load_n(&hk_busy_skips, __ATOMIC_RELAXED);
        q.d = (int)__atomic_load_n(&act_max_us, __ATOMIC_RELAXED);
        push(&q);
        tk_rate_idle_t = now;
    } else {
        for (int m = 0; m < 2; m++)
            for (int b = 0; b < NB; b++) tk_hist0[m][b] = __atomic_load_n(&mx_hist[m][b], __ATOMIC_RELAXED);
    }
    tk_frames0 = frames; tk_move0 = mv; tk_rate_t = now;
}

static unsigned tk_led_frames, tk_led_silent;
static uint64_t tk_led_seen_t;
static void tick_led_heartbeat(uint64_t now)
{
    unsigned n = __atomic_load_n(&led_frames, __ATOMIC_ACQUIRE);
    if (!n) return;                         /* never started: nothing to compare */
    if (n != tk_led_frames) {
        tk_led_frames = n; tk_led_seen_t = now;
        if (tk_led_silent) { tk_led_silent = 0; fs_rec r = { .t = now, .type = R_SILENT, .a = F_LED, .b = 0 }; push(&r); }
    } else if (!tk_led_silent && now - tk_led_seen_t > ms_to_ticks(1000)) {
        /* the LED bus runs ~100 frames/s: a second of nothing = the filter is off (or the bus) */
        tk_led_silent = 1;
        fs_rec r = { .t = now, .type = R_SILENT, .a = F_LED, .b = 1 };
        r.x[0] = now - tk_led_seen_t;
        push(&r);
    }
}
static void tick_led(uint64_t now)
{
    if (!__atomic_load_n(&led_frames, __ATOMIC_ACQUIRE)) return;
    for (int s = 0; s < 2; s++) {
        uint64_t w = __atomic_load_n(&led_word[s], __ATOMIC_RELAXED);
        int changed = 0;
        for (int l = 0; l < NL; l++) {
            unsigned lv = led_level(w, l);
            if (!tl_seen) { tl_level[s][l] = lv; tl_state[s][l] = lv ? 1 : 0; changed = 1; continue; }
            if (lv != tl_level[s][l]) {
                tl_level[s][l] = lv;
                tl_prev_change[s][l] = tl_change[s][l];
                tl_change[s][l] = now;
            }
            /* one step is a steady change (logged once); a second step within BLINK_MS is a
             * blink, held until BLINK_MS passes with no step */
            int blink = tl_prev_change[s][l] && now - tl_change[s][l] < ms_to_ticks(BLINK_MS) &&
                        tl_change[s][l] - tl_prev_change[s][l] < ms_to_ticks(BLINK_MS);
            unsigned st = blink ? 2u : lv ? 1u : 0u;
            if (st != tl_state[s][l]) { tl_state[s][l] = st; changed = 1; }
        }
        if (changed) {
            fs_rec r = { .t = now, .type = R_LED, .a = s, .b = (int)__atomic_load_n(&erp_sel[s], __ATOMIC_RELAXED),
                         .c = side_loop(s), .d = side_stored(s) };
            r.x[0] = w;
            r.x[1] = 0;
            for (int l = 0; l < NL; l++) r.x[1] |= (uint64_t)tl_state[s][l] << (4 * l);
            push(&r);
        }
    }
    tl_seen = 1;
}
/* the LED loop state is usable only while the LED filter runs (deck run 2: off after 31 s) */
static void tick_led_publish(void)
{
    for (int s = 0; s < 2; s++)
        __atomic_store_n(&led_loop_pub[s], tl_seen && !tk_led_silent && tk_led_frames ? side_loop(s) : -1, __ATOMIC_RELEASE);
}

/* arming: state lines, re-arm on master off->on, a banner when disarmed while on */
static int ak_en = -1, ak_dis = -1, ak_auto = -2;
static void tick_arm(uint64_t now)
{
    int en = en_row(), dis = __atomic_load_n(&disarm_why, __ATOMIC_ACQUIRE);
    if (en && ak_en == 0 && dis) {           /* the DJ switched it on again: a fresh start */
        __atomic_store_n(&disarm_why, 0, __ATOMIC_RELEASE);
        fs_rec r = { .t = now, .type = R_ARM, .a = 2, .b = dis };
        push(&r);
        dis = 0;
    }
    /* the mixer filter as edge source: near switch-off = disarm (it can only go quiet) */
    if (act_src() == SRC_FILTER && (filters_on & 1) && __atomic_load_n(&f_slow[F_MIXER], __ATOMIC_RELAXED) >= MIXER_SLOW_DISARM) {
        disarm(D_MIXER_FILTER);
        dis = __atomic_load_n(&disarm_why, __ATOMIC_ACQUIRE);
    }
    int am = (en != ak_en) ? automix_hooked() : ak_auto;
    if (en != ak_en || dis != ak_dis || am != ak_auto) {
        fs_rec r = { .t = now, .type = R_ARM, .a = en, .b = dis, .c = am, .d = act_src(), .e = vm_ok };
        for (int d = 0; d < NCH; d++) {
            r.x[0] |= (uint64_t)(__atomic_load_n(&play_src[d], __ATOMIC_RELAXED) & 3u) << (2 * d);
            r.x[1] |= (uint64_t)(__atomic_load_n(&cue_src[d], __ATOMIC_RELAXED) & 3u) << (2 * d);
        }
        r.x[2] = bus_thread();
        push(&r);
        if (en && dis && dis != ak_dis && AZ_HAS(C, banner)) C->banner(MOD_ID, DBANNER[dis > 0 && dis < D_N ? dis : 0], 4000);
        ak_en = en; ak_dis = dis; ak_auto = am;
    }
}

static void my_tick(void)
{
    if (!__atomic_load_n(&init_done, __ATOMIC_ACQUIRE)) return;
    uint64_t now = counter();
    note_thread(X_TICK, now);
    tick_led_heartbeat(now);
    tick_led(now);
    tick_led_publish();
    tick_faders(now);
    tick_xcheck(now);
    tick_arm(now);
    tick_rate(now);
#ifdef AZ_HARNESS   /* TEST-ONLY: the harness's log() is not thread-safe, so drain on its UI thread */
    for (int n = 0; n < 8 && drain_once(); n++) {}
#endif
}

static void deck_event(int deck, int what, void *user)
{
    (void)user;
    uint64_t now = counter(), a = __atomic_load_n(&last_activity_t, __ATOMIC_RELAXED);
    note_thread(X_DECKEV, now);
    if (what == AZ_DECK_LOADED && deck >= 0 && deck < NCH) __atomic_store_n(&load_t[deck], now, __ATOMIC_RELAXED);
    if ((what == AZ_DECK_PLAY || what == AZ_DECK_STOP) && deck >= 0 && deck < NCH)
        __atomic_store_n(&deckev_t[deck], now, __ATOMIC_RELEASE);
    /* lines only around the test (5 s after a fader edge or a press); the load time is kept */
    if (!a || now - a > ms_to_ticks(5000)) { deck_ev_quiet++; return; }
    fs_rec r = { .t = now, .type = R_DECK, .a = deck, .b = what };
    push(&r);
}

/* ---- learning facade objects from physical presses (worker) ----------------------------- */
/* the one side with a press of button k within LEARN_MS before t (a few ms after allowed) */
static int press_side(uint64_t t, int k)
{
    int side = -1, n = 0;
    for (int s = 0; s < 2; s++) {
        uint64_t p = __atomic_load_n(&erp_press_t[s][k], __ATOMIC_RELAXED);
        if (!p) continue;
        if ((p <= t && t - p <= ms_to_ticks(LEARN_MS)) || (p > t && p - t <= ms_to_ticks(5))) { side = s; n++; }
    }
    return n == 1 ? side : -1;
}
static void learn_rec(int kind, int deck, int src, int result, uint64_t obj, uint64_t t)
{
    fs_rec r = { .t = t, .type = R_LEARN, .a = kind, .b = deck, .c = src, .d = result };
    r.x[0] = obj;
    push(&r);
}
/* fill the unknown Play facades from a learned one and the 0x620 spacing (vtable-checked) */
static void play_fill(int from, uint64_t t)
{
    uint64_t base = __atomic_load_n(&play_obj[from], __ATOMIC_RELAXED);
    for (int d = 0; d < NCH; d++) {
        if (__atomic_load_n(&play_src[d], __ATOMIC_RELAXED)) continue;
        uint64_t cand = base + (uint64_t)((int64_t)(d - from) * PLAY_STRIDE);
        int ok = obj_check(cand, VT_PLAYPAUSE) == 1;
        if (ok) {
            __atomic_store_n(&play_obj[d], cand, __ATOMIC_RELEASE);
            __atomic_store_n(&play_src[d], 2u, __ATOMIC_RELEASE);
        }
        learn_rec(0, d, 2, ok ? 0 : 1, cand, t);
    }
}
/* 0.0.6: a Play press on a side whose deck is unknown (no DECK SELECT press since load; deck
 * run 20261115-205017 had none all session, so 0.0.5 learned nothing and every fader edge was
 * play-obj-unknown). The press is held for PEND_MS; EP147's own PLAY/STOP deck event for exactly
 * one deck of that side (A = 1/3, B = 2/4) in that window names the deck, which also becomes the
 * side's selection until a DECK SELECT press says otherwise. None, both, or a selection that
 * now differs = not learned (logged). */
#define PEND_MS 400
static uint64_t lp_obj, lp_t;
static int lp_side, lp_on;
static void learn_deck(int cue, int d, uint64_t obj, uint64_t t);
static void learn_pending(uint64_t now)
{
    if (!lp_on) return;
    int hit = -1, n = 0;
    for (int d = lp_side; d < NCH; d += 2) {
        uint64_t v = __atomic_load_n(&deckev_t[d], __ATOMIC_ACQUIRE);
        if (!v) continue;
        int64_t dt = (int64_t)(v - lp_t);
        if (dt >= -(int64_t)ms_to_ticks(5) && dt <= (int64_t)ms_to_ticks(PEND_MS)) { hit = d; n++; }
    }
    if (n == 0) {
        if (now - lp_t > ms_to_ticks(PEND_MS + 100)) { lp_on = 0; learn_rec(0, -1, 1, 3, lp_obj, lp_t); }
        return;
    }
    lp_on = 0;
    if (n > 1) { learn_rec(0, -1, 1, 4, lp_obj, lp_t); return; }
    unsigned exp = 0;
    if (__atomic_compare_exchange_n(&erp_sel[lp_side], &exp, (unsigned)(hit + 1), 0, __ATOMIC_ACQ_REL, __ATOMIC_RELAXED)) {
        fs_rec r = { .t = lp_t, .type = R_SEL, .a = lp_side, .b = hit + 1, .c = 1 };
        push(&r);
    } else if (exp != (unsigned)(hit + 1)) {  /* a DECK SELECT press meanwhile says another deck */
        learn_rec(0, -1, 1, 4, lp_obj, lp_t);
        return;
    }
    learn_deck(0, hit, lp_obj, lp_t);
}
/* 0.0.7: was this hook call made by EP147 itself (return address in its non-PIE text)? Our own
 * loopExit calls return into this .so and must never teach objects. */
#ifdef FS_UNIT
static int fs_unit_lr_any = 1;                /* TEST-ONLY: host return addresses are not EP147's */
#define EP147_LR(lr) (fs_unit_lr_any || ((lr) >= 0x400000u && (lr) < 0x4000000u))
#else
#define EP147_LR(lr) ((lr) >= 0x400000u && (lr) < 0x4000000u)
#endif
/* 0.0.7: a CueLoop facade's deck = the one known Play facade with the same LoopState and core.
 * 1 learned (or already known), 0 not decided here (no Play facade, no or several matches). */
static int learn_cue_by_ls(uint64_t obj, uint64_t t)
{
    uint64_t ls = 0, core = 0, v2 = 0, v3 = 0;
    if (obj_check(obj, VT_CUELOOP) != 1 || read_u64((uintptr_t)(obj + 8), &v2) != 0 || v2 != VT_CUELOOP2 ||
        read_u64((uintptr_t)(obj + 0x10), &v3) != 0 || v3 != VT_CUELOOP3 ||
        read_u64((uintptr_t)(obj + CUE_LS_OFF), &ls) != 0 || read_u64((uintptr_t)(obj + CUE_CORE_OFF), &core) != 0 || !ls)
        return 0;
    int hit = -1, n = 0;
    for (int d = 0; d < NCH; d++) {
        uint64_t po = __atomic_load_n(&play_obj[d], __ATOMIC_ACQUIRE), pl = 0, pc = 0;
        if (obj_check(po, VT_PLAYPAUSE) != 1 || read_u64((uintptr_t)(po + PLAY_LS_OFF), &pl) != 0 ||
            read_u64((uintptr_t)(po + PLAY_CORE_OFF), &pc) != 0) continue;
        if (pl == ls && pc == core) { hit = d; n++; }
    }
    if (n != 1) { if (n > 1) learn_rec(1, -1, 1, 4, obj, t); return n > 1; }
    learn_deck(1, hit, obj, t);
    return 1;
}
static void learn(const fs_hev *e)
{
    if ((e->kind == H_PLAY18 || e->kind == H_LOOPEXIT) && e->thr) {
        uint64_t old = __atomic_load_n(&hook_bus_thr, __ATOMIC_RELAXED);
        if (!old) {                          /* the first one wins; a different one is logged */
            __atomic_store_n(&hook_bus_thr, e->thr, __ATOMIC_RELEASE);
        } else if (old != e->thr) {
            fs_rec r = { .t = e->t, .type = R_NOTE, .a = 2 };
            r.x[0] = old; r.x[1] = e->thr;
            push(&r);
        }
    }
    if (!EP147_LR(e->lr)) return;             /* 0.0.7: our own calls teach nothing */
    if (e->kind == H_PLAYOP && strcmp(playop_via(e->lr), "fn_1436d38") == 0) {   /* LOOP press */
        learn_cue_by_ls(e->self - LOOPOP_CUE_OFF, e->t);
        return;
    }
    int cue = e->kind == H_LOOPEXIT || e->kind == H_RELOOP;
    if (cue && learn_cue_by_ls(e->self, e->t)) return;   /* 0.0.7: deck from LoopState/core */
    if (!cue && e->kind != H_PLAY18 && !(e->kind == H_PLAYOP && playop_is_play(e->lr))) return;
    uint64_t obj = e->kind == H_PLAYOP ? e->self - 8 : e->self;
    int s = press_side(e->t, cue ? B_RELOOP : B_PLAY);
    if (s < 0) return;                       /* no single press: our own calls, tasks, CUE release */
    unsigned sel = __atomic_load_n(&erp_sel[s], __ATOMIC_RELAXED);
    if (!sel) {                              /* side's deck unknown (no DECK SELECT press yet) */
        if (cue) { learn_rec(cue, -1, 1, 3, obj, e->t); return; }
        /* 0.0.6: wait for EP147's own PLAY/STOP deck event to say which deck it was */
        if (!lp_on || lp_obj != obj || lp_side != s) { lp_obj = obj; lp_t = e->t; lp_side = s; lp_on = 1; }
        return;
    }
    learn_deck(cue, (int)sel - 1, obj, e->t);
}
static void learn_deck(int cue, int d, uint64_t obj, uint64_t t)
{
    uint64_t *tab = cue ? cue_obj : play_obj;
    unsigned *src = cue ? cue_src : play_src;
    uint64_t cur = __atomic_load_n(&tab[d], __ATOMIC_RELAXED);
    unsigned cs = __atomic_load_n(&src[d], __ATOMIC_RELAXED);
    if (cs == 1 && cur == obj) return;       /* known */
    if (obj_check(obj, cue ? VT_CUELOOP : VT_PLAYPAUSE) != 1) { learn_rec(cue, d, 1, 1, obj, t); return; }
    if (cs && cur != obj) {                  /* a press contradicts what we hold */
        learn_rec(cue, d, 1, 2, obj, t);
        disarm(D_OBJ_CONFLICT);
        for (int k = 0; k < NCH; k++)        /* forget the fill-ins; keep only presses */
            if (!cue && __atomic_load_n(&play_src[k], __ATOMIC_RELAXED) == 2) {
                __atomic_store_n(&play_src[k], 0u, __ATOMIC_RELEASE);
                __atomic_store_n(&play_obj[k], 0, __ATOMIC_RELEASE);
            }
        __atomic_store_n(&src[d], 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&tab[d], 0, __ATOMIC_RELEASE);
        return;
    }
    if (!cue)                                /* 0.0.7: the deck index must fit a full block of four */
        for (int k = 0; k < NCH; k++) {
            uint64_t cand = obj + (uint64_t)((int64_t)(k - d) * PLAY_STRIDE);
            if (obj_check(cand, VT_PLAYPAUSE) != 1) { learn_rec(0, d, 1, 6, obj, t); return; }
        }
    if (!cue)
        for (int k = 0; k < NCH; k++)        /* learned Play facades keep the 0x620 spacing */
            if (k != d && __atomic_load_n(&play_src[k], __ATOMIC_RELAXED) == 1 &&
                obj - __atomic_load_n(&play_obj[k], __ATOMIC_RELAXED) != (uint64_t)((int64_t)(d - k) * PLAY_STRIDE)) {
                learn_rec(0, d, 1, 2, obj, t);
                disarm(D_OBJ_CONFLICT);
                return;
            }
    __atomic_store_n(&tab[d], obj, __ATOMIC_RELEASE);
    __atomic_store_n(&src[d], 1u, __ATOMIC_RELEASE);
    learn_rec(cue, d, 1, 0, obj, t);
    if (!cue) play_fill(d, t);
}

/* ---- press-free CueLoop facades: a one-off memory scan (worker; read-only) ---------------
 * Once a Play facade is known and the master is on, the worker reads EP147's private anonymous
 * memory (rw-p mappings without a file, and [heap]) with process_vm_readv, at most SCAN_STEP
 * bytes per 50 ms round, looking for objects whose first three words are the CueLoop vtables
 * (0x2bc8128, 0x2bc8198, 0x2bc81c0: facade builder 0x1565b88). A hit is a deck's CueLoop facade
 * only if its LoopState (+0x68) and core (+0x20) equal that deck's Play facade's (+0x60,
 * +0x18). Exactly one such hit = taken (cue source S); none or several = unknown (fallback
 * rules). A press still teaches/overrides as before; a press contradicting a scanned object
 * disarms (CONFLICT). Never writes, never calls EP147. */
#if defined(FS_UNIT) || !defined(AZ_HARNESS)   /* the stock harness has no EP147 memory to scan */
#define SCAN_MAX_REG    384
#define SCAN_REG_MAX    (1ull << 30)       /* skip a single mapping above 1 GiB */
#define SCAN_TOTAL_MAX  (3ull << 30)       /* and stop after 3 GiB in all */
#define SCAN_STEP       (4u << 20)         /* per worker round (~50 ms): <= ~80 MB/s */
#define SCAN_CHUNK      65536u
#define SCAN_RUNS       3                  /* retries (when a deck is still unmatched) */
#define SCAN_RETRY_ROUNDS 600              /* ~30 s between runs */
typedef struct { uint64_t lo, hi; } fs_region;
static fs_region sc_reg[SCAN_MAX_REG];
static int sc_nreg, sc_ri, sc_state, sc_runs, sc_cut;   /* state 0 idle, 1 running */
static uint64_t sc_pos, sc_read, sc_bad, sc_hits, sc_total, sc_t0;
static uint64_t sc_match[NCH], sc_pls[NCH], sc_pcore[NCH];
static unsigned sc_nmatch[NCH], sc_wait;
static uint64_t sc_buf[SCAN_CHUNK / 8];

#if defined(FS_UNIT)
static const fs_region *fs_unit_regions;  /* TEST-ONLY */
static int fs_unit_nregions;
static int scan_regions(void)
{
    sc_nreg = 0;
    for (int i = 0; i < fs_unit_nregions && sc_nreg < SCAN_MAX_REG; i++) sc_reg[sc_nreg++] = fs_unit_regions[i];
    return sc_nreg;
}
static void read_block(uint64_t a, size_t n)
{
    for (size_t k = 0; k < n / 8; k++) {
        uint64_t v = 0;
        if (read_u64((uintptr_t)(a + 8 * k), &v) != 0) v = 0;
        sc_buf[k] = v;
    }
}
#else
static uint64_t hexnum(const char **p)
{
    uint64_t v = 0;
    for (;; (*p)++) {
        char c = **p;
        unsigned d = c >= '0' && c <= '9' ? (unsigned)(c - '0') : c >= 'a' && c <= 'f' ? (unsigned)(c - 'a' + 10) : 16u;
        if (d > 15) return v;
        v = v << 4 | d;
    }
}
/* "lo-hi perms offset dev inode [path]": anonymous rw-p (inode 0, no path) or [heap] */
static void maps_line(const char *l)
{
    const char *p = l;
    uint64_t lo = hexnum(&p);
    if (*p++ != '-') return;
    uint64_t hi = hexnum(&p);
    if (*p++ != ' ' || strncmp(p, "rw-p ", 5) != 0) return;
    p += 5;
    for (int f = 0; f < 2; f++) { while (*p && *p != ' ') p++; while (*p == ' ') p++; }   /* offset, dev */
    const char *ino = p;
    while (*p && *p != ' ') p++;
    if (p - ino != 1 || *ino != '0') return;
    while (*p == ' ') p++;
    if (*p && strcmp(p, "[heap]") != 0) return;
    if (hi <= lo || hi - lo > SCAN_REG_MAX || sc_nreg >= SCAN_MAX_REG) return;
    sc_reg[sc_nreg].lo = lo; sc_reg[sc_nreg].hi = hi; sc_nreg++;
}
static int scan_regions(void)
{
    static char rb[4096], line[512];
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC), ln = 0;
    sc_nreg = 0;
    if (fd < 0) return 0;
    for (;;) {
        ssize_t r = read(fd, rb, sizeof rb);
        if (r <= 0) break;
        for (ssize_t i = 0; i < r; i++) {
            if (rb[i] == '\n') { line[ln] = 0; maps_line(line); ln = 0; }
            else if (ln < (int)sizeof line - 1) line[ln++] = rb[i];
        }
    }
    if (ln) { line[ln] = 0; maps_line(line); }
    close(fd);
    return sc_nreg;
}
/* one chunk into sc_buf; unreadable pages read as zeros (and are counted) */
static void read_block(uint64_t a, size_t n)
{
    struct iovec l = { sc_buf, n }, r = { (void *)(uintptr_t)a, n };
    if (syscall(SYS_process_vm_readv, my_pid, &l, 1L, &r, 1L, 0L) == (long)n) return;
    for (size_t o = 0; o < n; o += 4096) {
        size_t k = n - o < 4096 ? n - o : 4096;
        struct iovec l1 = { (char *)sc_buf + o, k }, r1 = { (void *)(uintptr_t)(a + o), k };
        if (syscall(SYS_process_vm_readv, my_pid, &l1, 1L, &r1, 1L, 0L) != (long)k) {
            for (size_t j = 0; j < k / 8; j++) sc_buf[(o + 8 * j) / 8] = 0;
            sc_bad += k;
        }
    }
}
#endif

static void scan_hit(uint64_t a)
{
    uint64_t ls = 0, core = 0;
    sc_hits++;
    if (read_u64((uintptr_t)(a + CUE_LS_OFF), &ls) != 0 || read_u64((uintptr_t)(a + CUE_CORE_OFF), &core) != 0 || !ls) return;
    for (int d = 0; d < NCH; d++)
        if (sc_pls[d] && sc_pls[d] == ls && sc_pcore[d] == core) { sc_nmatch[d]++; sc_match[d] = a; }
}
static void scan_chunk(uint64_t a, size_t n)
{
    size_t nw = n / 8;
    for (size_t k = 0; k < nw; k++) {
        if (sc_buf[k] != VT_CUELOOP) continue;
        uint64_t v2 = 0, v3 = 0;
        if (k + 2 < nw) { v2 = sc_buf[k + 1]; v3 = sc_buf[k + 2]; }
        else if (read_u64((uintptr_t)(a + 8 * k + 8), &v2) != 0 || read_u64((uintptr_t)(a + 8 * k + 16), &v3) != 0) continue;
        if (v2 == VT_CUELOOP2 && v3 == VT_CUELOOP3) scan_hit(a + 8 * k);
    }
}
static void scan_finish(uint64_t now)
{
    fs_rec r = { .t = now, .type = R_SCAN, .a = 1, .b = sc_cut, .c = sc_runs };
    r.x[0] = sc_read; r.x[1] = sc_bad; r.x[2] = sc_hits; r.x[4] = now - sc_t0;
    for (int d = 0; d < NCH; d++) r.x[3] |= (uint64_t)(sc_nmatch[d] > 255 ? 255 : sc_nmatch[d]) << (8 * d);
    push(&r);
    for (int d = 0; d < NCH; d++) {
        if (!sc_nmatch[d]) continue;
        unsigned cs = __atomic_load_n(&cue_src[d], __ATOMIC_RELAXED);
        uint64_t cur = __atomic_load_n(&cue_obj[d], __ATOMIC_RELAXED);
        if (sc_nmatch[d] > 1) { if (!cs) learn_rec(1, d, 3, 4, sc_match[d], now); continue; }
        if (cs) { if (cur != sc_match[d]) learn_rec(1, d, 3, 5, sc_match[d], now); continue; }
        if (obj_check(sc_match[d], VT_CUELOOP) != 1) { learn_rec(1, d, 3, 1, sc_match[d], now); continue; }
        __atomic_store_n(&cue_obj[d], sc_match[d], __ATOMIC_RELEASE);
        __atomic_store_n(&cue_src[d], 3u, __ATOMIC_RELEASE);
        learn_rec(1, d, 3, 0, sc_match[d], now);
    }
    sc_state = 0;
    sc_wait = SCAN_RETRY_ROUNDS;
}
/* one worker round of the scan (start, continue, or finish) */
static void scan_step(void)
{
    if (!FS_SCAN || !vm_ok) return;          /* 0.0.7: off unless built with FS_SCAN=1 */
    uint64_t now = counter();
    if (!sc_state) {
        if (sc_wait) { sc_wait--; return; }
        if (sc_runs >= SCAN_RUNS || !en_row()) return;
        int need = 0;
        for (int d = 0; d < NCH; d++) {
            uint64_t po = __atomic_load_n(&play_obj[d], __ATOMIC_ACQUIRE);
            sc_pls[d] = sc_pcore[d] = sc_match[d] = 0; sc_nmatch[d] = 0;
            if (obj_check(po, VT_PLAYPAUSE) != 1 || read_u64((uintptr_t)(po + PLAY_LS_OFF), &sc_pls[d]) != 0 ||
                read_u64((uintptr_t)(po + PLAY_CORE_OFF), &sc_pcore[d]) != 0) { sc_pls[d] = sc_pcore[d] = 0; continue; }
            if (!__atomic_load_n(&cue_src[d], __ATOMIC_RELAXED)) need = 1;
        }
        if (!need) return;
        sc_runs++;
        if (scan_regions() <= 0) { sc_wait = SCAN_RETRY_ROUNDS; return; }
        uint64_t total = 0;
        for (int i = 0; i < sc_nreg; i++) total += sc_reg[i].hi - sc_reg[i].lo;
        sc_ri = 0; sc_pos = sc_reg[0].lo; sc_read = sc_bad = sc_hits = sc_total = 0; sc_cut = 0; sc_t0 = now;
        fs_rec r = { .t = now, .type = R_SCAN, .a = 0, .b = sc_nreg, .c = sc_runs };
        r.x[0] = total;
        push(&r);
        sc_state = 1;
        return;
    }
    uint64_t done = 0, bl = (uint64_t)(uintptr_t)sc_buf, bh = bl + sizeof sc_buf;
    while (done < SCAN_STEP && sc_ri < sc_nreg) {
        if (sc_pos >= sc_reg[sc_ri].hi) { if (++sc_ri < sc_nreg) sc_pos = sc_reg[sc_ri].lo; continue; }
        if (sc_total >= SCAN_TOTAL_MAX) { sc_cut = 1; break; }
        uint64_t n = sc_reg[sc_ri].hi - sc_pos;
        if (n > SCAN_CHUNK) n = SCAN_CHUNK;
        if (sc_pos < bh && sc_pos + n > bl) { sc_pos += n; continue; }   /* our own copy buffer */
        read_block(sc_pos, (size_t)n);
        scan_chunk(sc_pos, (size_t)n);
        sc_pos += n; sc_read += n; sc_total += n; done += n;
    }
    if (sc_ri >= sc_nreg || sc_cut) scan_finish(now);
}
#endif

/* ---- worker: the only logger ----------------------------------------------------------- */
#define OWN_BUDGET (160u * 1024u)
typedef struct { uint64_t last; unsigned tokens_x16, dropped; int init; } fs_bucket;
static fs_bucket bucket, pbucket;   /* bulk lines; priority lines (actions, hooks, presses) */
static unsigned w_bytes, w_budget_noted, w_dropped_reported, w_gated, w_gated_reported;
static int am_take(fs_bucket *b, uint64_t now, unsigned rate, unsigned burst)
{
    if (!b->init) { b->init = 1; b->last = now; b->tokens_x16 = burst * 16u; }
    uint64_t el = now > b->last ? now - b->last : 0, step = hz / (rate * 16u);
    if (step && el >= step) {
        uint64_t add = el / step;
        b->last += add * step;
        uint64_t tk = b->tokens_x16 + add;
        b->tokens_x16 = tk > burst * 16u ? burst * 16u : (unsigned)tk;
    }
    if (b->tokens_x16 < 16u) { b->dropped++; return 0; }
    b->tokens_x16 -= 16u;
    return 1;
}
static int logging_on(void)
{
    return h_log < 0 || !AZ_HAS(C, row_value) || C->row_value(h_log) == 1;
}
/* `now` is the record's own time: the worker never reads a clock */
static void emit(const char *line, int m, int prio, uint64_t now)
{
    if (m < 0) return;
    if (!logging_on()) { w_gated++; return; }
    if (w_gated != w_gated_reported && w_bytes + 112 < OWN_BUDGET) {
        /* 0.0.6: deck run 20261115-205017 lost every line before t=113 s; say when that happens */
        char g[112];
        int k = snprintf(g, sizeof g, "LOGGATE %u lines dropped while the Logging row read off (first line after: t=%ldms)",
                         w_gated - w_gated_reported, rel_ms(now));
        w_gated_reported = w_gated;
        C->log(MOD_ID, "%s", g);
        w_bytes += (unsigned)k;
    }
    if (prio ? !am_take(&pbucket, now, 20, 64) : !am_take(&bucket, now, 4, 16)) return;
    if (bucket.dropped + pbucket.dropped != w_dropped_reported && w_bytes + 112 < OWN_BUDGET) {
        char b[112];
        int k = snprintf(b, sizeof b, "RATE dropped=%u prio_dropped=%u qlost=%u hooklost=%u actlost=%u", bucket.dropped,
                         pbucket.dropped, __atomic_load_n(&Q.lost, __ATOMIC_RELAXED),
                         __atomic_load_n(&hr_lost, __ATOMIC_RELAXED), __atomic_load_n(&ar_lost, __ATOMIC_RELAXED));
        w_dropped_reported = bucket.dropped + pbucket.dropped;
        C->log(MOD_ID, "%s", b);
        w_bytes += (unsigned)k;
    }
    if (w_bytes + (unsigned)m >= OWN_BUDGET) {
        if (!w_budget_noted) { w_budget_noted = 1; C->log(MOD_ID, "BUDGET own 160KiB reached; later lines dropped"); }
        return;
    }
    C->log(MOD_ID, "%s", line);
    w_bytes += (unsigned)m;
}

static const char *side_name(int s) { return s ? "B" : "A"; }
/* the button press nearest t (either side, any of the six buttons), for hook correlation */
static int near_press(uint64_t t, char *b, size_t n)
{
    int bs = -1, bk = -1;
    uint64_t bd = 0;
    int64_t sd = 0;
    for (int s = 0; s < 2; s++)
        for (int k = 0; k < 6; k++) {
            uint64_t p = __atomic_load_n(&erp_press_t[s][k], __ATOMIC_RELAXED);
            if (!p) continue;
            uint64_t d = p > t ? p - t : t - p;
            if (bs < 0 || d < bd) { bs = s; bk = k; bd = d; sd = (int64_t)(t - p); }
        }
    if (bs < 0 || bd > ms_to_ticks(2000)) return snprintf(b, n, "near=none");
    return snprintf(b, n, "near=%s:%s dt=%+ldms", side_name(bs), BTN[bk], (long)(sd / (int64_t)(hz / 1000u)));
}
static const char *const DECKEV[] = { "?", "LOADED", "EJECTED", "PLAY", "STOP" };
static const char SRCCH[] = "-LFS";          /* none, press, 0x620 fill-in, memory scan */
static const char *const LOOPSRC[] = { "-", "hasloop", "led", "hasloop+led" };
static const char *const INNAME[] = { "unknown", "n/a", "other", "deck" };

static int format_act(const fs_aev *e, char *b, size_t n)
{
    uint64_t w = e->w0;
    int ch = (int)(w & 15u), src = (int)(w >> 4 & 15u), act = (int)(w >> 8 & 0xffu), rs = (int)(w >> 16 & 0xffu);
    int loaded = (int)(w >> 24 & 15u) - 1, playing = (int)(w >> 28 & 15u) - 1, loop = (int)(w >> 32 & 15u) - 1;
    int lsrc = (int)(w >> 36 & 15u), input = (int)(w >> 40 & 15u), would = (int)(w >> 44 & 15u);
    int wrs = (int)(w >> 48 & 0xffu), exit_on = (int)(w >> 56 & 1u), ps = (int)(w >> 58 & 3u), cs = (int)(w >> 60 & 3u);
    if (act > 3) act = 0;
    if (would > 3) would = 0;
    if (rs >= RS_N) rs = 0;
    if (wrs >= RS_N) wrs = 0;
    if (lsrc > 3) lsrc = 0;
    if (input > 3) input = 0;
    char calls[48] = "", wl[48] = "";
    unsigned ue = (unsigned)(e->w2 & 0xffffffffu), up = (unsigned)(e->w2 >> 32);
    if (act != A_NONE) {
        int k = 0;
        k += snprintf(calls + k, sizeof calls - (size_t)k, " call_us");
        if (ue != NOCALL) k += snprintf(calls + k, sizeof calls - (size_t)k, " loopExit=%u", ue);
        if (up != NOCALL) snprintf(calls + k, sizeof calls - (size_t)k, " play=%u", up);
    }
    if (rs == RS_OFF || rs == RS_DISARMED)
        snprintf(wl, sizeof wl, " would=%s%s%s", ANAME[would], would ? "" : "/", would ? "" : RSNAME[wrs]);
    return snprintf(b, n, "%s ch=%d src=%s action=%s reason=%s%s%s deck=%d loaded=%d playing=%d loop=%d(%s) exitloop=%d input=%s closed_ms=%lu v=%lu play_obj=%#lx(%c) cue_obj=%#lx(%c) thr=%#lx t=%ldms",
                    act ? "ACT" : "SKIP", ch + 1, src ? "filter" : "hook", ANAME[act], RSNAME[rs], wl, calls, ch + 1, loaded,
                    playing, loop, LOOPSRC[lsrc], exit_on, INNAME[input], (unsigned long)(e->w1 & 0xffffffffu),
                    (unsigned long)(e->w1 >> 32), (unsigned long)e->w3, SRCCH[ps], (unsigned long)e->w4, SRCCH[cs],
                    (unsigned long)e->w5, rel_ms(e->t));
}

static int format_rec(const fs_rec *r, char *b, size_t n)
{
    long t = rel_ms(r->t);
    switch (r->type) {
    case R_FADER:
        return snprintf(b, n, "FADER ch=%d v=%d from=%d xf=%d hook_raw=%d t=%ldms", r->a, r->b, r->c, r->d, r->e, t);
    case R_SETTLE:
        return snprintf(b, n, "FADER_SETTLE ch=%d v=%d hook_raw=%d t=%ldms", r->a, r->b, r->e, t);
    case R_RATE: {
        char h[2][64];
        for (int m = 0; m < 2; m++) {
            int k = 0;
            for (int i = 0; i < NB && k < (int)sizeof h[m]; i++)
                k += snprintf(h[m] + k, sizeof h[m] - (size_t)k, "%s%u", i ? "/" : "",
                              (unsigned)(r->x[1 + 3 * m + i / 4] >> (16 * (i % 4)) & 0xffffu));
        }
        double secs = (double)r->x[0] / (double)hz;
        return snprintf(b, n, "MIXRATE win=%.1fs frames=%d fps=%.1f moving_frames=%d led_frames=%d deck_events_quiet=%d dt_idle[<1/<2/<5/<10/<20/<50/<100/<500/>=500ms]=%s dt_moving=%s t=%ldms",
                        secs, r->a, secs > 0 ? r->a / secs : 0.0, r->b, r->c, r->d, h[0], h[1], t);
    }
    case R_LED: {
        uint64_t s = r->x[1];
        char sel[4];
        if (r->b) snprintf(sel, sizeof sel, "%d", r->b); else snprintf(sel, sizeof sel, "?");
        return snprintf(b, n, "LED side=%s sel=%s play%c cue%c in%c out%c reloop%c jog=%u loop=%d stored=%d t=%ldms",
                        side_name(r->a), sel, ST[s & 3], ST[s >> 4 & 3], ST[s >> 8 & 3], ST[s >> 12 & 3],
                        ST[s >> 16 & 3], (unsigned)(r->x[0] >> 36 & 3), r->c, r->d, t);
    }
    case R_SEL:
        return snprintf(b, n, "DECK_SELECT side=%s deck=%d%s t=%ldms", side_name(r->a), r->b,
                        r->c == 1 ? " (from EP147's PLAY/STOP deck event)" : "", t);
    case R_PRESS:
        return snprintf(b, n, "PRESS side=%s btn=%s %s shift=%d sel=%d t=%ldms", side_name(r->a), BTN[r->b],
                        r->c ? "down" : "up", r->d, r->e, t);
    case R_DECK:
        return snprintf(b, n, "DECK_EVENT deck=%d %s t=%ldms", r->a + 1, DECKEV[r->b >= 1 && r->b <= 4 ? r->b : 0], t);
    case R_THREAD:
        return snprintf(b, n, "THREAD %s tpidr=%#llx tid=%d t=%ldms", XNAME[r->a >= 0 && r->a < NX ? r->a : 0],
                        (unsigned long long)r->x[0], r->b, t);
    case R_SLOW:
        return snprintf(b, n, "FILTER_SLOW %s took=%luus count=%d%s t=%ldms", FNAME[r->a >= 0 && r->a < NF ? r->a : 0],
                        (unsigned long)r->x[0], r->b,
                        r->b >= 8 ? " FILTER_LIKELY_OFF (the core switches a filter off after 8 calls over 200 us)" : "", t);
    case R_SILENT:
        if (r->b)
            return snprintf(b, n, "FILTER_SILENT %s for %lums (switched off by the core?): LED loop state unusable", FNAME[r->a >= 0 && r->a < NF ? r->a : 0],
                            (unsigned long)(r->x[0] / (hz / 1000u)));
        return snprintf(b, n, "FILTER_BACK %s t=%ldms", FNAME[r->a >= 0 && r->a < NF ? r->a : 0], t);
    case R_STATUS:
        return snprintf(b, n, "FILTERS frames mixer=%llu erp=%llu led=%llu slow/max_us mixer=%u/%u erp=%u/%u led=%u/%u onMoved_calls=%llu actions=%d skips=%d busy=%d act_max_us=%d t=%ldms",
                        (unsigned long long)r->x[0], (unsigned long long)r->x[1], (unsigned long long)r->x[2],
                        (unsigned)(r->x[3] >> 32), (unsigned)r->x[3], (unsigned)(r->x[4] >> 32), (unsigned)r->x[4],
                        (unsigned)(r->x[5] >> 32), (unsigned)r->x[5], (unsigned long long)r->x[6], r->a, r->b, r->c, r->d, t);
    case R_NOTE:
        if (r->a == 2)
            return snprintf(b, n, "NOTE EP147 facade call on another thread: kept %#llx, saw %#llx t=%ldms",
                            (unsigned long long)r->x[0], (unsigned long long)r->x[1], t);
        return snprintf(b, n, "MIXER first frame: faders=%d/%d/%d/%d (arming needs %dms closed <=%d; open >=%d)",
                        r->b, r->c, r->d, r->e, ARM_MS, CLOSED_MAX, OPEN_MIN);
    case R_LEARN: {
        static const char *const RES[] = { "ok", "vtable-mismatch", "CONFLICT", "side-deck-unknown", "ambiguous",
                                           "already-known-differs", "block-mismatch" };
        return snprintf(b, n, "%s %s deck=%d obj=%#llx %s t=%ldms", r->c == 2 ? "FILL" : r->c == 3 ? "SCAN" : "LEARN",
                        r->a ? "cue" : "play", r->b + 1, (unsigned long long)r->x[0], RES[r->d >= 0 && r->d < 7 ? r->d : 0], t);
    }
    case R_ARM: {
        if (r->a == 2)
            return snprintf(b, n, "ARM re-armed by the master row (was disarmed: %s) t=%ldms", DNAME[r->b > 0 && r->b < D_N ? r->b : 0], t);
        char po[8], co[8];
        for (int d = 0; d < NCH; d++) { po[d] = SRCCH[r->x[0] >> (2 * d) & 3u]; co[d] = SRCCH[r->x[1] >> (2 * d) & 3u]; }
        po[4] = co[4] = 0;
        char pb[24] = "";
        if (r->b == D_POS_RANGE) snprintf(pb, sizeof pb, " pos=%#010x", (unsigned)__atomic_load_n(&hk_bad_bits, __ATOMIC_RELAXED));
        return snprintf(b, n, "ARM state=%s%s%s%s src=%s automix_hook=%s safe_read=%d play_objs=%s cue_objs=%s bus_thr=%#llx t=%ldms",
                        !r->a ? "off" : r->b ? "DISARMED" : "armed", r->b ? " reason=" : "", r->b ? DNAME[r->b > 0 && r->b < D_N ? r->b : 0] : "",
                        pb, r->d == SRC_HOOK ? "onMoved-hook" : "mixer-filter", r->c < 0 ? "unreadable" : r->c ? "YES" : "no", r->e,
                        po, co, (unsigned long long)r->x[2], t);
    }
    case R_XCHK:
        return snprintf(b, n, "XCHK ch=%d mixer edge without an onMoved call (%d in a row) t=%ldms", r->a, r->b, t);
    case R_SCAN:
        if (!r->a)
            return snprintf(b, n, "SCAN cueloop start: regions=%d bytes=%llu run=%d t=%ldms", r->b,
                            (unsigned long long)r->x[0], r->c, t);
        return snprintf(b, n, "SCAN cueloop done: run=%d read=%llu unreadable=%llu vtable_hits=%llu matched=%u/%u/%u/%u%s took=%lums t=%ldms",
                        r->c, (unsigned long long)r->x[0], (unsigned long long)r->x[1], (unsigned long long)r->x[2],
                        (unsigned)(r->x[3] & 0xffu), (unsigned)(r->x[3] >> 8 & 0xffu), (unsigned)(r->x[3] >> 16 & 0xffu),
                        (unsigned)(r->x[3] >> 24 & 0xffu), r->b ? " capped" : "",
                        (unsigned long)(r->x[4] / (hz / 1000u)), t);
    }
    return snprintf(b, n, "UNKNOWN type=%d", r->type);
}
static int format_hook(const fs_hev *e, char *b, size_t n)
{
    char near[96];
    near_press(e->t, near, sizeof near);
    long t = rel_ms(e->t);
    unsigned k = e->kind < H_N ? e->kind : 0;
    switch (k) {
    case H_PLAY18:
        return snprintf(b, n, "HOOK play18 (slot+0x18) this=%#lx start=%u jog=%#lx slip=%u lr=%#lx %s thr=%#lx t=%ldms",
                        (unsigned long)e->self, (unsigned)e->a1, (unsigned long)e->a2, (unsigned)(e->a3 & 0xffu),
                        (unsigned long)e->lr, near, (unsigned long)e->thr, t);
    case H_LOOPEXIT:
    case H_RELOOP:
        return snprintf(b, n, "HOOK %s this=%#lx arg=%u lr=%#lx %s thr=%#lx t=%ldms", HNAME[k],
                        (unsigned long)e->self, (unsigned)e->a1, (unsigned long)e->lr, near, (unsigned long)e->thr, t);
    case H_PLAYOP:
        return snprintf(b, n, "HOOK playop via=%s this=%#lx a1=%d a2=%d a3=%u lr=%#lx %s thr=%#lx t=%ldms",
                        playop_via(e->lr), (unsigned long)e->self, (int)e->a1, (int)e->a2,
                        (unsigned)(e->a3 & 0xffu), (unsigned long)e->lr, near, (unsigned long)e->thr, t);
    case H_ONMOVED:
        return snprintf(b, n, "HOOK onMoved first call: this=%#lx ch=%d pos=%#010x raw=%d thr=%#lx bus_thr=%#lx t=%ldms",
                        (unsigned long)e->self, (int)e->a1 + 1, (unsigned)e->a3, (int)(uint32_t)e->a2, (unsigned long)e->thr,
                        (unsigned long)bus_thread(), t);
    case H_ONAIR:
        return snprintf(b, n, "ONAIR ch_raw=%d on=%u this=%#lx lr=%#lx %s t=%ldms", (int)e->a1, (unsigned)(e->a2 & 0xffu),
                        (unsigned long)e->self, (unsigned long)e->lr, near, t);
    }
    return 0;
}
/* LOOPDIAG: log-only dump of the loop candidates at each edge (pushed before its ACT/SKIP) */
static int format_diag(const fs_dev *e, char *b, size_t n)
{
    uint64_t m = e->v[DV_META];
    int ch = (int)(m & 15u), d = (int)(m >> 4 & 15u), has = (int)(m >> 8 & 15u) - 1, led = (int)(m >> 12 & 15u) - 1;
    unsigned sel = (unsigned)(m >> 16 & 15u), ls = (unsigned)(m >> 20 & 15u), mask = (unsigned)(m >> 32);
    if (ls > 3) ls = 0;
    const unsigned long long *v = (const unsigned long long *)e->v;
    return snprintf(b, n, "LOOPDIAG ch=%d deck=%d hasloop=%d led_loop=%d sel=%u src=%s ls=%#llx ls_cue=%#llx core=%#llx/%#llx"
                    " ls08=%016llx ls10=%016llx lc30=%016llx lc38=%016llx lc40=%016llx ls368=%016llx ls370=%016llx"
                    " p48_98=%016llx p50_08=%016llx p50_60=%016llx p58_78=%016llx led=%016llx unread=%#x t=%ldms",
                    ch + 1, d + 1, has, led, sel, LOOPSRC[ls], v[DV_LSP], v[DV_LSC], v[DV_COREP], v[DV_COREC],
                    v[DV_LS08], v[DV_LS10], v[DV_LC30], v[DV_LC38], v[DV_LC40], v[DV_LS368], v[DV_LS370],
                    v[DV_O48_98], v[DV_O50_08], v[DV_O50_60], v[DV_O58_78], v[DV_LED], mask, rel_ms(e->t));
}
static int drain_once(void)
{
    char b[600];
    int did = 0;
    learn_pending(counter());
    fs_dev dv;
    if (d_pop(&dv)) { int m = format_diag(&dv, b, sizeof b); emit(b, m >= (int)sizeof b ? (int)sizeof b - 1 : m, 1, dv.t); did = 1; }
    fs_aev a;
    if (a_pop(&a)) { int m = format_act(&a, b, sizeof b); emit(b, m >= (int)sizeof b ? (int)sizeof b - 1 : m, 1, a.t); did = 1; }
    fs_hev e;
    if (h_pop(&e)) {
        learn(&e);
        int m = format_hook(&e, b, sizeof b);
        emit(b, m >= (int)sizeof b ? (int)sizeof b - 1 : m, 1, e.t);
        did = 1;
    }
    fs_rec r;
    if (q_pop(&Q, &r)) {
        int m = format_rec(&r, b, sizeof b), prio = r.type != R_FADER && r.type != R_SETTLE && r.type != R_RATE &&
                                                     r.type != R_LED && r.type != R_DECK;
        emit(b, m >= (int)sizeof b ? (int)sizeof b - 1 : m, prio, r.t);
        did = 1;
    }
    return did;
}
static unsigned w_gen;
static void *worker(void *arg)
{
    unsigned gen = (unsigned)(uintptr_t)arg;
    note_thread(X_WORKER, t0);
    while (!__atomic_load_n(&init_done, __ATOMIC_ACQUIRE)) {
        struct timespec ts = { 0, 20000000 };
        nanosleep(&ts, NULL);
    }
#ifndef AZ_HARNESS
    {
        char b[320];
        int m = snprintf(b, sizeof b, "fader-start " MOD_VERSION " ACTUATING (master row, default off): hooks claimed=%#x refused=%#x conflict=%#x sig_failed=%#x (bits play18/loopExit/reloop/playop/onMoved/onair) filters=%#x (mixer/erp/led) safe_read=%d edge_src=%s",
                         claimed, refused, conflicted, sig_failed, (unsigned)filters_on, vm_ok,
                         act_src() == SRC_HOOK ? "onMoved-hook" : "mixer-filter");
        emit(b, m, 1, t0);
    }
#endif
    az_deck_info di = { .size = sizeof di };
    while (__atomic_load_n(&w_gen, __ATOMIC_ACQUIRE) == gen) {
#ifndef AZ_HARNESS
        int n = 0;
        while (n < 8 && drain_once()) n++;
        scan_step();
#endif
        if (AZ_HAS(C, deck_info)) C->deck_info(0, &di);   /* a core call each round (harness reload) */
        struct timespec ts = { 0, 50000000 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

/* ---- page ------------------------------------------------------------------------------ */
static void on_draw(az_draw *d, az_rect a, void *user)
{
    (void)user;
    C->draw_header(d, "Fader Start");
    int row = C->row_height(d), px = row / 2;
    if (px < 8) px = 8;
    if (px > 96) px = 96;
    char l[5][112], po[5], co[5];
    int dis = __atomic_load_n(&disarm_why, __ATOMIC_ACQUIRE);
    for (int i = 0; i < NCH; i++) {
        po[i] = SRCCH[__atomic_load_n(&play_src[i], __ATOMIC_RELAXED) & 3u];
        co[i] = SRCCH[__atomic_load_n(&cue_src[i], __ATOMIC_RELAXED) & 3u];
    }
    po[4] = co[4] = 0;
    uint64_t w = __atomic_load_n(&mx_fad, __ATOMIC_RELAXED);
    snprintf(l[0], sizeof l[0], "%s%s", !en_row() ? "OFF (logs what it would do)" : dis ? "DISARMED: " : "ARMED",
             en_row() && dis ? DNAME[dis > 0 && dis < D_N ? dis : 0] : "");
    snprintf(l[1], sizeof l[1], "Decks 1-4 Play %s  Loop %s  (L press, F spacing, S scan)", po, co);
    snprintf(l[2], sizeof l[2], "Edges: %s  actions %u  skips %u", act_src() == SRC_HOOK ? "onMoved hook" : "mixer filter",
             __atomic_load_n(&act_n, __ATOMIC_RELAXED), __atomic_load_n(&skip_n, __ATOMIC_RELAXED));
    snprintf(l[3], sizeof l[3], "Faders %u/%u/%u/%u", fad(w, 0), fad(w, 1), fad(w, 2), fad(w, 3));
    snprintf(l[4], sizeof l[4], "Log: /tmp/azmod-community/fader-start.log");
    int y = a.y + row;
    for (int i = 0; i < 5; i++) {
        if (y + row > a.y + a.h) return;
        int lpx = px;
        if (AZ_HAS(C, text_width))
            while (lpx > 8 && C->text_width(l[i], lpx) > a.w - row / 2) lpx--;
        if (AZ_HAS(C, text_width) && C->text_width(l[i], lpx) > a.w - row / 2) { y += row; continue; }
        C->draw_text(d, a.x + row / 4, y + row / 4, l[i], lpx, C->color(AZ_COL_TEXT));
        y += row;
    }
}

/* ---- init ------------------------------------------------------------------------------ */
#if defined(__aarch64__) && !defined(AZ_HARNESS) && !defined(FS_UNIT)
static int sig_ok(const struct hook_def *h)
{
    const uint32_t *p = (const uint32_t *)h->addr;
    for (int i = 0; i < 4; i++) if (p[i] != h->sig[i]) return 0;
    return 1;
}
#else
static int sig_ok(const struct hook_def *h) { (void)h; return 0; }  /* never read EP147 on a host */
#endif

static void install_hooks(int want_onair)
{
    C->log(MOD_ID, "NOT HOOKED play slot+0x10 %#x (owned by %s): called only, after its first word is checked",
           VA_PLAY10, conflict_owner(VA_PLAY10));
    for (unsigned i = 0; i < H_N; i++) {
        const struct hook_def *h = &HOOKS[i];
        if (i == H_ONAIR && !want_onair) continue;
        const char *own = conflict_owner(h->addr);
        if (own) { conflicted |= 1u << i; C->log(MOD_ID, "CONFLICT %s %#lx owned by %s: not claimed", h->name, (unsigned long)h->addr, own); continue; }
        if (!sig_ok(h)) { sig_failed |= 1u << i; C->log(MOD_ID, "SIG mismatch %s %#lx: not claimed", h->name, (unsigned long)h->addr); continue; }
        if (C->claim_hook(MOD_ID, h->addr, h->fn, h->orig) != 0) { refused |= 1u << i; continue; }
        __atomic_fetch_or(&claimed, 1u << i, __ATOMIC_RELEASE);
    }
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    __atomic_store_n(&init_done, 0, __ATOMIC_RELEASE);   /* a harness reload re-runs init */
    C = core;
    if (!AZ_HAS(C, fw_tag) || !AZ_HAS(C, thread_start)) { C->log(MOD_ID, "needs fw_tag/thread_start"); return 2; }
#ifndef AZ_HARNESS
    if (!AZ_HAS(C, claim_hook) || !AZ_HAS(C, frame_filter)) { C->log(MOD_ID, "needs claim_hook/frame_filter"); return 2; }
#endif
    const char *tag = C->fw_tag();
#ifdef AZ_HARNESS   /* TEST-ONLY: the stock harness reports a fake tag */
    tag = FW_TAG;
#endif
    if (!tag || strcmp(tag, FW_TAG) != 0) {
        C->log(MOD_ID, "wrong firmware tag %s (want %s)", tag ? tag : "?", FW_TAG);
        return 3;
    }
    hz = frequency();
    if (hz < 1000000u) return 3;
    t0 = counter();
    claimed = refused = conflicted = sig_failed = 0;
    filters_on = 0;
    __atomic_store_n(&disarm_why, 0, __ATOMIC_RELEASE);
    probe_vm();

    if (AZ_HAS(C, register_row)) {
        const az_row_v1 g = { .size = sizeof g, .key = "fs", .label = "Fader Start", .kind = AZ_ROW_GROUP,
                              .help = "Channel fader up starts that deck (loop exited first). Test build." };
        const char *grp = C->register_row(MOD_ID, &g) >= 0 ? "fs" : NULL;
        const az_row_v1 en = { .size = sizeof en, .key = "enable", .label = "Fader Start", .parent = grp,
                               .kind = AZ_ROW_TOGGLE,
                               .help = "Off by default. On: a closed channel fader brought up plays its deck." };
        h_en = C->register_row(MOD_ID, &en);
        const az_row_v1 ex = { .size = sizeof ex, .key = "exitloop", .label = "Exit loop on fader up", .parent = grp,
                               .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
                               .help = "If the deck is looping: exit the loop, then play if paused. Never reloops." };
        h_exit = C->register_row(MOD_ID, &ex);
        const az_row_v1 lg = { .size = sizeof lg, .key = "logging", .label = "Logging", .parent = grp,
                               .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
                               .help = "Write lines to /tmp/azmod-community/fader-start.log (needs mod logs on)." };
        h_log = C->register_row(MOD_ID, &lg);
        const az_row_v1 oa = { .size = sizeof oa, .key = "onair-hook", .label = "On-Air hook (Reload)", .parent = grp,
                               .kind = AZ_ROW_TOGGLE,
                               .help = "Pass-through hook on onAirStateChanged 0x10ac848 for timing. After Reload." };
        h_onair = C->register_row(MOD_ID, &oa);
        n_rows = (grp != NULL) + (h_en >= 0) + (h_exit >= 0) + (h_log >= 0) + (h_onair >= 0);
    }
    {
        static az_screen_v1 scr;
        scr = (az_screen_v1){ .size = sizeof scr, .on_draw = on_draw };
        if (C->register_screen(MOD_ID, &scr) != 0) C->log(MOD_ID, "register_screen refused");
    }
    if (AZ_HAS(C, on_deck_event) && C->on_deck_event(MOD_ID, deck_event, NULL) != 0)
        C->log(MOD_ID, "on_deck_event refused");
    unsigned gen = __atomic_add_fetch(&w_gen, 1, __ATOMIC_ACQ_REL);   /* ends a previous worker */
    if (C->thread_start(MOD_ID, "fader-start-log", worker, (void *)(uintptr_t)gen) != 0) {
        C->log(MOD_ID, "thread_start refused");
        return 4;
    }
#ifdef AZ_HARNESS   /* TEST-ONLY: no hooks under the harness (claim_hook fails the api gate) */
    (void)install_hooks; (void)my_play18; (void)my_loopexit; (void)my_reloop; (void)my_playop; (void)my_onmoved;
    (void)my_onair;
#else
    install_hooks(h_onair >= 0 && AZ_HAS(C, row_value) && C->row_value(h_onair) == 1);
#endif
    /* filters after the hooks: the mixer filter must know whether onMoved is the edge source */
    if (AZ_HAS(C, frame_filter)) {
        if (C->frame_filter(MOD_ID, (1u << AZ_BUS_MIXER_IN) | AZ_FILTER_RAW, on_mixer, NULL) == 0) filters_on |= 1;
        if (C->frame_filter(MOD_ID, (1u << AZ_BUS_ERP_IN) | AZ_FILTER_RAW, on_erp, NULL) == 0) filters_on |= 2;
        if (C->frame_filter(MOD_ID, 1u << AZ_BUS_ERP_LED, on_led, NULL) == 0) filters_on |= 4;
    }
    C->log(MOD_ID, "loaded " MOD_VERSION " ACTUATING (Fader Start row %s); rows=%d filters=%#x hooks claimed=%#x refused=%#x conflict=%#x sig=%#x safe_read=%d",
           en_row() ? "ON" : "off", n_rows, (unsigned)filters_on, claimed, refused, conflicted, sig_failed, vm_ok);
    __atomic_store_n(&init_done, 1, __ATOMIC_RELEASE);
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
