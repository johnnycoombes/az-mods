/* SPDX-License-Identifier: GPL-3.0-only
 *
 * active-loop 0.2.7 — Active Loop Picker for EP147 (XDJ-AZ 2.00/2.01).
 *
 * 0.2.7: frame filter switched off by the core: say what is really lost (only the SHIFT
 *        combos; auto-arm, engage and Auto-advance run on hooks and keep working), show it
 *        on the page; one-shot playhead diagnostics per armed loop (approaching / not moving).
 * 0.2.6: /proc/self/maps read once in init (O_CLOEXEC), never on the tick path.
 * 0.2.5: no build for a library row without its audio file; build retries stop after 60 s.
 *
 * 0.2.4 (vs 0.2.3 emulator):
 *  - rekordbox flags counted at parse ("ANLZ parsed: ... flagged"), independent of templates.
 *  - Loops build with no loop cue anywhere: base = a firmware-constructed cue of this deck (a
 *    point hot cue from the load, else a copy of the holder's own slot taken on the playhead
 *    thread), with this deck's source key in both Positions; the firmware's is_loop must
 *    accept the result. No give-up while the track is loaded.
 *
 * 0.2.3 (vs 0.2.2 emulator):
 *  - ANLZ memory loops build on every deck: template from this deck's cache, its LoopState hot
 *    cue slots, or another deck's loop cue with this deck's Position source key; units shared.
 *  - No RMW anywhere the filter/hook touch (tick clears diagnostics with load+store).
 *  - Toast is one updatable surface; a newer toast replaces it, stale 'ready' is taken down.
 *
 * 0.2.2 (vs 0.2.1 emulator):
 *  - Auto-arm at load: after the ANLZ loops are merged, with Auto-advance on and nothing
 *    armed for this track, the first stored loop ahead of the playhead becomes Active.
 *  - Engage-check hook (0x14b37a0, JUCE ALSA thread): hot path is O(1) and lock-free with
 *    NO read-modify-write atomics (0.2.1 did an atomic add + CAS-able load every call; under
 *    qemu TCG an exclusive/RMW can stop all vCPUs, which also stalls the ERP bus thread
 *    inside our filter). It compares the move against a precomputed per-deck arm point
 *    (tick-published); firmware predicates, set/clear and re-arm run only on the rare move
 *    that needs them and are timed separately ("rare").
 *  - Filter: no clock reads on the fast path (timing sampled 1 in 64 frames + every slow
 *    path). The core times the call itself (isb; cntvct around fn) and charges any
 *    preemption inside that window to us; our own 0.2.1 window started after entry and
 *    ended before the epilogue, so it read lower (189.5 us) than the core's.
 *  - Rows: on_change logs every toggle (value as saved by the core) and applies it at once.
 *  - Toasts: one pending slot, coalesced ~300 ms so fast SHIFT+MEMORY shows only the latest
 *    pick; pick toasts 1500 ms; ready toast once per lock, and a load on another deck drops
 *    a pending/scheduled 'ready' for the others.
 *
 * 0.2.1 (vs 0.2.0 emulator):
 *  - Stored loops known at load: the track's ANLZ0000.EXT/.DAT (PCO2/PCOB) is read READ-ONLY
 *    from the stick on a worker thread (path via lib_tracks + PPTH index); memory loops not
 *    yet seen live get a CueProperty built from a loop cue of the same track (in/out only).
 *  - Exit detection by playhead (inside, then > out + 150 ms on 3 ticks); every Auto-advance
 *    decision logged; with nothing ahead (or Auto-advance off) the holder is cleared so the
 *    exited loop cannot re-trap.
 *  - SHIFT+MEMORY cycles after the queued/Active loop, wraps, says when only one is ahead.
 *  - Engage-check hook: crossing test first, firmware predicates only at the in point.
 *    Filter/hook durations self-measured (cntvct) and logged every 10 s.
 *  - Rows: Active Loop, Auto-advance (default on), Re-arm. Combo/pad/clear/toast rows gone.
 *
 * 0.2.0 (redesign):
 *  - Any stored loop can be the Active auto loop without moving the playhead:
 *    SHIFT+pad = that hot cue loop; SHIFT+MEMORY = next stored loop ahead (repeat cycles).
 *  - Set/clear queued and applied by a hook on 0x14b37a0 (engage check, playhead thread).
 *  - Re-arm: if LoopState still holds an older, non-looping loop, the firmware's own engage
 *    0x14b31f0 runs at the in point (replaces LoopState's loop, starts it).
 *  - Optional Auto-advance. No synthetic store at all; 0.1.12 arm removed.
 *
 * 0.1.12 (vs 0.1.11 emulator):
 *  - No synthetic store from tick. SHIFT+pad arms the hot-cue loop; the next stock MEMORY
 *    store with the same in/out sets it Active (no store pointer / MEMORY pre-press needed).
 *  - Stock DELETE is pending until proven otherwise; nothing is cleared or forgotten.
 *
 * 0.1.11 (vs 0.1.10 emulator):
 *  - prewarm reset FS.del (spurious startup DELETE); tick seeds its DELETE baseline.
 *  - Fresh-copy deferred set skipped when LoopState has a non-looping loop; toast after set.
 *
 * 0.1.10 (vs 0.1.9 emulator):
 *  - Stock DELETE (no SHIFT) seen by the filter; the last called/stored memory cue is removed
 *    from the cache, and if it was the Active Loop the holder is cleared and forgotten.
 *  - Dup check and restore only use memory cues seen live since the last load.
 *
 * 0.1.9 (vs 0.1.8 emulator):
 *  - Fresh SHIFT+pad copy: store now, set ~100 ms later via the dup-memory path.
 *  - Track (re)load detected from load-event bursts and content-id changes; restore
 *    re-applies the saved active cue on the same track.
 *  - Ready banner per deck, 1 s after lock, re-queued at +1.5 s and +3 s.
 *
 * 0.1.8 (vs 0.1.7 emulator):
 *  - Never destroy an unconstructed cache slot (0.1.7 asserted in ~PositionWithSourceInfo).
 *  - After each set, log holder loop + LoopState has-loop/looping (engage preconditions).
 *  - Filter fast path: one xor+mask, return when nothing changed and no SHIFT held.
 *  - Deck events handled in tick; ready banner ~3 s after first lock; restore on same-track reload.
 *
 * 0.1.7 (vs 0.1.6 emulator):
 *  - Filter: no clock, no RMW atomics, no deck logic, no calls; one 64-byte line; edge
 *    counters published with one relaxed u64 store; all paths pre-run at init; -z now.
 *  - Filter heartbeat in tick: if frames stop, log + "Active Loop: off" banner.
 *  - Player id decoded from the 0.1.6 dumps: facade+0xa8 -> {vt 0x2be5650, hashed id, u32 id};
 *    locks at the first event, presses are the fallback.
 *  - Store events attributed only by press correlation (never a default side).
 *  - Banner 4000 ms.
 *
 * 0.1.6 (vs 0.1.5 emulator: never bound):
 *  - Deck binding by input correlation: a CALL/MEMORY/pad press edge on one side, then a
 *    call/setout event from facade F within 100 ms -> candidate; 2 consistent -> locked.
 *    Never two facades on one deck; secondary deck select tracked in the filter.
 *    Player-id search kept as a confirmer (mismatches logged).
 *  - One hex dump per facade (0x200) and its pointees (0x120) via safe_read, from tick.
 *  - "Active Loop ready" shown on the first lock; "store armed" log back.
 *
 * 0.1.5 (vs 0.1.4 emulator crash):
 *  - Ring slot 64-aligned, cue buffer first (16-aligned): 0.1.4's cue at +12 made the
 *    firmware cue copy SIGBUS. Plain field stores, atomics on aligned u32 only.
 *  - Hooks copy raw args only (facade ptr, kind, firmware copy of the cue). Deck decode,
 *    is_loop, binding and every facade deref run in tick through safe_read
 *    (process_vm_readv), so a bad offset cannot fault a firmware thread.
 *  - Player id: facade+0x40 was wrong (read a pointer half). The id path is now searched
 *    and validated by the id object's signature; pid >= 4 rejected; learned path logged.
 *  - Banner: literal (static) text, queued from tick; ready banner deferred ~5 s.
 *
 * 0.1.4 (vs 0.1.3 emulator):
 *  - Toasts: C->banner called directly from tick() (mod code). Built with
 *    -fno-optimize-sibling-calls so no core call is a tail branch (a tail call
 *    makes the core see the post_ui runner as the caller -> "outside its code").
 *  - Deck binding is deterministic and permanent: the LocalDjPlayerID that
 *    DjPlayerUnit's ctor stores (the SDK's [[unit+0x1a0]+0x1f18] deck field) is
 *    also kept by the facade's Y[0xd] object at +0xf8 (and +0x108). Facade+0x40
 *    = Y[0xd]. No arrival / sole-loaded / bus-side heuristics.
 *  - Memory loops and hot-cue loops cached separately; SHIFT+MEMORY and the
 *    dup path both call set-active with a memory-cue object.
 *  - Hooks only copy the cue into a ring and return; logs and cache work run
 *    in tick().
 *  - Still no MoveChecking 0x155f500.
 */
#define _DEFAULT_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <fcntl.h>

#include "azmod-mod.h"

#define MOD_ID  "active-loop"
#define FW_TAG  "ep147-9e9454e357389ece"

#define VA_CALL      ((uintptr_t)0x142da18u)
#define VA_LOAD      ((uintptr_t)0x142d3d0u)
#define VA_STORE     ((uintptr_t)0x86b410u)
#define VA_SET       ((uintptr_t)0x14b3198u)
#define VA_CLEAR     ((uintptr_t)0x14b31e8u)
#define VA_IS_LOOP   ((uintptr_t)0x14a76b8u)
#define VA_CUE_COPY  ((uintptr_t)0x8504a8u)
#define VA_CUE_DTOR  ((uintptr_t)0x14a32d8u)
#define VA_CUE_MARK  ((uintptr_t)0x14a7648u)
#define VA_LOOP_SLOT ((uintptr_t)0x149ef50u)   /* LoopState cue-by-kind */
#define VA_ECHECK      ((uintptr_t)0x14b37a0u)   /* ActiveLoop auto-engage check */
#define VA_ENGAGE      ((uintptr_t)0x14b31f0u)   /* ActiveLoop engage */
#define VA_POS_IN      ((uintptr_t)0x14a76a8u)   /* CueProperty -> in Position */
#define VA_VALID_LOOP  ((uintptr_t)0x14a7668u)   /* CueProperty loop valid (engage check) */
#define VA_1508        ((uintptr_t)0x1508a28u)
#define VA_1525        ((uintptr_t)0x1525c28u)
#define VA_LS_HAS_LOOP ((uintptr_t)0x149f090u)
#define VA_LS_LOOPING  ((uintptr_t)0x149eee8u)
#define VA_SETOUT    ((uintptr_t)0x1437c20u)   /* CueLoopSetFacade::setLoopOutPoint */

#define CUE_SIZE     256
#define NDECK        4
#define NHOT         9      /* kinds 1..8 */
#define NMEM         32
#define MEM_SOFT_CAP 10
#define KIND_MEM     0

/* Facade layout (CueLoadFacade and CueLoopSetFacade share the base at +8). */
#define OFF_LOOP_ST  0x60   /* Y[0xf]  */
#define OFF_ENGINE   0x70   /* Y[8]    */
#define OFF_HOLDER   0xd0   /* ActiveLoop holder in engine */
#define YD_PID_A     0xf8
#define YD_PID_B     0x108

#define CUE_IN_TIME  0x40
#define CUE_OUT_TIME 0x98

#define ERP_LEN 30
#define A_SH_O  0x09
#define A_SH_B  7
#define A_MEM_O 0x0c
#define A_MEM_B 7
#define A_DEL_O 0x0c
#define A_DEL_B 6
#define A_PAD_O 0x0e
#define A_SEC_O 0x09
#define A_SEC_B 0
#define A_PRI_O 0x08
#define A_PRI_B 0
#define B_SH_O  0x11
#define B_SH_B  7
#define B_MEM_O 0x14
#define B_MEM_B 7
#define B_DEL_O 0x14
#define B_DEL_B 6
#define B_PAD_O 0x16
#define B_SEC_O 0x11
#define B_SEC_B 0
#define B_PRI_O 0x10
#define B_PRI_B 0

static const az_core_v1 *C;

typedef void (*facade_cue_fn)(void *facade, uint32_t kind, void *cue);
typedef void (*store_fn)(void *store, int kind, void *cue);
typedef void (*set_fn)(void *holder, void *cue);
typedef void (*clear_fn)(void *holder);
typedef int  (*pred_fn)(void *cue);
typedef void (*cue_copy_fn)(void *dst, const void *src);
typedef void (*cue_dtor_fn)(void *cue);
typedef void (*cue_mark_fn)(void *cue, const void *mark);
typedef void *(*loop_slot_fn)(void *loop_state, int kind);
typedef void (*setout_fn)(void *facade, int kind, int slip);

static facade_cue_fn orig_call, orig_load;
static store_fn      orig_store;
static setout_fn     orig_setout;
static set_fn        fw_set;
static clear_fn      fw_clear;
static pred_fn       fw_is_loop;
static cue_copy_fn   fw_cue_copy;
static cue_dtor_fn   fw_cue_dtor;
static cue_mark_fn   fw_cue_mark;
static loop_slot_fn  fw_loop_slot;
typedef int (*ls_pred_fn)(void *ls);
static ls_pred_fn fw_ls_has_loop, fw_ls_looping;


/* Tick-only binding state (0.1.5: nothing is bound or decoded on a hook thread). */
static void *engine_of[NDECK];
static void *ls_of[NDECK];
static uint32_t bind_logged;
static uint32_t engine_mismatch[NDECK];
static void *store_any;                 /* atomic (hook writes one aligned pointer) */

#define NFAC 16
typedef struct {
    void *facade;
    int   deck;        /* locked deck, -1 = not yet */
    int   cand, cand_n;/* input-correlation candidate */
    int   pid;         /* player-id search result (-1 none, -3 not tried) */
    int   dumped;
} fac_t;
static fac_t fac_map[NFAC];   /* tick-only */
static int   any_locked;
static long  ready_tick[NDECK][3];
static void ready_sched(int d);
static int fac_n;

/* Tick-only caches, 16-aligned for the firmware cue copy/dtor. */
static uint8_t mem_buf[NDECK][NMEM][CUE_SIZE] __attribute__((aligned(64)));
static int     mem_n[NDECK];
static uint8_t mem_ok[NDECK][NMEM];
static uint8_t mem_live[NDECK][NMEM];   /* 1 = seen live from the firmware, dup-able */
static int64_t last_call_in[NDECK], last_call_out[NDECK];
static int64_t del_pend_in[NDECK], del_pend_out[NDECK];
static int     del_pend_ok[NDECK];
static void set_active_mem(int d, int i, const char *why);
static int     last_call_ok[NDECK];
static long     last_load_tick[NDECK];
static uint8_t  act_buf[NDECK][CUE_SIZE] __attribute__((aligned(64)));
static uint8_t  act_ok[NDECK];
static int autoarm_done[NDECK];
static void track_changed(int d, const char *why);   /* 1 = mem_buf slot holds a constructed CueProperty */
static int     last_mem[NDECK] = { -1, -1, -1, -1 };
static uint8_t hot_buf[NDECK][NHOT][CUE_SIZE] __attribute__((aligned(64)));
static int     hot_ok[NDECK][NHOT];

static int side_deck[2] = { 0, 1 };

/* ---- hook -> tick ring ----
 * 0.1.4 SIGBUS: ev_t was {u32,4*i8,u32,cue[256]} so cue sat at +12; the firmware
 * cue copy (0x8504a8) does 8/16-byte accesses -> alignment fault in our .bss.
 * Now: slot 64-aligned, cue first at +0 (16-aligned), metadata naturally aligned,
 * atomics only on aligned u32. */
enum { SRC_CALL = 1, SRC_LOAD, SRC_STORE, SRC_SETOUT };
#define RING 64
typedef struct __attribute__((aligned(64))) {
    uint8_t   cue[CUE_SIZE] __attribute__((aligned(16)));
    void     *facade;       /* raw arg, decoded in tick */
    uint64_t  t_ns;         /* CLOCK_MONOTONIC at the hook */
    uint32_t  seq;          /* index+1 when ready, 0 = free (atomic) */
    int32_t   kind;
    int32_t   src;
    int32_t   has_cue;      /* 1 = cue[] holds a firmware copy (needs dtor) */
} ev_t;
_Static_assert(sizeof(ev_t) % 64 == 0, "ev_t size");
_Static_assert(__builtin_offsetof(ev_t, cue) % 16 == 0, "cue align");
_Static_assert(__builtin_offsetof(ev_t, seq) % 4 == 0, "seq align");
#define SEQ_BUSY 0xffffffffu   /* slot claimed by a producer, not yet published */
static ev_t     ring[RING] __attribute__((aligned(64)));
static uint32_t ring_w __attribute__((aligned(64)));   /* atomic */
static uint32_t ring_r;     /* tick-only */
static uint32_t ring_drop;  /* atomic */
static int      ring_stall;

/* ---- filter -> tick (0.1.7) ----
 * The filter keeps everything in one 64-byte line and publishes one u64 per frame with a
 * single relaxed store. Layout of snap (per side s, base = 28*s):
 *   nibble counters (wrap at 16) at base+0 SET, +4 CLR, +8 PAD, +12 PRESS, +16 SEC, +20 PRI;
 *   pad kind (1..8) at base+24; heartbeat frame counter at bits 56..63.
 * The tick diffs counters against its last copy, so no press is lost between ticks. */
enum { SG_SET = 0, SG_CLR, SG_PAD, SG_PRESS, SG_SEC, SG_PRI, NSIG };
static struct __attribute__((aligned(64))) {
    uint64_t snap;      /* filter writes (relaxed store), tick reads (relaxed load) */
    uint32_t prev;      /* filter-only: last level bits, 14 per side, bit 31 = seen */
    uint32_t cfg;       /* tick writes (relaxed), filter reads: CFG_* | combo */
    uint32_t del;       /* filter: stock DELETE (no SHIFT) edge counter per side (byte s) */
    uint32_t fmax;      /* 0.2.1: filter's own max duration, cntvct ticks (tick swaps to 0) */
    uint32_t fover;     /* 0.2.1: calls over thr */
    uint32_t thr;       /* tick writes: 100 us in cntvct ticks */
} FS;
_Static_assert(sizeof FS == 64, "FS one line");
static uint64_t tk_snap;               /* tick-only: last snapshot processed */
static int      tk_hb_same, filter_dead, tk_hb_seen;
static uint64_t press_obs_ns[2];       /* tick-only: when the tick saw a PRESS per side */
static int      press_obs_deck[2] = { -1, -1 };
#if defined(__aarch64__)
static inline uint64_t cnt_now(void) { uint64_t v; __asm__ volatile("mrs %0, cntvct_el0" : "=r"(v)); return v; }
static inline uint64_t cnt_frq(void) { uint64_t v; __asm__ volatile("mrs %0, cntfrq_el0" : "=r"(v)); return v; }
#else   /* host test harness */
static inline uint64_t cnt_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
static inline uint64_t cnt_frq(void) { return 1000000000ull; }
#endif
static uint32_t hmax_ec_own, hmax_ec_all, hmax_cue;   /* atomic, cntvct ticks, tick swaps */
/* 0.2.3: tick reads then clears a diagnostic max with plain load + store (no RMW on a line the
 * filter or hook writes; a lost update only loses one sample) */
static inline uint32_t take0(uint32_t *m)
{
    uint32_t v = __atomic_load_n(m, __ATOMIC_RELAXED);
    __atomic_store_n(m, 0, __ATOMIC_RELAXED);
    return v;
}
static inline void max_upd(uint32_t *m, uint64_t dt)
{
    uint32_t v = dt > 0xffffffffu ? 0xffffffffu : (uint32_t)dt;
    if (v > __atomic_load_n(m, __ATOMIC_RELAXED)) __atomic_store_n(m, v, __ATOMIC_RELAXED);
}
static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

static int h_autoadv = -1, h_rearm = -1;
static int h_enable = -1;
static uint32_t cfg_bits = (1u) | (0u << 1) | (1u << 3) | (1u << 4) | (1u << 5);
enum { CFG_ENABLE = 1u << 0, CFG_HOTCUE = 1u << 3, CFG_CLEAR = 1u << 4, CFG_TOAST = 1u << 5,
       CFG_AUTOADV = 1u << 6, CFG_REARM = 1u << 7 };
static int cfg_combo_val(uint32_t b) { return (int)((b >> 1) & 3u); }

enum { COMBO_SHIFT_MEMORY = 0, COMBO_OFF_PADS = 1, COMBO_PAD_ONLY = 2, NCOMBO = 3 };

static int plausible(uintptr_t p)
{
    return p >= 0x10000u && (p & 7u) == 0 && p < ((uintptr_t)1 << 48);
}

/* Fault-free read: process_vm_readv on ourselves returns EFAULT instead of SIGSEGV.
 * Tick-only (a syscall). */
#define LOG(...) do { if (C && AZ_HAS(C, log)) C->log(MOD_ID, __VA_ARGS__); } while (0)

/* Fallback when the kernel lacks process_vm_readv (ENOSYS / no CROSS_MEMORY_ATTACH):
 * check the range against readable mappings from /proc/self/maps (tick only). */
#define NMAP 512
static uintptr_t map_lo[NMAP], map_hi[NMAP];
static int map_n, vm_mode = -1;         /* -1 unknown, 1 process_vm_readv, 0 maps */
static long tick_no;
/* 0.2.6: the mappings are read once, in init(), never on the tick path. */
static void load_maps(void)
{
    map_n = 0;
    int fd = open("/proc/self/maps", O_RDONLY | O_CLOEXEC);
    if (fd < 0) return;
    FILE *f = fdopen(fd, "re");
    if (!f) { close(fd); return; }
    char line[256];
    while (map_n < NMAP && fgets(line, sizeof line, f)) {
        char *p = line, *q;
        unsigned long lo = strtoul(p, &q, 16);
        if (*q != '-') continue;
        unsigned long hi = strtoul(q + 1, &p, 16);
        if (*p == ' ' && p[1] == 'r') {
            map_lo[map_n] = lo; map_hi[map_n] = hi; map_n++;
        }
    }
    fclose(f);
}
/* init(): probe process_vm_readv once; only when it is missing, snapshot the maps. */
static void probe_safe_read(void)
{
    uint64_t src = 0x5a5a5a5a5a5a5a5aull, dst = 0;
    struct iovec l = { &dst, sizeof dst }, r = { &src, sizeof src };
    long rc = syscall(SYS_process_vm_readv, (long)getpid(), &l, 1L, &r, 1L, 0L);
    if (rc == (long)sizeof dst && dst == src) { vm_mode = 1; return; }
    vm_mode = 0;
    load_maps();
}
static int in_maps(uintptr_t a, size_t n)
{
    for (int i = 0; i < map_n; i++)
        if (a >= map_lo[i] && a + n <= map_hi[i]) return 1;
    return 0;
}
static int safe_read(uintptr_t addr, void *dst, size_t n)
{
    if (addr < 0x10000u || addr >= ((uintptr_t)1 << 48) || addr + n < addr) return -1;
    if (vm_mode != 0) {
        struct iovec l = { dst, n }, r = { (void *)addr, n };
        long rc = syscall(SYS_process_vm_readv, (long)getpid(), &l, 1L, &r, 1L, 0L);
        return rc == (long)n ? 0 : -1;
    }
    /* fallback (no process_vm_readv): the init-time snapshot only; no file I/O here */
    if (!in_maps(addr, n)) return -1;
    memcpy(dst, (const void *)addr, n);
    return 0;
}
static uintptr_t safe_ptr(uintptr_t addr)
{
    uintptr_t v = 0;
    if (safe_read(addr, &v, sizeof v) != 0 || !plausible(v)) return 0;
    return v;
}


/* ---- toasts: from tick (mod code); text is always a string literal (static
 *      storage), because the core queues banner text. ---- */
/* 0.2.3: ONE updatable toast. EP147's caution box (C->banner) is a queue the mod cannot edit,
 * so a pick 1 s after the last one queued behind it. Now the toast is our own surface (core
 * 1.2.3 surfaces, AZ_LAYER_OVER_EP147): a newer toast rewrites the text in place, and a stale
 * one (e.g. 'ready (deck 1)' after deck 2 loads) is taken down at once. Without surfaces the
 * old path is used: one pending toast, coalesced ~300 ms, then C->banner. */
#define TOAST_COALESCE_TICKS 18
static const char *toast_text;
static int  toast_pending, toast_ms, toast_ready_deck = -1;
static long toast_due;
static az_surface *tsurf;
static char  ts_text[112];
static int   ts_shown, ts_ready_deck = -1;
static uint64_t ts_hide_ns;
static void ts_paint(az_draw *dr, az_rect a, void *user)
{
    (void)user;
    int px = a.h / 2; if (px < 10) px = 10;
    C->fill_rect(dr, a.x, a.y, a.w, a.h, C->color(AZ_COL_HI));
    C->draw_text(dr, a.x + px / 2, a.y + (a.h - px * 3 / 2) / 2, ts_text, px, C->color(AZ_COL_HI_TEXT));
}
static int ts_input(const az_event *ev, void *user) { (void)ev; (void)user; return 0; }
static void ts_init(void)
{
    if (!AZ_HAS(C, surface_create) || !AZ_HAS(C, surface_show) || !AZ_HAS(C, surface_place) || !AZ_HAS(C, surface_redraw)) {
        LOG("toast: no surfaces in this core, using the banner queue"); return;
    }
    static az_surface_v1 sd;
    sd = (az_surface_v1){ .size = sizeof sd, .layer = AZ_LAYER_OVER_EP147,
        .views = AZ_VIEW_BIT(AZ_VIEW_DECK) | AZ_VIEW_BIT(AZ_VIEW_BROWSE) | AZ_VIEW_BIT(AZ_VIEW_SEARCH) |
                 AZ_VIEW_BIT(AZ_VIEW_SOURCE) | AZ_VIEW_BIT(AZ_VIEW_OTHER),
        .on_paint = ts_paint, .on_input = ts_input };
    tsurf = C->surface_create(MOD_ID, &sd);
    if (!tsurf) { LOG("toast: surface refused, using the banner queue"); return; }
    C->surface_place(tsurf, (az_rect){ 340, 12, 600, 52 });
    C->surface_show(tsurf, 0);
}
static void ts_hide(const char *why)
{
    if (!tsurf || !ts_shown) return;
    C->surface_show(tsurf, 0); ts_shown = 0; ts_ready_deck = -1;
    LOG("toast hidden (%s)", why);
}
static void do_toast_ms(const char *text, int ms, int ready_deck)  /* text: copied */
{
    if (!text) return;
    if (!(__atomic_load_n(&cfg_bits, __ATOMIC_RELAXED) & CFG_TOAST)) {
        LOG("toast off: %s", text);
        return;
    }
    if (tsurf) {                                   /* updatable: replace in place, now */
        if (ts_shown) LOG("toast superseded: '%s' -> '%s'", ts_text, text);
        snprintf(ts_text, sizeof ts_text, "%s", text);
        ts_ready_deck = ready_deck;
        ts_hide_ns = now_ns() + (uint64_t)ms * 1000000ull;
        C->surface_show(tsurf, 1); C->surface_redraw(tsurf); ts_shown = 1;
        LOG("toast shown (%d ms): %s", ms, ts_text);
        return;
    }
    if (toast_pending) LOG("toast replaced before showing: %s", toast_text);
    toast_text = text; toast_ms = ms; toast_ready_deck = ready_deck;
    toast_pending = 1;
    toast_due = tick_no + TOAST_COALESCE_TICKS;
}
static void do_toast(const char *text) { do_toast_ms(text, 2500, -1); }
static __attribute__((noinline)) void flush_toast(void)
{
    if (tsurf) { if (ts_shown && now_ns() >= ts_hide_ns) ts_hide("timeout"); return; }
    if (!toast_pending || tick_no < toast_due) return;
    toast_pending = 0;
    if (!AZ_HAS(C, banner)) { LOG("toast: core has no banner"); return; }
    C->banner(MOD_ID, toast_text, toast_ms);
    LOG("toast queued (%d ms): %s", toast_ms, toast_text);
}

/* 0.2.6: set only when init() completed; until then (or after a failed init) every hook is a
 * pure pass-through to the firmware, so a partial claim leaves nothing active. */
static int al_live;

/* ---- hook side: copy raw args only. No facade deref, no core call, no log. ---- */
static void push_ev(void *facade, int kind, int src, const void *cue)
{
    if (!__atomic_load_n(&al_live, __ATOMIC_ACQUIRE)) return;
    uint64_t t0 = cnt_now();
    uint32_t i = __atomic_fetch_add(&ring_w, 1, __ATOMIC_ACQ_REL);
    ev_t *e = &ring[i % RING];
    /* 0.2.6: claim the slot (0 -> SEQ_BUSY) so two producers a lap apart cannot both write it */
    uint32_t free_seq = 0;
    if (!__atomic_compare_exchange_n(&e->seq, &free_seq, SEQ_BUSY, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&ring_drop, 1, __ATOMIC_RELAXED);
        return;
    }
    e->facade = facade;
    e->t_ns = now_ns();
    e->kind = kind;
    e->src = src;
    e->has_cue = 0;
    /* The cue is a caller temporary, so it must be copied now; a raw byte copy would
     * keep dangling pointers of its owned members, so use the firmware copy-ctor
     * into the 16-aligned slot. Only for real cue kinds and a sane pointer. */
    if (cue && plausible((uintptr_t)cue) && kind >= 0 && kind <= 8) {
        fw_cue_copy(e->cue, cue);
        e->has_cue = 1;
    }
    __atomic_store_n(&e->seq, i + 1, __ATOMIC_RELEASE);
    max_upd(&hmax_cue, cnt_now() - t0);
}

static void my_call(void *facade, uint32_t kind, void *cue)
{
    push_ev(facade, (int)kind, SRC_CALL, cue);
    orig_call(facade, kind, cue);
}
static void my_load(void *facade, uint32_t kind, void *cue)
{
    push_ev(facade, (int)kind, SRC_LOAD, cue);
    orig_load(facade, kind, cue);
}
static void my_store(void *store, int kind, void *cue)
{
    if (plausible((uintptr_t)store) && __atomic_load_n(&al_live, __ATOMIC_ACQUIRE)) __atomic_store_n(&store_any, store, __ATOMIC_RELEASE);
    push_ev(NULL, kind, SRC_STORE, cue);
    orig_store(store, kind, cue);
}
static void my_setout(void *facade, int kind, int slip)
{
    orig_setout(facade, kind, slip);
    if (kind >= 1 && kind <= 8) push_ev(facade, kind, SRC_SETOUT, NULL);  /* slot read in tick */
}

/* ---- tick side: deck from the facade, by LocalDjPlayerID, all reads fault-free ----
 * The id object keeps the id twice: u64 at +0xf8 = id | hashA<<32, at +0x108 = id | hashB<<32
 * (the function at 0x14ca9a8). Signature: equal low halves < 4, non-zero and different high halves.
 * 0.1.4 assumed facade+0x40 is that object; on the emulator it read 0xdae47d00 (a pointer
 * half) -> wrong. Now the path is searched (1 or 2 pointer hops from the facade), learned
 * once, and every result is validated with the signature. pid >= 4 is never accepted. */
/* Player id, from the 0.1.6 dumps (D1 vs D2 facades):
 *   facade+0xa8 -> LocalDjPlayerID object: +0 vtable 0x2be5650,
 *   +8 u64 hashed id (low byte = id, rest constant), +0x10 u32 id.
 *   The same object is embedded at [facade+0xa0]+0xe0. Y[0xd] ([facade+0x40]) keeps the
 *   hashed id at +0xf8/+0x108 too: 0.1.4 compared the low 32 bits, which hold the hash
 *   (0xdae47d00 + id) -> "garbage".
 */
#define OFF_PID_OBJ  0xa8
#define VT_PLAYER_ID ((uintptr_t)0x2be5650u)
#define OFF_YD       0x40
static int pid_of_facade(uintptr_t f)
{
    uintptr_t p = safe_ptr(f + OFF_PID_OBJ);
    if (!p) return -1;
    uint64_t v[3];
    if (safe_read(p, v, sizeof v)) return -1;
    uint32_t id = (uint32_t)v[2];
    if (v[0] != VT_PLAYER_ID || id >= NDECK || (v[1] & 0xffu) != id) return -1;
    /* cross-check with Y[0xd] */
    uintptr_t yd = safe_ptr(f + OFF_YD);
    uint64_t h;
    if (yd && !safe_read(yd + YD_PID_A, &h, 8) && (h & 0xffu) != id) return -1;
    return (int)id;
}
static fac_t *fac_get(void *facade)
{
    for (int i = 0; i < fac_n; i++) if (fac_map[i].facade == facade) return &fac_map[i];
    if (fac_n >= NFAC) return NULL;
    fac_t *e = &fac_map[fac_n++];
    e->facade = facade; e->deck = -1; e->cand = -1; e->cand_n = 0; e->pid = -3; e->dumped = 0;
    return e;
}
#define PRESS_WINDOW_NS 150000000ull
/* Deck of the single side whose press the tick saw within +-150 ms of t, else -1. */
static int press_deck_for(uint64_t t)
{
    int hit = -1, n = 0;
    for (int s = 0; s < 2; s++) {
        uint64_t p = press_obs_ns[s];
        if (!p || press_obs_deck[s] < 0) continue;
        uint64_t dt = t > p ? t - p : p - t;
        if (dt <= PRESS_WINDOW_NS) { hit = press_obs_deck[s]; n++; }
    }
    return n == 1 ? hit : -1;
}
static void bind_once(int d, void *facade);
static int deck_of_facade(void *facade, int src, uint64_t t)
{
    if (!plausible((uintptr_t)facade)) return -1;
    fac_t *e = fac_get(facade);
    if (!e) return -1;
    if (e->deck >= 0) return e->deck;
    if (e->pid == -3) {
        int p = pid_of_facade((uintptr_t)facade);
        e->pid = p;
        LOG("facade %p: player id %s%d (facade+0xa8)", facade, p >= 0 ? "" : "invalid ", p);
        if (p >= 0) {
            int clash = 0;
            for (int i = 0; i < fac_n; i++)
                if (&fac_map[i] != e && fac_map[i].deck == p) clash = 1;
            if (!clash) {
                e->deck = p;
                LOG("facade %p locked to deck %d (player id)", facade, p + 1);
                bind_once(p, facade);
                ready_sched(p);
                return p;
            }
            LOG("facade %p: deck %d already taken; falling back to presses", facade, p + 1);
        }
    }
    if (src == SRC_LOAD) return -1;               /* loads are not press-driven */
    int c = press_deck_for(t);
    if (c < 0) { LOG("facade %p: event without a single recent press (not counted)", facade); return -1; }
    if (e->cand == c) e->cand_n++; else { if (e->cand >= 0) LOG("facade %p: candidate %d -> %d (reset)", facade, e->cand + 1, c + 1); e->cand = c; e->cand_n = 1; }
    LOG("facade %p: press match deck %d (%d/2)", facade, c + 1, e->cand_n);
    if (e->cand_n < 2) return -1;
    for (int i = 0; i < fac_n; i++)
        if (&fac_map[i] != e && fac_map[i].deck == c) {
            LOG("facade %p: deck %d already locked to %p; conflict, not bound", facade, c + 1, fac_map[i].facade);
            e->cand = -1; e->cand_n = 0;
            return -1;
        }
    e->deck = c;
    if (e->pid >= 0 && e->pid != c) LOG("facade %p: MISMATCH press says deck %d, player-id says %d", facade, c + 1, e->pid + 1);
    else if (e->pid == c) LOG("facade %p: player-id confirms deck %d", facade, c + 1);
    LOG("facade %p locked to deck %d (input-correlated)", facade, c + 1);
    bind_once(c, facade);
    ready_sched(c);
    return c;
}
static void bind_once(int d, void *facade)
{
    uintptr_t eng = safe_ptr((uintptr_t)facade + OFF_ENGINE);
    if (!eng) return;
    if (!engine_of[d]) {
        engine_of[d] = (void *)eng;
        ls_of[d] = (void *)safe_ptr((uintptr_t)facade + OFF_LOOP_ST);
    } else if (engine_of[d] != (void *)eng) {
        engine_mismatch[d]++;
    }
}

/* ---- ERP filter: bit tests only. No bind, resolve, log, row_value, or shared locks. ---- */
#define NIB_INC(sn, pos) ((sn) = ((sn) & ~(0xfull << (pos))) | \
                         ((((((sn) >> (pos)) & 0xfull) + 1u) & 0xfull) << (pos)))
#define SIDE_BITS(f, P) \
    ( (((unsigned)(f)[P##_SH_O]  >> P##_SH_B)  & 1u)       | \
      ((((unsigned)(f)[P##_MEM_O] >> P##_MEM_B) & 1u) << 1) | \
      ((((unsigned)(f)[P##_DEL_O] >> P##_DEL_B) & 1u) << 2) | \
      ((((unsigned)(f)[P##_MEM_O] >> 4) & 3u) ? 8u : 0u)     | \
      ((((unsigned)(f)[P##_SEC_O] >> P##_SEC_B) & 1u) << 4) | \
      ((((unsigned)(f)[P##_PRI_O] >> P##_PRI_B) & 1u) << 5) | \
      ((unsigned)(f)[P##_PAD_O] << 6) )

/* Filter: level bits, eat, count edges, one relaxed store. No clock, no RMW atomics,
 * no deck logic, no calls. */
__attribute__((hot)) static int on_erp(int bus, unsigned char *frame, int len, void *user)
{
    (void)bus; (void)user;
    if (__builtin_expect(!frame || len < ERP_LEN, 0)) return AZ_FRAME_OK;
    uint32_t cfg = __atomic_load_n(&FS.cfg, __ATOMIC_RELAXED);
    uint64_t sn = FS.snap;
    sn += 1ull << 56;                                    /* heartbeat */
    /* 0.2.2: own timing only on 1 frame in 64 (heartbeat low bits) and on slow paths */
    uint64_t t0 = ((sn >> 56) & 63u) == 0 ? cnt_now() : 0;
    if (cfg & CFG_ENABLE) {
        uint32_t cur = SIDE_BITS(frame, A) | (SIDE_BITS(frame, B) << 14);
        uint32_t prev = FS.prev;
        /* fast path: nothing relevant changed and SHIFT is up on both sides */
        if (__builtin_expect(((cur ^ prev) & 0x0fffffffu) == 0 && (prev >> 31) &&
                             !(cur & (1u | (1u << 14))), 1)) {
            __atomic_store_n(&FS.snap, sn, __ATOMIC_RELAXED);
            if (__builtin_expect(t0 == 0, 1)) return AZ_FRAME_OK;
            goto timed;
        }
        if (!t0) t0 = cnt_now();
        uint32_t rise = (prev >> 31) ? (cur & ~prev) : 0u;
        FS.prev = cur | 0x80000000u;
        int do_mem = cfg_combo_val(cfg) == COMBO_SHIFT_MEMORY;
        int do_pad = (cfg & CFG_HOTCUE) || cfg_combo_val(cfg) == COMBO_PAD_ONLY;
        int do_clr = (cfg & CFG_CLEAR) != 0;
        if (__builtin_expect(rise != 0, 0)) {
            for (int s = 0; s < 2; s++) {
                unsigned c = (cur >> (14 * s)) & 0x3fffu, r = (rise >> (14 * s)) & 0x3fffu;
                unsigned base = 28u * (unsigned)s, padnew = r >> 6;
                if (!r) continue;
                if ((c & 1u) && (r & 2u) && do_mem) NIB_INC(sn, base + 0);
                if ((c & 1u) && (r & 4u) && do_clr) NIB_INC(sn, base + 4);
                if ((c & 1u) && padnew && do_pad) {
                    NIB_INC(sn, base + 8);
                    sn = (sn & ~(0xfull << (base + 24))) |
                         ((uint64_t)(__builtin_ctz(padnew) + 1) << (base + 24));
                }
                if ((r & (2u | 8u)) || padnew) NIB_INC(sn, base + 12);
                if (r & 16u) NIB_INC(sn, base + 16);
                if (r & 32u) NIB_INC(sn, base + 20);
                if ((r & 4u) && !(c & 1u))          /* stock DELETE: rare, second store */
                    __atomic_store_n(&FS.del, FS.del + (1u << (8 * s)), __ATOMIC_RELAXED);
            }
        }
        /* eat (level based, stateless) */
        if (cur & 1u) {
            if (do_mem) frame[A_MEM_O] &= (unsigned char)~(1u << A_MEM_B);
            if (do_clr) frame[A_DEL_O] &= (unsigned char)~(1u << A_DEL_B);
            if (do_pad) frame[A_PAD_O] = 0;
        }
        if (cur & (1u << 14)) {
            if (do_mem) frame[B_MEM_O] &= (unsigned char)~(1u << B_MEM_B);
            if (do_clr) frame[B_DEL_O] &= (unsigned char)~(1u << B_DEL_B);
            if (do_pad) frame[B_PAD_O] = 0;
        }
    }
    FS.snap = sn;   /* own copy (filter-only reader) ... */
    __atomic_store_n(&FS.snap, sn, __ATOMIC_RELAXED);   /* ...and the one published store */
timed:;
    if (!t0) return AZ_FRAME_OK;
    uint64_t dt = cnt_now() - t0;                       /* 0.2.1: our own time, self-measured */
    if (__builtin_expect(dt > FS.fmax, 0)) __atomic_store_n(&FS.fmax, (uint32_t)(dt > 0xffffffffu ? 0xffffffffu : dt), __ATOMIC_RELAXED);
    if (__builtin_expect(dt > FS.thr, 0)) __atomic_store_n(&FS.fover, FS.fover + 1, __ATOMIC_RELAXED);
    return AZ_FRAME_OK;
}

/* Run every filter path once at init (before registering): the first execution of a code
 * path is the slow one (page-in, i-cache; on the qemu emulator, JIT translation). */
static void prewarm_filter(void)
{
    static const uint8_t pads[] = { 1, 2, 4, 128 };
    unsigned char f[ERP_LEN];
    uint32_t keep = FS.cfg;
    FS.cfg = CFG_ENABLE | CFG_HOTCUE | CFG_CLEAR;
    for (int rep = 0; rep < 3; rep++)
        for (int step = 0; step < 16; step++) {
            memset(f, 0, sizeof f);
            if (step & 1) { f[A_SH_O] |= 1u << A_SH_B; f[B_SH_O] |= 1u << B_SH_B; }
            if (step & 2) { f[A_MEM_O] |= (1u << A_MEM_B) | 0x10; f[B_MEM_O] |= (1u << B_MEM_B) | 0x20; }
            if (step & 4) { f[A_DEL_O] |= 1u << A_DEL_B; f[B_DEL_O] |= 1u << B_DEL_B;
                            f[A_SEC_O] |= 1u << A_SEC_B; f[B_PRI_O] |= 1u << B_PRI_B; }
            if (step & 8) { f[A_PAD_O] = pads[rep]; f[B_PAD_O] = pads[rep + 1]; }
            on_erp(AZ_BUS_ERP_IN, f, ERP_LEN, NULL);
            memset(f, 0, sizeof f);
            on_erp(AZ_BUS_ERP_IN, f, ERP_LEN, NULL);
        }
    FS.snap = 0; FS.prev = 0; FS.del = 0; FS.fmax = 0; FS.fover = 0; FS.cfg = keep;   /* 0.1.10 forgot del -> spurious DELETE */
}

/* ---- tick-side caches ---- */
static int64_t cue_in(const void *c)  { return *(const int64_t *)((const uint8_t *)c + CUE_IN_TIME); }
static int64_t cue_out(const void *c) { return *(const int64_t *)((const uint8_t *)c + CUE_OUT_TIME); }
static const char *src_name(int s)
{
    return s == SRC_CALL ? "call" : s == SRC_LOAD ? "load" : s == SRC_STORE ? "store" : "setout";
}

/* 0.2.1: loops read from ANLZ are in ms, so match within MATCH_TOL units (~2 ms) */
#define MATCH_TOL 100
static uint8_t mem_flag[NDECK][NMEM];   /* 0.2.2: rekordbox Active Loop flag (from ANLZ) */
static uint8_t mem_syn[NDECK][NMEM];    /* 1 = built from ANLZ (template + in/out), not yet seen live */
static int near(int64_t a, int64_t b) { int64_t x = a - b; return x <= MATCH_TOL && x >= -MATCH_TOL; }
static int mem_find(int d, const void *c)
{
    int64_t in = cue_in(c), out = cue_out(c);
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_ok[d][i] && near(cue_in(mem_buf[d][i]), in) && near(cue_out(mem_buf[d][i]), out)) return i;
    return -1;
}
/* Dup check: only entries the firmware showed us live (call/store/our own store) since the
 * last load and not stock-deleted since. Restored/remembered copies never count. */
static int mem_find_live(int d, const void *c)
{
    int i = mem_find(d, c);
    return i >= 0 && mem_live[d][i] ? i : -1;
}
/* Add (or refresh) a memory loop; returns its index. */
static int mem_put(int d, const void *c)
{
    int i = mem_find(d, c);
    int fresh = i < 0;
    if (i < 0)
        for (int k = 0; k < mem_n[d]; k++) if (!mem_ok[d][k]) { i = k; break; }
    if (i < 0) {
        if (mem_n[d] < NMEM) i = mem_n[d]++;
        else i = NMEM - 1;
    }
    /* 0.1.7 bug: dtor ran on never-constructed (zeroed) slots -> EP147 asserted
     * ExistenceCheckable<PositionWithSourceInfo>::exists() in ~PositionWithSourceInfo
     * (0x14a348c / 0x14a36ac) and the Position registry was disturbed. */
    if (mem_ok[d][i]) { fw_cue_dtor(mem_buf[d][i]); mem_ok[d][i] = 0; }
    memset(mem_buf[d][i], 0, CUE_SIZE);
    fw_cue_copy(mem_buf[d][i], c);
    mem_ok[d][i] = 1;
    if (fresh) mem_flag[d][i] = 0;
    return i;
}
static void hot_put(int d, int k, const void *c)
{
    if (hot_ok[d][k]) fw_cue_dtor(hot_buf[d][k]);
    memset(hot_buf[d][k], 0, CUE_SIZE);
    fw_cue_copy(hot_buf[d][k], c);
    hot_ok[d][k] = 1;
}
static void mem_reset(int d)
{
    for (int i = 0; i < NMEM; i++)
        if (mem_ok[d][i]) { fw_cue_dtor(mem_buf[d][i]); mem_ok[d][i] = 0; }
    memset(mem_live[d], 0, sizeof mem_live[d]);
    memset(mem_syn[d], 0, sizeof mem_syn[d]);
    memset(mem_flag[d], 0, sizeof mem_flag[d]);
    last_call_ok[d] = 0;
    mem_n[d] = 0; last_mem[d] = -1;
    for (int k = 0; k < NHOT; k++) {
        if (hot_ok[d][k]) fw_cue_dtor(hot_buf[d][k]);
        hot_ok[d][k] = 0;
    }
}

static int side_to_deck(int side)
{
    int d = side_deck[side & 1];
    if (d < 0 || d >= NDECK) d = side & 1;
    return d;
}

static void *live_hot(int d, int kind);
/* 0.2.4: Position (0x58) validity, exactly as the firmware's is_loop/valid_loop read it:
 * +8 byte != 0, +0xc low 5 bits != 0, +0xe byte != 0, +0x10 u32 != 0, +0x18 sample != INT64_MAX.
 * Bytes +8..+0x13 are the "source key" (same track/deck); +0x18 is the sample. */
static int pos_valid(const uint8_t *P)
{
    return P[8] && (P[12] & 0x1f) && P[14] && *(const uint32_t *)(P + 16) &&
           *(const int64_t *)(P + 24) != INT64_MAX;
}
/* 0.2.4: per deck, the latest firmware-constructed cue of ANY kind seen since the load (point
 * hot cues included) and its in-Position source key: a base to build loops from when the
 * track has no loop cue the firmware handed us. Tick-only. */
static uint8_t  base_buf[NDECK][CUE_SIZE] __attribute__((aligned(64)));
static uint8_t  base_ok[NDECK];
static int      base_kind[NDECK];
static uint64_t tk_k8[NDECK];
static uint32_t tk_k16[NDECK], tk_kok[NDECK];
static void base_note(int d, const ev_t *e)
{
    const uint8_t *P = e->cue + 0x28;
    if (!e->has_cue || !pos_valid(P)) return;
    if (base_ok[d]) fw_cue_dtor(base_buf[d]);
    memset(base_buf[d], 0, CUE_SIZE);
    fw_cue_copy(base_buf[d], e->cue);
    base_ok[d] = 1; base_kind[d] = e->kind;
    tk_k8[d] = *(const uint64_t *)(P + 8); tk_k16[d] = *(const uint32_t *)(P + 16);
    if (!tk_kok[d]) LOG("deck %d: source key from a %s cue (kind %d)", d + 1, e->src == SRC_LOAD ? "load" : "call", e->kind);
    tk_kok[d] = 1;
}
static void handle_ev(ev_t *e)
{
    int d = -1;
    if (e->src == SRC_STORE) {
        static int store_armed;
        if (!store_armed) { store_armed = 1; LOG("store armed (store=%p)", __atomic_load_n(&store_any, __ATOMIC_ACQUIRE)); }
        d = press_deck_for(e->t_ns);          /* the MEMORY press side, never a default */
        if (d < 0) { LOG("store kind=%d: no single recent press, not attributed", e->kind); return; }
    } else {
        d = deck_of_facade(e->facade, e->src, e->t_ns);
        if (d >= 0 && e->src == SRC_LOAD) {
            if (!last_load_tick[d] || tick_no - last_load_tick[d] > 60) track_changed(d, "load events");
            last_load_tick[d] = tick_no;
        }
    }
    int loop = e->has_cue && fw_is_loop(e->cue);
    if (d < 0 || d >= NDECK) {
        LOG("%s kind=%d loop=%d: no deck", src_name(e->src), e->kind, loop);
        return;
    }
    if (e->src == SRC_SETOUT) {
        void *c = live_hot(d, e->kind);
        LOG("setout deck %d kind=%d live=%d", d + 1, e->kind, c && fw_is_loop(c));
        if (c && fw_is_loop(c)) hot_put(d, e->kind, c);
        return;
    }
    LOG("%s deck %d kind=%d loop=%d", src_name(e->src), d + 1, e->kind, loop);
    if (e->src == SRC_LOAD || e->src == SRC_CALL) base_note(d, e);
    if (e->kind == KIND_MEM && e->has_cue && (e->src == SRC_CALL || e->src == SRC_STORE)) {
        last_call_in[d] = cue_in(e->cue); last_call_out[d] = cue_out(e->cue); last_call_ok[d] = 1;
    }
    if (!loop) return;
    if (e->kind == KIND_MEM) {
        int i = mem_put(d, e->cue);
        if (mem_syn[d][i]) LOG("deck %d: ANLZ memory loop #%d now seen live (real cue replaces the built one)", d + 1, i);
        mem_live[d][i] = 1; mem_syn[d][i] = 0;
        if (del_pend_ok[d] && near(cue_in(e->cue), del_pend_in[d]) && near(cue_out(e->cue), del_pend_out[d])) {
            del_pend_ok[d] = 0;
            LOG("deck %d: memory cue in=%lld out=%lld seen again -> stock DELETE did not remove it", d + 1,
                (long long)del_pend_in[d], (long long)del_pend_out[d]);
        }
        if (e->src != SRC_LOAD) last_mem[d] = i;
        else if (last_mem[d] < 0) last_mem[d] = i;
    } else if (e->kind >= 1 && e->kind <= 8) {
        hot_put(d, e->kind, e->cue);
    }
}

static void drain_ring(void)
{
    for (int n = 0; n < RING; n++) {
        uint32_t w = __atomic_load_n(&ring_w, __ATOMIC_ACQUIRE);
        if (ring_r == w) break;
        ev_t *e = &ring[ring_r % RING];
        uint32_t s = __atomic_load_n(&e->seq, __ATOMIC_ACQUIRE);
        if (s == ring_r + 1) {
            handle_ev(e);
            if (e->has_cue) fw_cue_dtor(e->cue);
            __atomic_store_n(&e->seq, 0, __ATOMIC_RELEASE);
            ring_r++; ring_stall = 0;
        } else if (s != 0 && s != SEQ_BUSY) {    /* stale slot from a dropped lap */
            if (e->has_cue) fw_cue_dtor(e->cue);
            __atomic_store_n(&e->seq, 0, __ATOMIC_RELEASE);
            ring_r++;
        } else if (++ring_stall > 3) {           /* producer dropped this index */
            ring_r++; ring_stall = 0;
        } else break;
    }
    /* Late publishers: a slot the reader already skipped (producer was preempted mid-copy)
     * is handled and freed here instead of waiting a full lap. */
    for (int k = 0; k < RING; k++) {
        ev_t *e = &ring[k];
        uint32_t sq = __atomic_load_n(&e->seq, __ATOMIC_ACQUIRE);
        if (sq && sq != SEQ_BUSY && (int32_t)((sq - 1) - ring_r) < 0) {
            handle_ev(e);
            if (e->has_cue) fw_cue_dtor(e->cue);
            __atomic_store_n(&e->seq, 0, __ATOMIC_RELEASE);
        }
    }
    uint32_t dr = __atomic_exchange_n(&ring_drop, 0, __ATOMIC_RELAXED);
    if (dr) LOG("ring: dropped %u hook events", dr);
}

static void log_binds(void)
{
    for (int d = 0; d < NDECK; d++) {
        void *eng = engine_of[d];
        if (eng && !(bind_logged & (1u << d))) {
            bind_logged |= 1u << d;
            LOG("deck %d bound via player-id engine=%p holder=%p ls=%p", d + 1, eng,
                (void *)((uint8_t *)eng + OFF_HOLDER), ls_of[d]);
        }
        uint32_t mm = engine_mismatch[d]; engine_mismatch[d] = 0;
        if (mm) LOG("deck %d: %u events from another engine ignored (binding kept)", d + 1, mm);
    }
}

static void *holder_of(int d)
{
    void *eng = engine_of[d];
    return eng ? (void *)((uint8_t *)eng + OFF_HOLDER) : NULL;
}

/* The one set-active path, used by SHIFT+MEMORY, dup, and copy. */
static int64_t  active_in[NDECK], active_out[NDECK];
static uint32_t active_cid[NDECK];
static int      restore_pending[NDECK];
static uint32_t deck_evt;   /* atomic: deck-event flags for tick */
static uint32_t deck_cid(int d)
{
    az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
    if (!AZ_HAS(C, deck_info) || C->deck_info(d, &di) != 0 || !di.loaded) return 0;
    return di.content_id;
}
static void ready_sched(int d)
{
    any_locked = 1;
    ready_tick[d][0] = tick_no + 60; ready_tick[d][1] = 0; ready_tick[d][2] = 0;   /* 0.2.2: once */
}
/* 0.2.2: a load on deck d makes 'ready' for the other decks stale */
static void ready_drop_others(int d)
{
    for (int o = 0; o < NDECK; o++) {
        if (o == d) continue;
        int had = ready_tick[o][0] || ready_tick[o][1] || ready_tick[o][2];
        ready_tick[o][0] = ready_tick[o][1] = ready_tick[o][2] = 0;
        if (had) LOG("ready toast for deck %d dropped (deck %d loaded)", o + 1, d + 1);
    }
    if (ts_shown && ts_ready_deck >= 0 && ts_ready_deck != d) {
        LOG("ready toast for deck %d cleared (deck %d loaded)", ts_ready_deck + 1, d + 1);
        ts_hide("stale ready");
    }
    if (toast_pending && toast_ready_deck >= 0 && toast_ready_deck != d) {
        LOG("pending toast dropped (stale after deck %d load): %s", d + 1, toast_text);
        toast_pending = 0;
    }
}
static void ready_flush(void)
{
    static const char *const TXT[NDECK] = { "Active Loop ready (deck 1)", "Active Loop ready (deck 2)",
                                            "Active Loop ready (deck 3)", "Active Loop ready (deck 4)" };
    for (int d = 0; d < NDECK; d++)
        for (int k = 0; k < 3; k++)
            if (ready_tick[d][k] && tick_no >= ready_tick[d][k]) {
                ready_tick[d][k] = 0;
                if (!toast_pending && !(ts_shown && ts_ready_deck < 0)) do_toast_ms(TXT[d], 2500, d); else ready_tick[d][k] = tick_no + 30;
                return;
            }
}
/* ===================== 0.2.1: known loops at load (ANLZ, read-only) ======================
 * EP147 only hands us memory cues when they are called or stored, so at load we read the
 * track's rekordbox analysis file from the stick, READ-ONLY (open O_RDONLY, never written):
 *   audio path  <- C->lib_tracks (content id)       [core 1.2.3, from our own thread]
 *   stick root  <- the first parent of the path that has PIONEER/USBANLZ
 *   ANLZ dir    <- USBANLZ/<P..>/<..>/ANLZ0000.DAT whose PPTH tag is that path (index, once)
 *   loops       <- EXT PCO2 / DAT PCOB entries of type 2 (loop): hot cue no. (0 = memory),
 *                  in ms, loop end ms.
 * All of it runs on a worker (thread_start); the tick only picks up the finished result. */
#include <dirent.h>
#include <fcntl.h>
#define NAL 64
typedef struct { int32_t hot; uint32_t in_ms, out_ms; int32_t flag; } al_t;   /* flag: rekordbox Active Loop (PCPT status 4) */
typedef struct {
    uint32_t cid; int n; al_t e[NAL];
    char msg[200];
} anlz_res_t;
static anlz_res_t anlz_res[NDECK];          /* worker writes while state==1, tick after ==2/3 */
static uint32_t   anlz_state[NDECK];        /* atomic: 0 idle, 1 queued/running, 2 ok, 3 failed */
static uint32_t   anlz_cid[NDECK];          /* atomic: requested content id */
static uint32_t   anlz_worker;              /* atomic: 1 = a worker thread runs */

/* path index: built once per stick root (worker-only) */
typedef struct { uint64_t h; uint32_t dir_off; } ix_t;
static char    ix_root[256];
static ix_t   *ix_ent;  static int ix_n, ix_cap;
static char   *ix_pool; static size_t ix_pool_n, ix_pool_cap;
static uint64_t fnv(const char *s)
{
    uint64_t h = 1469598103934665603ull;
    for (; *s; s++) { h ^= (uint8_t)*s; h *= 1099511628211ull; }
    return h;
}
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }

/* read at most cap bytes of a file, O_RDONLY */
static long slurp(const char *path, uint8_t *buf, size_t cap)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;
    size_t got = 0;
    while (got < cap) {
        ssize_t r = read(fd, buf + got, cap - got);
        if (r < 0) { if (errno == EINTR) continue; break; }
        if (r == 0) break;
        got += (size_t)r;
    }
    close(fd);
    return (long)got;
}
/* PPTH (UTF-16BE) -> UTF-8 */
static int anlz_ppth(const uint8_t *b, long n, char *out, size_t cap)
{
    if (n < 28 || memcmp(b, "PMAI", 4)) return -1;
    uint32_t off = be32(b + 4);
    while (off + 12 <= (uint32_t)n) {
        const uint8_t *t = b + off;
        uint32_t lh = be32(t + 4), lt = be32(t + 8);
        if (lt < 12 || off + lt > (uint32_t)n) return -1;
        if (!memcmp(t, "PPTH", 4) && lh >= 16) {
            uint32_t len = be32(t + 12);
            const uint8_t *s = t + lh;
            size_t o = 0;
            if (lh + len > lt) len = lt - lh;
            for (uint32_t i = 0; i + 1 < len && o + 4 < cap; i += 2) {
                uint32_t c = be16(s + i);
                if (c >= 0xd800 && c < 0xdc00 && i + 3 < len) {
                    uint32_t c2 = be16(s + i + 2);
                    c = 0x10000 + ((c - 0xd800) << 10) + (c2 - 0xdc00); i += 2;
                }
                if (!c) break;
                if (c < 0x80) out[o++] = (char)c;
                else if (c < 0x800) { out[o++] = (char)(0xc0 | c >> 6); out[o++] = (char)(0x80 | (c & 63)); }
                else if (c < 0x10000) { out[o++] = (char)(0xe0 | c >> 12); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
                else { out[o++] = (char)(0xf0 | c >> 18); out[o++] = (char)(0x80 | ((c >> 12) & 63)); out[o++] = (char)(0x80 | ((c >> 6) & 63)); out[o++] = (char)(0x80 | (c & 63)); }
            }
            out[o] = 0;
            return 0;
        }
        off += lt;
    }
    return -1;
}
static void ix_add(uint64_t h, const char *dir)
{
    size_t l = strlen(dir) + 1;
    if (ix_n == ix_cap) {
        int nc = ix_cap ? ix_cap * 2 : 1024;
        ix_t *ne = realloc(ix_ent, (size_t)nc * sizeof *ne);
        if (!ne) return;
        ix_ent = ne; ix_cap = nc;
    }
    if (ix_pool_n + l > ix_pool_cap) {
        size_t nc = ix_pool_cap ? ix_pool_cap * 2 : 65536;
        while (nc < ix_pool_n + l) nc *= 2;
        char *np = realloc(ix_pool, nc);
        if (!np) return;
        ix_pool = np; ix_pool_cap = nc;
    }
    memcpy(ix_pool + ix_pool_n, dir, l);
    ix_ent[ix_n].h = h; ix_ent[ix_n].dir_off = (uint32_t)ix_pool_n; ix_n++;
    ix_pool_n += l;
}
static uint8_t anlz_buf[1 << 20];   /* worker-only */
static int ix_build(const char *root)
{
    char p1[512], p2[800], p3[1200], pp[1024];
    ix_n = 0; ix_pool_n = 0;
    snprintf(ix_root, sizeof ix_root, "%s", root);
    snprintf(p1, sizeof p1, "%s/PIONEER/USBANLZ", root);
    DIR *d1 = opendir(p1);
    if (!d1) return -1;
    struct dirent *a, *b;
    while ((a = readdir(d1))) {
        if (a->d_name[0] == '.') continue;
        snprintf(p2, sizeof p2, "%s/%s", p1, a->d_name);
        DIR *d2 = opendir(p2);
        if (!d2) continue;
        while ((b = readdir(d2))) {
            if (b->d_name[0] == '.') continue;
            snprintf(p3, sizeof p3, "%s/%s/ANLZ0000.DAT", p2, b->d_name);
            long n = slurp(p3, anlz_buf, 4096);      /* PPTH sits in the first tags */
            if (n > 0 && anlz_ppth(anlz_buf, n, pp, sizeof pp) == 0 && pp[0]) {
                snprintf(p3, sizeof p3, "%s/%s", p2, b->d_name);
                ix_add(fnv(pp), p3);
            }
        }
        closedir(d2);
    }
    closedir(d1);
    return ix_n;
}
/* loops from one ANLZ file: PCO2 (EXT) or PCOB (DAT) */
static int anlz_loops(const uint8_t *b, long n, al_t *e, int cap, int *nn)
{
    if (n < 28 || memcmp(b, "PMAI", 4)) return -1;
    uint32_t off = be32(b + 4);
    int found = 0;
    while (off + 12 <= (uint32_t)n) {
        const uint8_t *t = b + off;
        uint32_t lh = be32(t + 4), lt = be32(t + 8);
        if (lt < 12 || off + lt > (uint32_t)n) break;
        int pco2 = !memcmp(t, "PCO2", 4), pcob = !memcmp(t, "PCOB", 4);
        if ((pco2 || pcob) && lh >= 20) {
            found = 1;
            uint32_t q = lh;
            while (q + 12 <= lt) {
                const uint8_t *c = t + q;
                uint32_t le = be32(c + 8);
                if (le < 12 || q + le > lt) break;
                if (pco2 && !memcmp(c, "PCP2", 4) && le >= 0x1c) {
                    uint32_t hot = be32(c + 0x0c), ty = c[0x10], tin = be32(c + 0x14), tout = be32(c + 0x18);
                    if (ty == 2 && tout != 0xffffffffu && tout > tin && *nn < cap) e[(*nn)++] = (al_t){ (int32_t)hot, tin, tout, 0 };
                } else if (pcob && !memcmp(c, "PCPT", 4) && le >= 0x28) {
                    uint32_t hot = be32(c + 0x0c), ty = c[0x1c], tin = be32(c + 0x20), tout = be32(c + 0x24);
                    /* 0.2.2: PCPT +0x10 status: 4 = rekordbox "Active Loop" (exactly one loop per
                     * track in all 36 DAT files with loops that we have; 0 on every other cue) */
                    int32_t act = be32(c + 0x10) == 4u;
                    if (ty == 2 && tout != 0xffffffffu && tout > tin && *nn < cap) e[(*nn)++] = (al_t){ (int32_t)hot, tin, tout, act };
                }
                q += le;
            }
        }
        off += lt;
    }
    return found ? 0 : -1;
}
static void al_dedup(al_t *e, int *n)
{
    int m = 0;
    for (int i = 0; i < *n; i++) {
        int dup = 0;
        for (int j = 0; j < m; j++)
            if (e[j].hot == e[i].hot && e[j].in_ms == e[i].in_ms && e[j].out_ms == e[i].out_ms) { dup = 1; e[j].flag |= e[i].flag; break; }
        if (!dup) e[m++] = e[i];
    }
    *n = m;
}
typedef struct { uint32_t cid; char path[512]; int hit; } find_t;
static int find_cb(const az_track *t, void *user)
{
    find_t *f = user;
    if (t && t->id == f->cid && t->path) { snprintf(f->path, sizeof f->path, "%s", t->path); f->hit = 1; return 1; }
    return 0;
}
/* 0.2.4: loops + rekordbox flags of one ANLZ folder (EXT PCO2, DAT PCOB flags), read-only */
static int anlz_dir_loops(const char *dir, anlz_res_t *r)
{
    char p[700];
    int got = -1; r->n = 0;
    snprintf(p, sizeof p, "%s/ANLZ0000.EXT", dir);
    long n = slurp(p, anlz_buf, sizeof anlz_buf);
    if (n > 0 && anlz_loops(anlz_buf, n, r->e, NAL, &r->n) == 0) got = 0;
    /* 0.2.2: the Active Loop flag only exists in PCPT (PCOB); EXT's PCO2 has none, so the DAT
     * is always read too: its flags are copied onto the EXT entries (same in/out within 2 ms),
     * or its entries are used when the EXT gave no loops. */
    {
        static al_t dat[NAL]; int dn = 0;
        snprintf(p, sizeof p, "%s/ANLZ0000.DAT", dir);
        n = slurp(p, anlz_buf, sizeof anlz_buf);
        if (n > 0 && anlz_loops(anlz_buf, n, dat, NAL, &dn) == 0) {
            if (got != 0 || r->n == 0) { memcpy(r->e, dat, sizeof(al_t) * (size_t)dn); r->n = dn; got = 0; }
            else
                for (int i = 0; i < dn; i++) if (dat[i].flag)
                    for (int j = 0; j < r->n; j++) {
                        int64_t di = (int64_t)r->e[j].in_ms - dat[i].in_ms, dq = (int64_t)r->e[j].out_ms - dat[i].out_ms;
                        if (r->e[j].hot == dat[i].hot && di <= 2 && di >= -2 && dq <= 2 && dq >= -2) r->e[j].flag = 1;
                    }
        }
    }
    return got;
}
static int anlz_one(int d, uint32_t cid, anlz_res_t *r)
{
    static find_t f;
    memset(&f, 0, sizeof f); f.cid = cid;
    int stick = -1;
    for (int s = 0; s < 2 && !f.hit; s++)
        if (C->lib_tracks(MOD_ID, s, 0, find_cb, &f) >= 0 && f.hit) stick = s;
    if (!f.hit) { snprintf(r->msg, sizeof r->msg, "content id %u not in the library of USB 1/2", cid); return -1; }
    {   /* 0.2.5: a library row without its audio file: nothing to build (read-only probe) */
        int afd = open(f.path, O_RDONLY | O_CLOEXEC);
        if (afd < 0) { snprintf(r->msg, sizeof r->msg, "no audio file for content id %u (%.120s), build skipped", cid, f.path); return -1; }
        close(afd);
    }
    /* stick root: first parent with PIONEER/USBANLZ */
    char root[512], probe[600];
    snprintf(root, sizeof root, "%s", f.path);
    int ok = 0;
    for (char *sl = strrchr(root, '/'); sl && sl > root; sl = strrchr(root, '/')) {
        *sl = 0;
        snprintf(probe, sizeof probe, "%s/PIONEER/USBANLZ", root);
        DIR *pd = opendir(probe);                 /* (no stat: keeps the glibc floor at 2.17) */
        if (pd) { closedir(pd); ok = 1; break; }
    }
    if (!ok) { snprintf(r->msg, sizeof r->msg, "no PIONEER/USBANLZ above %.120s", f.path); return -1; }
    const char *rel = f.path + strlen(root);          /* "/Contents/..." as PPTH stores it */
    uint64_t h = fnv(rel);
    const char *dir = NULL;
    int built = 0;
    if (strcmp(ix_root, root) || !ix_n) { ix_build(root); built = 1; }
    for (int i = 0; i < ix_n && !dir; i++) if (ix_ent[i].h == h) dir = ix_pool + ix_ent[i].dir_off;
    if (!dir && !built) {                                /* new analysis since the index: once more */
        ix_build(root);
        for (int i = 0; i < ix_n && !dir; i++) if (ix_ent[i].h == h) dir = ix_pool + ix_ent[i].dir_off;
    }
    if (!dir) { snprintf(r->msg, sizeof r->msg, "no ANLZ with PPTH %.120s (%d indexed)", rel, ix_n); return -1; }
    int got = anlz_dir_loops(dir, r);
    al_dedup(r->e, &r->n);
    if (got != 0) { snprintf(r->msg, sizeof r->msg, "no cue list tag in %.150s", dir); return -1; }
    snprintf(r->msg, sizeof r->msg, "USB %d %.150s", stick + 1, dir + strlen(root));
    (void)d;
    return 0;
}
static void *anlz_main(void *arg)
{
    (void)arg;
    for (int again = 1; again; ) {
        again = 0;
        for (int d = 0; d < NDECK; d++) {
            if (__atomic_load_n(&anlz_state[d], __ATOMIC_ACQUIRE) != 1) continue;
            uint32_t cid = __atomic_load_n(&anlz_cid[d], __ATOMIC_ACQUIRE);
            anlz_res_t *r = &anlz_res[d];
            memset(r, 0, sizeof *r); r->cid = cid;
            int rc = anlz_one(d, cid, r);
            if (__atomic_load_n(&anlz_cid[d], __ATOMIC_ACQUIRE) != cid) { again = 1; continue; }  /* re-requested */
            __atomic_store_n(&anlz_state[d], rc == 0 ? 2u : 3u, __ATOMIC_RELEASE);
        }
        for (int d = 0; d < NDECK; d++) if (__atomic_load_n(&anlz_state[d], __ATOMIC_ACQUIRE) == 1) again = 1;
    }
    __atomic_store_n(&anlz_worker, 0, __ATOMIC_RELEASE);
    /* a request that raced the exit is picked up by the next anlz_kick */
    return NULL;
}
/* tick: request deck d's list (content id cid) */
static void anlz_request(int d, uint32_t cid)
{
    if (!cid) return;
    __atomic_store_n(&anlz_cid[d], cid, __ATOMIC_RELEASE);
    __atomic_store_n(&anlz_state[d], 1u, __ATOMIC_RELEASE);
}
static void anlz_kick(void)
{
    int want = 0;
    for (int d = 0; d < NDECK; d++) if (__atomic_load_n(&anlz_state[d], __ATOMIC_ACQUIRE) == 1) want = 1;
    if (!want || __atomic_load_n(&anlz_worker, __ATOMIC_ACQUIRE)) return;
    if (!AZ_HAS(C, thread_start) || !AZ_HAS(C, lib_tracks)) {
        for (int d = 0; d < NDECK; d++)
            if (__atomic_load_n(&anlz_state[d], __ATOMIC_ACQUIRE) == 1) {
                snprintf(anlz_res[d].msg, sizeof anlz_res[d].msg, "core has no lib_tracks/thread_start");
                __atomic_store_n(&anlz_state[d], 3u, __ATOMIC_RELEASE);
            }
        return;
    }
    __atomic_store_n(&anlz_worker, 1u, __ATOMIC_RELEASE);
    if (C->thread_start(MOD_ID, "al-anlz", anlz_main, NULL) != 0) {
        __atomic_store_n(&anlz_worker, 0, __ATOMIC_RELEASE);
        LOG("anlz: thread_start refused; loops known only once called/stored");
        for (int d = 0; d < NDECK; d++) if (__atomic_load_n(&anlz_state[d], __ATOMIC_ACQUIRE) == 1)
            __atomic_store_n(&anlz_state[d], 0, __ATOMIC_RELEASE);
    }
}

/* ===================== 0.2.0: active-loop selection + re-arm ======================
 * Set/clear are queued per deck and performed by the engage-check hook (0x14b37a0) on
 * EP147's own playhead thread, right before the firmware's check; if that hook is idle
 * (deck stopped) for ~0.5 s the tick performs it (the 0.1.x path that is proven to work).
 */
enum { OP_IDLE = 0, OP_SET = 1, OP_CLEAR = 2, OP_BUSY = 3, OP_DONE = 4 };
static uint8_t  set_buf[NDECK][CUE_SIZE] __attribute__((aligned(64)));
static uint8_t  set_ok[NDECK];
static uint32_t pend_op[NDECK] __attribute__((aligned(16)));   /* atomic */
static int64_t  arm_in[NDECK] __attribute__((aligned(16)));   /* tick writes, hook reads */
static uint32_t hk_need[NDECK];                               /* tick writes, hook reads */
/* 0.2.4: holder-slot snapshot: the tick asks (1), the playhead thread copy-constructs the
 * holder's own CueProperty (holder+0x38) into snap_buf with the firmware copy (same thread that
 * runs set/engage) and publishes (2). A firmware-constructed, firmware-owned-layout base. */
static uint8_t  snap_buf[NDECK][CUE_SIZE] __attribute__((aligned(64)));
static uint32_t snap_state[NDECK];
static long     snap_tick[NDECK];
static long     pend_tick[NDECK];
static const char *pend_why[NDECK];
static void    *hold_pub[NDECK];          /* atomic: holder per deck, for the hook */
static int64_t  pos_now[NDECK] __attribute__((aligned(16)));   /* atomic: playhead from hook */
static uint32_t pos_seq[NDECK];           /* 0.2.2: tick-only, 1 once the hook wrote pos_now */
#define POS_UNSET INT64_MIN
static int pos_seen(int d)
{
    if (!pos_seq[d] && __atomic_load_n(&pos_now[d], __ATOMIC_RELAXED) != POS_UNSET) pos_seq[d] = 1;
    return pos_seq[d] != 0;
}
static uint32_t rearm_cnt[NDECK];           /* atomic */
static int      act_src_hot[NDECK];       /* tick: active came from a hot cue */
static int      tk_engaged_ours[NDECK];

typedef void *(*pos_in_fn)(void *cue);
typedef int   (*int_fn)(void *obj);
typedef void  (*engage_fn)(void *holder);
typedef void  (*echeck_fn)(void *holder, void *prev, void *newp, unsigned jump);
static pos_in_fn fw_pos_in;
static int_fn    fw_valid_loop, fw_1508, fw_1525;
static engage_fn fw_engage;
static echeck_fn orig_echeck;

static void active_note(int d, const uint8_t *cue, int hot)
{
    active_in[d] = cue_in(cue); active_out[d] = cue_out(cue);
    active_cid[d] = deck_cid(d);
    act_src_hot[d] = hot;
    if (act_ok[d]) fw_cue_dtor(act_buf[d]);
    memset(act_buf[d], 0, CUE_SIZE);
    fw_cue_copy(act_buf[d], cue);
    act_ok[d] = 1;
    tk_engaged_ours[d] = 0;
}
static void fmt_time(char *b, size_t n, int64_t samples)
{
    long ds = (long)(samples / 4410);               /* EP147 cue times are 44.1 kHz samples */
    snprintf(b, n, "%02ld:%02ld.%ld", ds / 600, (ds / 10) % 60, ds % 10);
}
/* banner text must stay valid while queued: rotate static buffers */
static void toastf(const char *fmt, const char *a)
{
    static char ring_txt[8][112]; static int k;
    char *b = ring_txt[k++ & 7];
    snprintf(b, 112, fmt, a);
    do_toast_ms(b, 1500, -1);
}
static int request(int d, int op, const void *cue, const char *why)
{
    uint32_t cur = __atomic_load_n(&pend_op[d], __ATOMIC_ACQUIRE);
    if (cur == OP_BUSY) return -1;                  /* hook mid-way: retry next tick */
    if (cur == OP_SET || cur == OP_CLEAR) {
        if (!__atomic_compare_exchange_n(&pend_op[d], &cur, OP_IDLE, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
            return -1;
    }
    if (op == OP_SET) {
        if (set_ok[d]) { fw_cue_dtor(set_buf[d]); set_ok[d] = 0; }
        memset(set_buf[d], 0, CUE_SIZE);
        fw_cue_copy(set_buf[d], cue);
        set_ok[d] = 1;
    }
    pend_tick[d] = tick_no; pend_why[d] = why;
    __atomic_store_n(&pend_op[d], (uint32_t)op, __ATOMIC_RELEASE);
    __atomic_store_n(&hk_need[d], 1u, __ATOMIC_RELEASE);
    return 0;
}
static void log_set_check(int d)
{
    void *h = holder_of(d), *ls = ls_of[d];
    int reg = -1, looping = -1, held = h ? fw_is_loop((uint8_t *)h + 0x38) : -1;
    uint64_t probe;
    if (plausible((uintptr_t)ls) && !safe_read((uintptr_t)ls, &probe, 8)) {
        reg = fw_ls_has_loop(ls) & 0xff; looping = fw_ls_looping(ls) & 0xff;
    }
    LOG("set check deck %d: holder loop=%d, LoopState has-loop=%d looping=%d", d + 1, held, reg, looping);
}
/* tick: finish / fall back */
static void pend_service(void)
{
    for (int d = 0; d < NDECK; d++) {
        uint32_t op = __atomic_load_n(&pend_op[d], __ATOMIC_ACQUIRE);
        if (op == OP_DONE) {
            __atomic_store_n(&pend_op[d], OP_IDLE, __ATOMIC_RELEASE);
            LOG("deck %d: %s applied on the firmware playhead thread", d + 1, pend_why[d]);
            log_set_check(d);
        } else if ((op == OP_SET || op == OP_CLEAR) && (!orig_echeck || tick_no - pend_tick[d] > 30)) {
            uint32_t exp = op;
            if (!__atomic_compare_exchange_n(&pend_op[d], &exp, OP_BUSY, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                continue;
            void *h = holder_of(d);
            if (h) { if (op == OP_SET) fw_set(h, set_buf[d]); else fw_clear(h); }
            __atomic_store_n(&hk_need[d], 0, __ATOMIC_RELAXED);
            __atomic_store_n(&pend_op[d], OP_IDLE, __ATOMIC_RELEASE);
            LOG("deck %d: %s applied from tick (playhead hook idle, deck stopped?)", d + 1, pend_why[d]);
            log_set_check(d);
        }
    }
}

/* ---- the engage-check hook: EP147 playhead thread (JUCE ALSA), every move ----
 * Firmware 0x14b37a0(holder, prev, new, isJump): engage (0x14b31f0) only if the active loop
 * is valid, LoopState has NO loop and is not looping, holder[2]+0x78 == 0, 0x1508a28 == -1,
 * !0x1525c28, mode == 0, not a jump, same source and prev < in <= new.
 * Re-arm: when the only blocker is a retained (registered, not looping) LoopState loop that
 * differs from the active loop, run the firmware's own engage 0x14b31f0, which replaces
 * LoopState's loop with the active one and starts it (what the first pass does).
 * 0.2.1: this runs on the audio-side thread, so the cheap in-point crossing test comes first
 * and the firmware predicates are only called on the move that crosses the in point; no
 * lock of ours is taken here (none exists); own time and firmware time are self-measured. */
/* 0.2.2 hot path: <= 4 pointer compares, 1 relaxed store, 3 relaxed loads, 2 int64
 * compares. No RMW atomics, no firmware call, no deref of holder/prev beyond the playhead
 * fields the firmware's own check reads. arm_in[d] is the Active Loop's in point published
 * by the tick (INT64_MAX = nothing to re-arm); hk_need[d] != 0 when a set/clear is queued. */
static uint32_t hmax_ec_rare;                                 /* rare path, cntvct ticks */
static uint32_t hk_samp;                                      /* hook-only sample counter */
/* 0.2.3: the deck's playhead Position source key (+8 u64, +16 u32), captured by the hook
 * once per load (src_ok: tick clears on load, hook sets; plain stores, no RMW) */
static uint64_t src_k8[NDECK];
static uint32_t src_k16[NDECK], src_ok[NDECK];
static __attribute__((noinline)) int echeck_rare(int d, void *holder, uint8_t *P, uint8_t *N, unsigned jump)
{
    uint64_t t0 = cnt_now();
    uint8_t *H = holder;
    uint32_t op = __atomic_load_n(&pend_op[d], __ATOMIC_ACQUIRE);
    if ((op == OP_SET || op == OP_CLEAR) &&
        __atomic_compare_exchange_n(&pend_op[d], &op, OP_BUSY, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        if (op == OP_SET) fw_set(holder, set_buf[d]); else fw_clear(holder);
        __atomic_store_n(&pend_op[d], OP_DONE, __ATOMIC_RELEASE);
    }
    if (__atomic_load_n(&snap_state[d], __ATOMIC_ACQUIRE) == 1) {
        memset(snap_buf[d], 0, CUE_SIZE);
        fw_cue_copy(snap_buf[d], H + 0x38);
        __atomic_store_n(&snap_state[d], 2u, __ATOMIC_RELEASE);
    }
    __atomic_store_n(&hk_need[d], 0, __ATOMIC_RELAXED);   /* tick re-raises if still needed */
    int took = 0;
    uint32_t cfg = __atomic_load_n(&cfg_bits, __ATOMIC_RELAXED);
    uint8_t *in = H + 0x38 + 0x28;                     /* = fw_pos_in(H + 0x38) */
    if ((cfg & CFG_ENABLE) && (cfg & CFG_REARM) && !(jump & 0xffu) &&
        *(int64_t *)(P + 24) < *(int64_t *)(in + 24) && *(int64_t *)(N + 24) >= *(int64_t *)(in + 24) &&
        *(uint64_t *)(in + 8) == *(uint64_t *)(N + 8) && *(uint32_t *)(in + 16) == *(uint32_t *)(N + 16) &&
        *(uint64_t *)(in + 8) == *(uint64_t *)(P + 8) && *(uint32_t *)(in + 16) == *(uint32_t *)(P + 16) &&
        (fw_valid_loop(H + 0x38) & 0xff)) {
        void *ls = *(void **)(H + 8);
        const uint8_t *rl = (const uint8_t *)ls + 0x160;
        if (ls && (fw_ls_has_loop(ls) & 0xff) && !(fw_ls_looping(ls) & 0xff) &&
            !*(*(uint8_t **)(H + 16) + 120) && fw_1508(*(void **)(H + 32)) == -1 &&
            !(fw_1525(*(void **)(H + 40)) & 0xff) && *(int32_t *)(*(uint8_t **)(H + 24) + 12) == 0 &&
            (cue_in(rl) != cue_in(H + 0x38) || cue_out(rl) != cue_out(H + 0x38))) {
            fw_engage(holder);
            __atomic_store_n(&rearm_cnt[d], __atomic_load_n(&rearm_cnt[d], __ATOMIC_RELAXED) + 1, __ATOMIC_RELAXED);
            took = 1;
        }
    }
    max_upd(&hmax_ec_rare, cnt_now() - t0);
    return took;
}
static void my_echeck(void *holder, void *prev, void *newp, unsigned jump)
{
    if (__builtin_expect(!__atomic_load_n(&al_live, __ATOMIC_ACQUIRE), 0)) {
        orig_echeck(holder, prev, newp, jump);
        return;
    }
    uint32_t sm = ++hk_samp;                  /* plain: this hook runs on one thread */
    uint64_t t0 = (sm & 63u) == 0 ? cnt_now() : 0;
    int d = -1;
    if (__atomic_load_n(&hold_pub[0], __ATOMIC_RELAXED) == holder) d = 0;
    else if (__atomic_load_n(&hold_pub[1], __ATOMIC_RELAXED) == holder) d = 1;
    else if (__atomic_load_n(&hold_pub[2], __ATOMIC_RELAXED) == holder) d = 2;
    else if (__atomic_load_n(&hold_pub[3], __ATOMIC_RELAXED) == holder) d = 3;
    if (d >= 0) {
        int64_t np = *(int64_t *)((uint8_t *)newp + 24), pp = *(int64_t *)((uint8_t *)prev + 24);
        __atomic_store_n(&pos_now[d], np, __ATOMIC_RELAXED);
        if (__builtin_expect(!__atomic_load_n(&src_ok[d], __ATOMIC_ACQUIRE), 0)) {
            __atomic_store_n(&src_k8[d], *(uint64_t *)((uint8_t *)newp + 8), __ATOMIC_RELAXED);
            __atomic_store_n(&src_k16[d], *(uint32_t *)((uint8_t *)newp + 16), __ATOMIC_RELAXED);
            __atomic_store_n(&src_ok[d], 1u, __ATOMIC_RELEASE);
        }
        int64_t a = __atomic_load_n(&arm_in[d], __ATOMIC_RELAXED);
        if (__builtin_expect(__atomic_load_n(&hk_need[d], __ATOMIC_RELAXED) != 0 || (pp < a && np >= a), 0)) {
            if (echeck_rare(d, holder, prev, newp, jump)) return;   /* re-armed: engaged already */
        }
    }
    if (t0) max_upd(&hmax_ec_own, cnt_now() - t0);
    orig_echeck(holder, prev, newp, jump);
    if (t0) max_upd(&hmax_ec_all, cnt_now() - t0);
}

/* playhead in cue units: hook value, else deck_info seconds * 44100 */
static int64_t playhead(int d)
{
    if (pos_seen(d)) return __atomic_load_n(&pos_now[d], __ATOMIC_RELAXED);
    az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
    if (AZ_HAS(C, deck_info) && C->deck_info(d, &di) == 0 && di.loaded) return (int64_t)(di.pos_s * 44100.0);
    return -1;
}
/* candidates: live (or ANLZ-built) memory loops + hot cue loops of this track */
static const uint8_t *next_ahead(int d, int64_t after, int *is_hot, int *idx)
{
    const uint8_t *best = NULL; int64_t bi = 0;
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_ok[d][i] && mem_live[d][i] && fw_is_loop(mem_buf[d][i]) && cue_in(mem_buf[d][i]) > after &&
            (!best || cue_in(mem_buf[d][i]) < bi)) { best = mem_buf[d][i]; bi = cue_in(best); *is_hot = 0; *idx = i; }
    for (int k = 1; k < NHOT; k++)
        if (hot_ok[d][k] && fw_is_loop(hot_buf[d][k]) && cue_in(hot_buf[d][k]) > after &&
            (!best || cue_in(hot_buf[d][k]) < bi)) { best = hot_buf[d][k]; bi = cue_in(best); *is_hot = 1; *idx = k; }
    return best;
}
/* 0.2.2: earliest rekordbox-flagged memory loop with in > after */
static const uint8_t *next_flagged(int d, int64_t after, int *idx)
{
    const uint8_t *best = NULL;
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_ok[d][i] && mem_live[d][i] && mem_flag[d][i] && fw_is_loop(mem_buf[d][i]) && cue_in(mem_buf[d][i]) > after &&
            (!best || cue_in(mem_buf[d][i]) < cue_in(best))) { best = mem_buf[d][i]; *idx = i; }
    return best;
}
static int count_known(int d, int64_t after)
{
    int n = 0;
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_ok[d][i] && mem_live[d][i] && fw_is_loop(mem_buf[d][i]) && cue_in(mem_buf[d][i]) > after) n++;
    for (int k = 1; k < NHOT; k++)
        if (hot_ok[d][k] && fw_is_loop(hot_buf[d][k]) && cue_in(hot_buf[d][k]) > after) n++;
    return n;
}
static int spent[NDECK], ex_inside[NDECK], ex_past[NDECK];
/* 0.2.7: one-shot diagnostics per armed loop (AZ-EMU D3 run: armed, never inside) */
static int      dg_near[NDECK], dg_stall[NDECK];
static int64_t  dg_pos[NDECK];
static uint64_t dg_ns[NDECK];
static void pick_and_set(int d, const uint8_t *c, int hot, int idx, const char *why)
{
    char t[32]; fmt_time(t, sizeof t, cue_in(c));
    if (request(d, OP_SET, c, why) != 0) { LOG("deck %d: set busy, try again", d + 1); return; }
    active_note(d, c, hot);
    spent[d] = 0; ex_inside[d] = 0; ex_past[d] = 0;
    dg_near[d] = 0; dg_stall[d] = 0; dg_pos[d] = -1; dg_ns[d] = 0;
    if (!hot) last_mem[d] = idx;
    LOG("deck %d: Active Loop <- %s %d%s at %s in=%lld out=%lld (%s), queued for the playhead thread",
        d + 1, hot ? "hot cue" : "memory", idx, (!hot && mem_syn[d][idx]) ? " (from ANLZ)" : "", t,
        (long long)cue_in(c), (long long)cue_out(c), why);
    toastf(hot ? "Active Loop: hot cue at %s" : "Active Loop: memory loop at %s", t);
}

/* SHIFT+MEMORY: cycles through the stored loops ahead, starting after the queued/Active one
 * (not after the playhead): 1st press = first ahead, each repeat = the next, then wraps. */
static void apply_set(int d)
{
    if (!holder_of(d)) { LOG("pick abort: deck %d not bound", d + 1); do_toast("Active Loop: deck not ready"); return; }
    int64_t pos = playhead(d);
    if (pos < 0) { LOG("pick abort: deck %d playhead unknown", d + 1); do_toast("Active Loop: deck not ready"); return; }
    int64_t after = pos;
    int cur = act_ok[d] && !spent[d];
    if (cur && active_in[d] > pos) after = active_in[d];
    int hot = 0, idx = -1, wrapped = 0, n = count_known(d, pos);
    const uint8_t *c = next_ahead(d, after, &hot, &idx);
    if (!c && after != pos) { c = next_ahead(d, pos, &hot, &idx); wrapped = 1; }
    char tp[32], ta[32]; fmt_time(tp, sizeof tp, pos); fmt_time(ta, sizeof ta, after);
    if (!c) {
        LOG("pick deck %d: no known stored loop ahead of %s (%d known on this track)", d + 1, tp, count_known(d, -1));
        do_toast("Active Loop: no stored loop ahead");
        return;
    }
    if (cur && near(cue_in(c), active_in[d]) && near(cue_out(c), active_out[d])) {
        LOG("pick deck %d: the Active Loop is the only stored loop ahead of %s", d + 1, tp);
        char t[32]; fmt_time(t, sizeof t, cue_in(c));
        toastf("Active Loop: only loop ahead (%s)", t);
        return;
    }
    LOG("pick deck %d: %d loops ahead of playhead %s; cycling after %s%s", d + 1, n, tp,
        cur && after != pos ? ta : "the playhead", wrapped ? " -> wrapped to the first" : "");
    pick_and_set(d, c, hot, idx, "SHIFT+MEMORY pick");
}
/* restore keeps the 0.1.x signature */
static void set_active_mem(int d, int i, const char *why)
{
    if (!mem_ok[d][i]) { LOG("set abort: deck %d memory #%d not constructed", d + 1, i); return; }
    pick_and_set(d, mem_buf[d][i], 0, i, why);
}
static void apply_clear(int d)
{
    if (!holder_of(d)) { LOG("clear abort: deck %d not bound", d + 1); do_toast("Active Loop: deck not ready"); return; }
    if (request(d, OP_CLEAR, NULL, "clear") != 0) { LOG("deck %d: clear busy, try again", d + 1); return; }
    if (act_ok[d]) { fw_cue_dtor(act_buf[d]); act_ok[d] = 0; }
    active_cid[d] = 0; restore_pending[d] = 0; spent[d] = 0;
    LOG("cleared deck %d (queued)", d + 1);
    do_toast("Active Loop cleared");
}

static void *live_hot(int d, int kind)
{
    void *ls = ls_of[d];
    uint64_t probe;
    if (!plausible((uintptr_t)ls) || !fw_loop_slot || safe_read((uintptr_t)ls, &probe, 8)) return NULL;
    void *c = fw_loop_slot(ls, kind);
    return plausible((uintptr_t)c) ? c : NULL;
}

/* SHIFT+pad: that hot cue loop becomes the Active Loop directly (its own CueProperty). */
static void apply_copy_pad(int d, int kind)
{
    if (!holder_of(d)) { LOG("pad abort: deck %d not bound", d + 1); do_toast("Active Loop: deck not ready"); return; }
    if (kind < 1 || kind > 8) { LOG("pad abort: bad kind %d", kind); return; }
    if (!(hot_ok[d][kind] && fw_is_loop(hot_buf[d][kind]))) {
        void *live = live_hot(d, kind);
        if (live && fw_is_loop(live)) hot_put(d, kind, live);
    }
    if (!(hot_ok[d][kind] && fw_is_loop(hot_buf[d][kind]))) {
        LOG("pad abort: hot cue %d is not a loop", kind); do_toast("Active Loop: pad is not a loop"); return;
    }
    pick_and_set(d, hot_buf[d][kind], 1, kind, "SHIFT+pad hot cue");
}

/* tick: re-arm log; 0.2.1 exit detection from the playhead itself.
 * 0.2.0 waited for LoopState 'looping' 1->0 on a loop equal to ours, but has-loop read 0 even
 * while looping (set check: has-loop=0 looping=1), so 'ours' never held and nothing fired.
 * Now: inside = playhead within [in, out + 150 ms]; exited = after being inside, the
 * playhead is beyond out + 150 ms on 3 ticks in a row. On exit: Auto-advance picks the next
 * stored loop ahead; otherwise (or nothing ahead) the holder is cleared on the playhead thread
 * so the exited loop cannot re-trap on a later pass. Every decision is logged. */
#define EXIT_TOL 6615          /* 150 ms of 44.1 kHz cue units */
static void watch_loops(void)
{
    for (int d = 0; d < NDECK; d++) {
        uint32_t n = __atomic_exchange_n(&rearm_cnt[d], 0, __ATOMIC_RELAXED);
        if (n) LOG("deck %d: re-armed: LoopState's old loop replaced by the Active Loop at its in point (%u)", d + 1, n);
        if (!act_ok[d] || spent[d]) continue;
        uint32_t op = __atomic_load_n(&pend_op[d], __ATOMIC_ACQUIRE);
        if (op != OP_IDLE) continue;                    /* set still queued */
        int64_t pos = playhead(d);
        if (pos < 0) continue;
        int64_t in = active_in[d], out = active_out[d];
        char ti[32], to[32], tp[32];
        if (pos < in) {
            uint64_t tn = now_ns();
            if (pos != dg_pos[d]) { dg_pos[d] = pos; dg_ns[d] = tn; dg_stall[d] = 0; }
            else if (dg_ns[d] && tn - dg_ns[d] > 10000000000ull && !dg_stall[d]) {
                dg_stall[d] = 1;
                fmt_time(ti, sizeof ti, in); fmt_time(tp, sizeof tp, pos);
                LOG("deck %d: Active Loop at %s armed, playhead not moving at %s for 10 s (deck stopped?)", d + 1, ti, tp);
            }
            if (!dg_near[d] && in - pos <= 5 * 44100) {
                dg_near[d] = 1;
                fmt_time(ti, sizeof ti, in); fmt_time(tp, sizeof tp, pos);
                LOG("deck %d: playhead %s, 5 s or less before the Active Loop at %s", d + 1, tp, ti);
            }
        }
        if (pos >= in && pos <= out + EXIT_TOL) {
            if (!ex_inside[d]) {
                fmt_time(ti, sizeof ti, in); fmt_time(to, sizeof to, out);
                LOG("deck %d: playhead inside the Active Loop %s-%s", d + 1, ti, to);
            }
            ex_inside[d] = 1; ex_past[d] = 0;
        } else if (pos > out + EXIT_TOL) {
            if (!ex_inside[d]) continue;                /* passed over by a jump: still Active */
            if (++ex_past[d] < 3) continue;
            fmt_time(ti, sizeof ti, in); fmt_time(to, sizeof to, out); fmt_time(tp, sizeof tp, pos);
            LOG("deck %d: Active Loop %s-%s exited (playhead %s, past out)", d + 1, ti, to, tp);
            int adv = (__atomic_load_n(&cfg_bits, __ATOMIC_RELAXED) & CFG_AUTOADV) != 0;
            if (adv) {
                int hot = 0, idx = -1;
                int64_t from = pos > out ? pos : out;
                const uint8_t *c = next_flagged(d, from, &idx);
                const char *src = "rekordbox flag";
                if (c) hot = 0; else { c = next_ahead(d, from, &hot, &idx); src = "fallback"; }
                if (c) {
                    char tn[32]; fmt_time(tn, sizeof tn, cue_in(c));
                    LOG("auto-advance deck %d: next loop ahead at %s (%d ahead), source: %s", d + 1, tn, count_known(d, pos), src);
                    pick_and_set(d, c, hot, idx, "auto-advance");
                    continue;
                }
                LOG("auto-advance deck %d: no stored loop ahead of %s (%d known) -> clearing the exited loop",
                    d + 1, tp, count_known(d, -1));
            } else {
                LOG("auto-advance off: deck %d clearing the exited loop (no re-trap)", d + 1);
            }
            if (request(d, OP_CLEAR, NULL, "exit clear") != 0) { LOG("deck %d: exit clear busy, retry", d + 1); continue; }
            spent[d] = 1; ex_inside[d] = 0; ex_past[d] = 0;
            restore_pending[d] = 0;
            do_toast(adv ? "Active Loop done (none ahead)" : "Active Loop done");
        } else {
            if (ex_inside[d]) {
                fmt_time(tp, sizeof tp, pos);
                LOG("deck %d: playhead jumped back before the Active Loop (%s): still Active", d + 1, tp);
            }
            ex_inside[d] = 0; ex_past[d] = 0;
        }
    }
}

/* 0.2.1: rows reduced to Active Loop / Auto-advance / Re-arm; combos, pad, clear and toasts
 * are always on while Active Loop is on. */
static void sync_cfg(void)
{
    uint32_t b = CFG_ENABLE | CFG_HOTCUE | CFG_CLEAR | CFG_TOAST | CFG_REARM | CFG_AUTOADV;
    if (AZ_HAS(C, row_value)) {
        if (h_enable >= 0 && C->row_value(h_enable) != 1) b &= ~CFG_ENABLE;
        if (h_autoadv >= 0 && C->row_value(h_autoadv) != 1) b &= ~CFG_AUTOADV;
        if (h_rearm >= 0 && C->row_value(h_rearm) != 1) b &= ~CFG_REARM;
    }
    b |= ((uint32_t)COMBO_SHIFT_MEMORY & 3u) << 1;
    __atomic_store_n(&cfg_bits, b, __ATOMIC_RELAXED);
}

/* tick: hand ANLZ loops to the cache. Memory loops not seen live yet get a CueProperty built
 * from a constructed loop cue of the same track (firmware copy-ctor, so its Position source
 * info and refcounts are the firmware's own), with only the in/out sample positions changed.
 * Units: calibrated per track from a hot cue loop that is in both lists (cue units / ms),
 * else from the playhead hook vs deck_info seconds, else 44.1 per ms. */
static uint32_t anlz_for[NDECK];     /* content id requested for this load (0 = not yet) */
static int      anlz_merged[NDECK], anlz_wait[NDECK], anlz_parsed_logged[NDECK];
static uint8_t  syn_tmp[CUE_SIZE] __attribute__((aligned(64)));
static double units_shared;          /* 0.2.3: last calibrated units/ms, any deck */
static double units_per_ms(int d, const anlz_res_t *r, char *how, size_t hn)
{
    for (int i = 0; i < r->n; i++) {
        int k = r->e[i].hot;
        if (k >= 1 && k <= 8 && hot_ok[d][k] && fw_is_loop(hot_buf[d][k]) && r->e[i].in_ms > 1000) {
            double u = (double)cue_in(hot_buf[d][k]) / (double)r->e[i].in_ms;
            if (u > 20.0 && u < 200.0) { snprintf(how, hn, "hot cue %d", k); units_shared = u; return u; }
        }
    }
    az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
    if (pos_seen(d) && AZ_HAS(C, deck_info) && C->deck_info(d, &di) == 0 && di.pos_s > 5.0) {
        double u = (double)__atomic_load_n(&pos_now[d], __ATOMIC_RELAXED) / (di.pos_s * 1000.0);
        if (u > 20.0 && u < 200.0) { snprintf(how, hn, "playhead"); units_shared = u; return u; }
    }
    if (units_shared > 20.0) { snprintf(how, hn, "shared from another deck"); return units_shared; }
    snprintf(how, hn, "default 44.1 kHz");
    return 44.1;
}
/* 0.2.3 template order: (a) a loop cue of this deck (cache), (b) a loop from this deck's
 * LoopState hot cue slots, (c) any other deck's loop cue with this deck's Position source key
 * (from the playhead hook) written into its in/out Positions. The engage check compares only
 * that key (+8/+16) with the playhead, and in/out samples are always ours. */
static uint8_t xtpl[CUE_SIZE] __attribute__((aligned(64)));
static int     xtpl_ok;
static const uint8_t *own_template(int d)
{
    for (int k = 1; k < NHOT; k++) if (hot_ok[d][k] && fw_is_loop(hot_buf[d][k])) return hot_buf[d][k];
    for (int i = 0; i < mem_n[d]; i++) if (mem_ok[d][i] && !mem_syn[d][i] && fw_is_loop(mem_buf[d][i])) return mem_buf[d][i];
    return NULL;
}
static const uint8_t *loop_template_how(int d, char *how, size_t hn)
{
    const uint8_t *t = own_template(d);
    if (t) { snprintf(how, hn, "deck %d loop cue", d + 1); return t; }
    for (int k = 1; k < NHOT; k++) {
        void *live = live_hot(d, k);
        if (live && fw_is_loop(live)) { hot_put(d, k, live); snprintf(how, hn, "deck %d LoopState hot cue %d", d + 1, k); return hot_buf[d][k]; }
    }
    /* 0.2.4 (c)/(d): no loop cue on this deck: build from a firmware-constructed cue of this
     * deck (a point hot cue from the load, else the holder's own slot), with this deck's source
     * key written into BOTH Positions (in/out samples are set by the caller). */
    int have_key = 0; uint64_t k8 = 0; uint32_t k16 = 0;
    if (__atomic_load_n(&src_ok[d], __ATOMIC_ACQUIRE)) {
        k8 = __atomic_load_n(&src_k8[d], __ATOMIC_RELAXED); k16 = __atomic_load_n(&src_k16[d], __ATOMIC_RELAXED); have_key = 1;
    } else if (tk_kok[d]) { k8 = tk_k8[d]; k16 = tk_k16[d]; have_key = 2; }
    const uint8_t *base = NULL; const char *bh = "";
    if (base_ok[d]) { base = base_buf[d]; bh = "point cue"; }
    else if (__atomic_load_n(&snap_state[d], __ATOMIC_ACQUIRE) == 2) { base = snap_buf[d]; bh = "holder slot"; }
    else if (__atomic_load_n(&snap_state[d], __ATOMIC_ACQUIRE) == 0 || tick_no - snap_tick[d] > 120) {
        __atomic_store_n(&snap_state[d], 1u, __ATOMIC_RELEASE); snap_tick[d] = tick_no;
        __atomic_store_n(&hk_need[d], 1u, __ATOMIC_RELEASE);
    }
    if (base && have_key) {
        if (xtpl_ok) { fw_cue_dtor(xtpl); xtpl_ok = 0; }
        memset(xtpl, 0, CUE_SIZE);
        fw_cue_copy(xtpl, base);
        xtpl_ok = 1;
        for (int pp = 0; pp < 2; pp++) {
            uint8_t *P = xtpl + (pp ? 0x80 : 0x28);
            *(uint64_t *)(P + 8) = k8; *(uint32_t *)(P + 16) = k16;
            *(int64_t *)(P + 24) = 0;                        /* placeholder, caller sets in/out */
        }
        snprintf(how, hn, "deck %d %s, key from %s (no template needed)", d + 1, bh, have_key == 1 ? "playhead" : "load cue");
        return xtpl;
    }
    if (!__atomic_load_n(&src_ok[d], __ATOMIC_ACQUIRE)) {
        snprintf(how, hn, "none yet (deck %d: %s, %s)", d + 1, base ? "base ok" : "no base cue yet", have_key ? "key ok" : "no source key yet");
        return NULL;
    }
    for (int o = 0; o < NDECK; o++) {
        if (o == d || !(t = own_template(o))) continue;
        if (xtpl_ok) { fw_cue_dtor(xtpl); xtpl_ok = 0; }
        memset(xtpl, 0, CUE_SIZE);
        fw_cue_copy(xtpl, t);
        xtpl_ok = 1;
        uint64_t k8 = __atomic_load_n(&src_k8[d], __ATOMIC_RELAXED);
        uint32_t k16 = __atomic_load_n(&src_k16[d], __ATOMIC_RELAXED);
        for (int pp = 0; pp < 2; pp++) {               /* in Position at +0x28, out at +0x80 */
            uint8_t *P = xtpl + (pp ? 0x80 : 0x28);
            *(uint64_t *)(P + 8) = k8; *(uint32_t *)(P + 16) = k16;
        }
        snprintf(how, hn, "deck %d loop cue, deck %d source key", o + 1, d + 1);
        return xtpl;
    }
    snprintf(how, hn, "none (no loop cue on any deck yet)");
    return NULL;
}
static int anlz_merge(int d, const anlz_res_t *r, int quiet)
{
    char how[32];
    double u = units_per_ms(d, r, how, sizeof how);
    char tplhow[64];
    const uint8_t *tpl = loop_template_how(d, tplhow, sizeof tplhow);
    int nm = 0, nh = 0, built = 0, known = 0, nflag = 0, unbuilt = 0;
    int pflag = 0;
    for (int i = 0; i < r->n; i++) pflag += r->e[i].flag != 0;     /* 0.2.4: from the parse, no template needed */
    for (int i = 0; i < r->n; i++) {
        const al_t *e = &r->e[i];
        int64_t in = (int64_t)(e->in_ms * u + 0.5), out = (int64_t)(e->out_ms * u + 0.5);
        char ti[32], to[32]; fmt_time(ti, sizeof ti, in); fmt_time(to, sizeof to, out);
        if (e->hot) {
            nh++;
            int k = e->hot;
            if (!quiet) LOG("  anlz deck %d: hot cue %d loop %s-%s %s", d + 1, k, ti, to,
                (k >= 1 && k <= 8 && hot_ok[d][k] && fw_is_loop(hot_buf[d][k])) ? "(loaded)" : "(not loaded yet)");
            continue;
        }
        nm++;
        int j;
        for (j = 0; j < mem_n[d]; j++)
            if (mem_ok[d][j] && near(cue_in(mem_buf[d][j]), in) && near(cue_out(mem_buf[d][j]), out)) break;
        if (j < mem_n[d]) { nflag += e->flag != 0; mem_live[d][j] = 1; mem_flag[d][j] = (uint8_t)e->flag; known++; if (!quiet) LOG("  anlz deck %d: memory loop %s-%s (already known)%s", d + 1, ti, to, e->flag ? " [rekordbox Active Loop]" : ""); continue; }
        if (!tpl) { unbuilt++; if (!quiet) LOG("  anlz deck %d: memory loop %s-%s (no template yet: %s)", d + 1, ti, to, tplhow); continue; }
        memset(syn_tmp, 0, CUE_SIZE);
        fw_cue_copy(syn_tmp, tpl);
        *(int64_t *)(syn_tmp + CUE_IN_TIME) = in;
        *(int64_t *)(syn_tmp + CUE_OUT_TIME) = out;
        if (!fw_is_loop(syn_tmp)) {                       /* firmware's own predicate must accept it */
            fw_cue_dtor(syn_tmp); unbuilt++;
            if (!quiet) LOG("  anlz deck %d: memory loop %s-%s: built cue rejected by is_loop (template %s)", d + 1, ti, to, tplhow);
            continue;
        }
        int idx = mem_put(d, syn_tmp);
        fw_cue_dtor(syn_tmp);
        mem_live[d][idx] = 1; mem_syn[d][idx] = 1; mem_flag[d][idx] = (uint8_t)e->flag;
        built++; nflag += e->flag != 0;
        LOG("  anlz deck %d: memory loop %s-%s in=%lld out=%lld -> #%d (built)%s", d + 1, ti, to, (long long)in, (long long)out, idx,
            e->flag ? " [rekordbox Active Loop]" : "");
    }
    if (!quiet || built)
        LOG("deck %d: %d stored loops at load from ANLZ (%s): %d memory, %d hot cue; %d built, %d already known, %d waiting; "
            "%d flagged Active Loop in rekordbox; units %.3f/ms (%s); template: %s",
            d + 1, r->n, r->msg, nm, nh, built, known, unbuilt, pflag, u, how, tplhow);
    (void)nflag;
    return unbuilt;
}
/* 0.2.2: with Auto-advance on and nothing armed for this track, the first stored loop ahead
 * of the playhead becomes the Active Loop once the load's loops are known. */
static void auto_arm(int d, uint32_t cid)
{
    if (autoarm_done[d]) return;
    autoarm_done[d] = 1;
    uint32_t cfg = __atomic_load_n(&cfg_bits, __ATOMIC_RELAXED);
    if (!(cfg & CFG_ENABLE)) { LOG("auto-arm deck %d: skipped (Active Loop off)", d + 1); return; }
    if (restore_pending[d]) { LOG("auto-arm deck %d: skipped (same track, restoring the saved Active Loop)", d + 1); return; }
    if (act_ok[d] && !spent[d] && active_cid[d] == cid) { LOG("auto-arm deck %d: skipped (an Active Loop is already armed)", d + 1); return; }
    int64_t pos = playhead(d);
    if (pos < 0) pos = 0;
    char tp[32]; fmt_time(tp, sizeof tp, pos);
    int hot = 0, idx = -1;
    /* 1: the rekordbox Active Loop flag (earliest flagged loop ahead, else earliest flagged) */
    const uint8_t *c = next_flagged(d, pos - 1, &idx);
    if (!c) c = next_flagged(d, INT64_MIN, &idx);
    const char *src = "rekordbox flag";
    if (!c) {
        /* 2: fallback, only with Auto-advance on */
        if (!(cfg & CFG_AUTOADV)) { LOG("auto-arm deck %d: no rekordbox Active Loop flag; fallback skipped (Auto-advance off)", d + 1); return; }
        c = next_ahead(d, pos - 1, &hot, &idx);
        src = "fallback";
        if (!c) { LOG("auto-arm deck %d: no rekordbox Active Loop flag and no stored loop ahead of %s (%d known)", d + 1, tp, count_known(d, -1)); return; }
    }
    char t[32]; fmt_time(t, sizeof t, cue_in(c));
    LOG("auto-arm deck %d: source: %s -> %s %d at %s (playhead %s, %d loops ahead)", d + 1, src,
        hot ? "hot cue" : "memory", idx, t, tp, count_known(d, pos - 1));
    pick_and_set(d, c, hot, idx, src[0] == 'r' ? "auto-arm at load (rekordbox flag)" : "auto-arm at load (fallback)");
    toastf(src[0] == 'r' ? "Active Loop: %s (rekordbox)" : "Active Loop: first loop at %s (auto)", t);
}
static void anlz_service(void)
{
    if (tick_no % 15) return;
    for (int d = 0; d < NDECK; d++) {
        uint32_t cid = deck_cid(d);
        if (!cid || !holder_of(d)) continue;
        if (anlz_for[d] != cid) {                       /* new load (or first sight): ask the worker */
            anlz_for[d] = cid; anlz_merged[d] = 0; anlz_wait[d] = 0; anlz_parsed_logged[d] = 0;
            anlz_request(d, cid);
            LOG("deck %d: reading stored loops for content id %u from the stick (read-only)", d + 1, cid);
        }
        if (anlz_merged[d]) continue;
        uint32_t st = __atomic_load_n(&anlz_state[d], __ATOMIC_ACQUIRE);
        if (st == 3) { anlz_merged[d] = 1; LOG("deck %d: stored loops not read: %s (loops known once called/stored)", d + 1, anlz_res[d].msg); auto_arm(d, cid); continue; }
        if (st != 2 || anlz_res[d].cid != cid) continue;
        if (!anlz_parsed_logged[d]) {
            int f = 0, m = 0; for (int i = 0; i < anlz_res[d].n; i++) { f += anlz_res[d].e[i].flag != 0; m += !anlz_res[d].e[i].hot; }
            anlz_parsed_logged[d] = 1;
            LOG("deck %d: ANLZ parsed: %d loops (%d memory), %d flagged Active Loop in rekordbox (PCPT status 4)", d + 1, anlz_res[d].n, m, f);
        }
        if (tick_no - last_load_tick[d] < 60) continue;            /* let the load burst land first */
        /* 0.2.3: merge now; memory loops still waiting for a template are retried every
         * 0.25 s for up to 30 s (quiet), auto-arm runs once nothing waits or at the end */
        int left = anlz_merge(d, &anlz_res[d], anlz_wait[d] > 0);
        /* 0.2.4: no give-up while the track stays loaded: a base cue or the source key can
         * arrive only once the deck plays. Arms as soon as the loops are built. */
        if (left) {
            ++anlz_wait[d];
            if (anlz_wait[d] >= 240) {                         /* 0.2.5: 60 s, then stop quietly */
                LOG("deck %d: stopped building %d memory loops after 60 s (no base cue or source key)", d + 1, left);
                anlz_merged[d] = 1;
                auto_arm(d, cid);
                continue;
            }
            if (anlz_wait[d] == 1) {
                char h[96]; (void)loop_template_how(d, h, sizeof h);
                LOG("deck %d: %d memory loops waiting to be built (%s); retrying every 0.25 s", d + 1, left, h);
            }
            int pf = 0, fi = -1;
            for (int i = 0; i < anlz_res[d].n; i++) pf += anlz_res[d].e[i].flag != 0;
            if (count_known(d, -1) == 0 || (pf && !next_flagged(d, INT64_MIN, &fi))) continue;   /* wait for the flagged one */
        }
        anlz_merged[d] = 1;
        auto_arm(d, cid);
    }
    anlz_kick();
}

/* 0.2.1: own timing every 10 s (filter, hooks), in us */
static void timing_log(void)
{
    static uint64_t last; static uint32_t fq; static uint32_t smax_f, smax_e, smax_ea, smax_c;
    uint64_t now = now_ns();
    if (!fq) {
        uint64_t f = cnt_frq(); fq = f ? (uint32_t)f : 1000000000u;
        __atomic_store_n(&FS.thr, (uint32_t)(fq / 10000u), __ATOMIC_RELAXED);   /* 100 us */
    }
    if (!last) { last = now; return; }
    if (now - last < 10000000000ull) return;
    last = now;
    uint32_t f = take0(&FS.fmax);
    uint32_t fo = take0(&FS.fover);
    uint32_t e = take0(&hmax_ec_own);
    uint32_t ea = take0(&hmax_ec_all);
    uint32_t c = take0(&hmax_cue);
    uint32_t er = take0(&hmax_ec_rare);
    if (f > smax_f) smax_f = f;
    if (e > smax_e) smax_e = e;
    if (ea > smax_ea) smax_ea = ea;
    if (c > smax_c) smax_c = c;
#define US(x) ((double)(x) * 1e6 / (double)fq)
    LOG("timing 10s (us, own code, cntvct): filter max %.1f (session %.1f), %u calls >100; "
        "engage-check hot path max %.1f (session %.1f), with firmware %.1f (session %.1f), rare path max %.1f; "
        "cue hooks max %.1f (session %.1f) [filter and hot path sampled 1/64]",
        US(f), US(smax_f), fo, US(e), US(smax_e), US(ea), US(smax_ea), US(er), US(c), US(smax_c));
#undef US
}

static void on_deck(int deck, int what, void *user)
{
    (void)user;
    /* may run off the UI thread: only flag it, the tick does the work */
    if (deck >= 0 && deck < NDECK && (what == AZ_DECK_LOADED || what == AZ_DECK_EJECTED))
        __atomic_or_fetch(&deck_evt, (what == AZ_DECK_LOADED ? 1u : 16u) << deck, __ATOMIC_RELAXED);
}
static void sync_cfg(void);
/* 0.2.2: the core saves the value, then calls this on the UI thread; apply it at once and log
 * it, so a press that the core's menu did not deliver is visible as a missing line. */
static void row_changed(int value, void *user)
{
    sync_cfg();
    __atomic_store_n(&FS.cfg, __atomic_load_n(&cfg_bits, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
    LOG("row %s -> %s (applied now)", (const char *)user, value == 1 ? "ON" : "OFF");
}
static unsigned long n_reload, n_eject;   /* 0.2.6: load/eject counts, no per-event log */
static int restore_wait[NDECK];
static uint32_t seen_cid[NDECK];
static long     cid_poll;
static void track_changed(int d, const char *why)
{
    mem_reset(d);
    (void)why;   /* 0.2.6: no per-load log line (log-quiet gate); counted instead */
    __atomic_add_fetch(&n_reload, 1, __ATOMIC_RELAXED);
    ready_drop_others(d);
    autoarm_done[d] = 0;
    __atomic_store_n(&src_ok[d], 0u, __ATOMIC_RELEASE);
    if (base_ok[d]) { fw_cue_dtor(base_buf[d]); base_ok[d] = 0; }
    tk_kok[d] = 0;
    if (__atomic_load_n(&snap_state[d], __ATOMIC_ACQUIRE) == 2) { fw_cue_dtor(snap_buf[d]); }
    __atomic_store_n(&snap_state[d], 0u, __ATOMIC_RELEASE);
    anlz_for[d] = 0;                       /* re-read the stored loops for this load */
    spent[d] = 0; ex_inside[d] = 0; ex_past[d] = 0;
    restore_pending[d] = act_ok[d] && active_cid[d] != 0;
    restore_wait[d] = 0;
}
static void poll_cids(void)
{
    if (++cid_poll % 30) return;
    for (int d = 0; d < NDECK; d++) {
        uint32_t c = deck_cid(d);
        if (c && seen_cid[d] && c != seen_cid[d]) track_changed(d, "content id changed");
        if (c) seen_cid[d] = c;
    }
}
static void deck_events(void)
{
    uint32_t ev = __atomic_exchange_n(&deck_evt, 0, __ATOMIC_RELAXED);
    for (int d = 0; d < NDECK; d++) {
        int loaded = (ev >> d) & 1, ejected = (ev >> (d + 4)) & 1;
        if (!loaded && !ejected) continue;
        if (loaded) track_changed(d, "deck event");
        else { mem_reset(d); restore_pending[d] = 0; __atomic_add_fetch(&n_eject, 1, __ATOMIC_RELAXED); }
    }
}
/* (5) same track reloaded this session: re-apply the remembered active loop as soon as
 * its memory cue reaches the cache (load/call/store). */
/* Restore only once the remembered memory loop is seen live again after the reload (a call
 * or store event with the same in/out); a stock DELETE of it drops the memory. */
static void try_restore(void)
{
    for (int d = 0; d < NDECK; d++) {
        if (!restore_pending[d] || !holder_of(d)) continue;
        if (tick_no - last_load_tick[d] < 60) continue;
        if (!act_ok[d]) { restore_pending[d] = 0; continue; }
        uint32_t cid = deck_cid(d);
        if (!cid) { if (++restore_wait[d] > 60 * 30) { restore_pending[d] = 0; LOG("restore deck %d: gave up (no content id)", d + 1); } continue; }
        if (cid != active_cid[d]) { restore_pending[d] = 0; LOG("restore deck %d: other track (%u, was %u), nothing to restore", d + 1, cid, active_cid[d]); continue; }
        int i = mem_find_live(d, act_buf[d]);
        if (i < 0) {
            if (++restore_wait[d] == 1)
                LOG("restore deck %d: waiting until memory loop in=%lld out=%lld is seen live (CALL it)", d + 1,
                    (long long)active_in[d], (long long)active_out[d]);
            continue;
        }
        restore_pending[d] = 0;
        set_active_mem(d, i, "restore");
        LOG("restore deck %d: same track %u, live memory loop in=%lld out=%lld re-applied", d + 1, cid,
            (long long)active_in[d], (long long)active_out[d]);
    }
}
/* Stock DELETE (no SHIFT) removes the memory cue at the playhead = the last called/stored one. */
/* Stock DELETE: EP147 gives us no delete event, and the deleted cue is not always the
 * last called one. So: the suspect cue stops counting as live (no dup match, no restore)
 * but nothing is cleared or forgotten; seeing it again in a call/store cancels it. */
static void on_stock_delete(int d)
{
    if (!last_call_ok[d]) {
        for (int i = 0; i < mem_n[d]; i++) mem_live[d][i] = 0;
        LOG("stock DELETE deck %d: target unknown, memory cache unverified until seen again", d + 1);
        return;
    }
    int64_t in = last_call_in[d], out = last_call_out[d];
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_ok[d][i] && near(cue_in(mem_buf[d][i]), in) && near(cue_out(mem_buf[d][i]), out)) mem_live[d][i] = 0;
    del_pend_in[d] = in; del_pend_out[d] = out; del_pend_ok[d] = 1;
    LOG("stock DELETE deck %d: pending (unconfirmed) for memory cue in=%lld out=%lld; not used for "
        "dup/restore until seen again; Active Loop left as is", d + 1, (long long)in, (long long)out);
}

static unsigned nib(uint64_t v, unsigned pos) { return (unsigned)((v >> pos) & 0xfu); }

static void poll_filter(void)
{
    uint64_t sn = __atomic_load_n(&FS.snap, __ATOMIC_RELAXED);
    uint32_t cfg = __atomic_load_n(&cfg_bits, __ATOMIC_RELAXED);
    /* heartbeat: frames stop reaching us -> the core switched the filter off */
    if ((sn >> 56) == (tk_snap >> 56)) {
        if (tk_hb_seen && ++tk_hb_same == 180 && !filter_dead && (cfg & CFG_ENABLE)) {
            filter_dead = 1;
            /* 0.2.7: the core keeps a switched-off filter off for the session and a filter can
             * only be registered from init(), so it cannot be re-armed. Only the SHIFT combos
             * depend on it: auto-arm, engage, re-arm and Auto-advance run on hooks. */
            LOG("frame filter is not running (no ERP frames for ~3 s): SHIFT+MEMORY / SHIFT+pad / "
                "SHIFT+DELETE off until EP147 restarts; auto-arm, engage and Auto-advance still on");
            do_toast("Active Loop on; SHIFT combos off until restart");
        }
    } else {
        if (filter_dead) { filter_dead = 0; LOG("frame filter running again"); }
        tk_hb_same = 0; tk_hb_seen = 1;
    }
    uint64_t now = now_ns();
    for (int s = 0; s < 2; s++) {
        unsigned base = 28u * (unsigned)s;
        unsigned dn[NSIG];
        for (int k = 0; k < NSIG; k++) dn[k] = (nib(sn, base + 4u * k) - nib(tk_snap, base + 4u * k)) & 0xfu;
        if (dn[SG_SEC]) side_deck[s] = s ? 3 : 2;
        if (dn[SG_PRI]) side_deck[s] = s ? 1 : 0;
        if (dn[SG_PRESS]) { press_obs_ns[s] = now; press_obs_deck[s] = side_to_deck(s); }
        if (!(cfg & CFG_ENABLE)) continue;
        if (dn[SG_SET]) apply_set(side_to_deck(s));
        if (dn[SG_CLR]) apply_clear(side_to_deck(s));
        if (dn[SG_PAD]) apply_copy_pad(side_to_deck(s), (int)nib(sn, base + 24));
    }
    tk_snap = sn;
    static uint32_t tk_del;
    static int tk_del_seeded;
    uint32_t dl = __atomic_load_n(&FS.del, __ATOMIC_RELAXED);
    if (!tk_del_seeded) { tk_del_seeded = 1; tk_del = dl; }   /* never count a baseline as a press */
    for (int s2 = 0; s2 < 2; s2++)
        if (((dl >> (8 * s2)) & 0xffu) != ((tk_del >> (8 * s2)) & 0xffu)) on_stock_delete(side_to_deck(s2));
    tk_del = dl;
}

static void my_tick(void)
{
    tick_no++;
    sync_cfg();
    __atomic_store_n(&FS.cfg, __atomic_load_n(&cfg_bits, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
    deck_events();
    poll_filter();          /* presses first, so events in this tick can correlate */
    drain_ring();
    try_restore();
    pend_service();
    watch_loops();
    anlz_service();
    timing_log();
    for (int d = 0; d < NDECK; d++) {
        __atomic_store_n(&hold_pub[d], holder_of(d), __ATOMIC_RELAXED);
        uint32_t cf = __atomic_load_n(&cfg_bits, __ATOMIC_RELAXED);
        int64_t a = (act_ok[d] && !spent[d] && (cf & CFG_ENABLE) && (cf & CFG_REARM)) ? active_in[d] : INT64_MAX;
        __atomic_store_n(&arm_in[d], a, __ATOMIC_RELAXED);
    }
    poll_cids();
    ready_flush();
    log_binds();
    flush_toast();
}

static void on_draw(az_draw *d, az_rect a, void *user)
{
    (void)user;
    C->draw_header(d, "Active Loop Picker");
    int row = C->row_height(d), px = row / 2;
    if (px < 8) px = 8;
    if (px > 96) px = 96;
    int y = a.y + row;
    const char *lines[] = {
        "SHIFT+MEMORY: next stored loop ahead.",
        "SHIFT+pad: that hot cue loop.",
        "SHIFT+DELETE: clear.",
        "No playhead jump.",
        "SHIFT combos off until restart (loops still arm).",
    };
    int nl = filter_dead ? 5 : 4;   /* 0.2.7: the core switched the frame filter off */
    for (int i = 0; i < nl; i++) {
        if (y + row > a.y + a.h) return;
        /* 0.2.6: fit each line to the page width (narrow pages, harness draw-area gate) */
        int lp = px, avail = a.w - row / 2;
        if (AZ_HAS(C, text_width)) {
            while (lp > 8 && C->text_width(lines[i], lp) > avail) lp--;
            if (C->text_width(lines[i], lp) > avail) { y += row; continue; }
        }
        if (y + row / 4 + lp * 3 / 2 > a.y + a.h) return;
        C->draw_text(d, a.x + row / 4, y + row / 4, lines[i], lp, C->color(AZ_COL_TEXT));
        y += row;
    }
}

/* 0.2.6: check every target before claiming any. A target must be readable and must not
 * already start with a hook trampoline (B imm26, or LDR x16/x17 + BR). The
 * core can still refuse a claim for reasons only it knows (jump islands); then al_live stays 0
 * and the hooks already installed only call through to the firmware. */
static int target_ok(uintptr_t va, const char *what)
{
    uint32_t ins[4];
    if (safe_read(va, ins, sizeof ins) != 0) { LOG("precheck %s %#lx: not readable", what, (unsigned long)va); return 0; }
    /* a hook rewrites the entry: B imm26, or LDR x16/x17,=lit then BR x16/x17 */
    int patched = (ins[0] & 0xfc000000u) == 0x14000000u ||
                  ((ins[0] & 0xff000000u) == 0x58000000u && (ins[0] & 0x1eu) == 0x10u &&
                   (ins[1] & 0xfffffc1fu) == 0xd61f0000u);
    if (patched) {
        LOG("precheck %s %#lx: already patched (%08x %08x); claiming nothing", what, (unsigned long)va, ins[0], ins[1]);
        return 0;
    }
    return 1;
}

static int claim_all(void)
{
    if (!target_ok(VA_STORE, "store") || !target_ok(VA_CALL, "call") || !target_ok(VA_LOAD, "load"))
        return 8;
    int setout_ok = target_ok(VA_SETOUT, "setLoopOutPoint"), echeck_ok = target_ok(VA_ECHECK, "engage-check");
    if (C->claim_hook(MOD_ID, VA_STORE, (void *)my_store, (void **)&orig_store) != 0) {
        LOG("claim_hook store refused"); return 5;
    }
    if (C->claim_hook(MOD_ID, VA_CALL, (void *)my_call, (void **)&orig_call) != 0) {
        LOG("claim_hook call refused; Reload + rm .starting if suspended"); return 6;
    }
    if (C->claim_hook(MOD_ID, VA_LOAD, (void *)my_load, (void **)&orig_load) != 0) {
        LOG("claim_hook load refused; Reload + rm .starting if suspended"); return 7;
    }
    if (!setout_ok) orig_setout = NULL;
    else if (C->claim_hook(MOD_ID, VA_SETOUT, (void *)my_setout, (void **)&orig_setout) != 0) {
        LOG("claim_hook setLoopOutPoint refused (pad uses LoopState fallback)");
        orig_setout = NULL;
    }
    if (!echeck_ok) orig_echeck = NULL;
    else if (C->claim_hook(MOD_ID, VA_ECHECK, (void *)my_echeck, (void **)&orig_echeck) != 0) {
        LOG("claim_hook engage-check 0x14b37a0 refused: no re-arm, set/clear from tick");
        orig_echeck = NULL;
    }
    return 0;
}

static int init(const az_core_v1 *core)
{
    if (!core || core->abi != AZ_MOD_ABI) return 1;
    C = core;
    if (!AZ_HAS(C, claim_hook) || !AZ_HAS(C, fw_tag)) { LOG("needs claim_hook"); return 2; }
    const char *tag = C->fw_tag();
#ifdef AZ_HARNESS   /* TEST-ONLY (make test-harness): the stock harness reports a fake tag */
    tag = FW_TAG;
#endif
    if (!tag || strcmp(tag, FW_TAG) != 0) {
        LOG("wrong firmware tag %s want %s", tag ? tag : "?", FW_TAG);
        return 3;
    }
    probe_safe_read();
    LOG("safe_read: %s", vm_mode == 1 ? "process_vm_readv" : "init-time /proc/self/maps snapshot");
    fw_set = (set_fn)VA_SET;
    fw_clear = (clear_fn)VA_CLEAR;
    fw_is_loop = (pred_fn)VA_IS_LOOP;
    fw_cue_copy = (cue_copy_fn)VA_CUE_COPY;
    fw_cue_dtor = (cue_dtor_fn)VA_CUE_DTOR;
    fw_cue_mark = (cue_mark_fn)VA_CUE_MARK;
    fw_loop_slot = (loop_slot_fn)VA_LOOP_SLOT;
    fw_ls_has_loop = (ls_pred_fn)VA_LS_HAS_LOOP;
    fw_pos_in = (pos_in_fn)VA_POS_IN;
    fw_valid_loop = (int_fn)VA_VALID_LOOP;
    fw_1508 = (int_fn)VA_1508;
    fw_1525 = (int_fn)VA_1525;
    fw_engage = (engage_fn)VA_ENGAGE;
    fw_ls_looping = (ls_pred_fn)VA_LS_LOOPING;

    if (AZ_HAS(C, register_row)) {
        const az_row_v1 en = {
            .size = sizeof en, .key = "enable", .label = "Active Loop",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
            .help = "SHIFT+MEMORY next loop, SHIFT+pad hot loop, SHIFT+DELETE clear.",
            .on_change = row_changed, .user = (void *)"Active Loop",
        };
        h_enable = C->register_row(MOD_ID, &en);
        const az_row_v1 aa = {
            .size = sizeof aa, .key = "auto-advance", .label = "Auto-advance",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON, .parent = "enable",
            .help = "First loop ahead at load; after an exit, the next one ahead.",
            .on_change = row_changed, .user = (void *)"Auto-advance",
        };
        h_autoadv = C->register_row(MOD_ID, &aa);
        const az_row_v1 ra = {
            .size = sizeof ra, .key = "rearm", .label = "Re-arm",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON, .parent = "enable",
            .help = "Engage even after an earlier loop.",
            .on_change = row_changed, .user = (void *)"Re-arm",
        };
        h_rearm = C->register_row(MOD_ID, &ra);
    }

    {
        static az_screen_v1 scr;
        scr = (az_screen_v1){ .size = sizeof scr, .on_draw = on_draw };
        if (C->register_screen(MOD_ID, &scr) != 0)
            C->log(MOD_ID, "register_screen refused");
    }

    for (int d = 0; d < NDECK; d++) { pos_now[d] = POS_UNSET; arm_in[d] = INT64_MAX; }
    ts_init();
    prewarm_filter();
    __atomic_store_n(&FS.cfg, __atomic_load_n(&cfg_bits, __ATOMIC_RELAXED), __ATOMIC_RELAXED);
#ifdef AZ_HARNESS   /* TEST-ONLY: the stock harness core has no frame_filter; on_erp is unit-tested */
    if (!AZ_HAS(C, frame_filter) || !C->frame_filter) { (void)on_erp; LOG("harness: no frame_filter"); } else
#endif
    if (!AZ_HAS(C, frame_filter) ||
        C->frame_filter(MOD_ID, 1u << AZ_BUS_ERP_IN, on_erp, NULL) != 0) {
        C->log(MOD_ID, "frame_filter refused");
        return 4;
    }

    if (AZ_HAS(C, on_deck_event) && C->on_deck_event(MOD_ID, on_deck, NULL) != 0)
        LOG("on_deck_event refused (cue cache not reset on load)");

#ifdef AZ_HARNESS   /* TEST-ONLY: no hooks under the harness (claim_hook fails the api gate) */
    int cr = 0;
    (void)claim_all;
#else
    int cr = claim_all();
#endif
    if (cr != 0) return cr;

    if (!AZ_HAS(C, banner)) LOG("warning: core has no banner");
    sync_cfg();
    __atomic_store_n(&al_live, 1, __ATOMIC_RELEASE);
    LOG("loaded 0.2.7; template-free ANLZ build, flags at parse, wait-free filter/hot path, updatable toast (%s); setout=%d",
        tsurf ? "surface" : "banner queue", orig_setout != NULL);
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
