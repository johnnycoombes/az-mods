/* SPDX-License-Identifier: GPL-3.0-only
 *
 * active-loop 0.2.12 — Active Loop Picker for EP147 (XDJ-AZ 2.00/2.01).
 *
 * 0.2.12: the breath is slower and goes all the way out. One cycle is 4 s (was 2 s) and the armed pad fades from full red (FF0000)
 *        down to OFF (red byte 0, the whole cell 00 00 00) and back, instead of stopping at a dim red. The curve is shaped so the pad
 *        spends only a brief moment dark (red byte 0 about 6% of a cycle, 0.24 s; 2 or less about 11%, 0.44 s) and reads as lit the
 *        rest of the time. Only led_level_update() changed (period, shape, floor 0); on_led and everything else are as 0.2.11.
 *
 * 0.2.11: the ARMED Active Loop pad BREATHES. Its red fades smoothly between full red (FF0000) and a dim red that stays clearly lit,
 *        one cycle every 2 s. Pads that are only saved (flagged, not armed) stay solid red. The breath is the LED frame's colour
 *        only: the tick reads CLOCK_MONOTONIC, shapes a raised cosine and a gamma curve (LED bytes are linear light), and publishes
 *        ONE byte (led_lvl, atomic); on_led only reads that byte and writes the armed pad's red channel. No clock, call, lock or
 *        log is added to the LED path, and the input filter is untouched. New Mod Menu row "Pulse armed pad" (default ON; off =
 *        solid red as 0.2.10). Nothing else changed: every 0.2.10 rule (never delete a loop we did not store and confirm,
 *        no writes to the deck/stick, ownership read-back) is as it was. HOT CUE gating and the stock dim-cell rule are as before.
 *
 * 0.2.10 (AZ-EMU al029a round: the firmware ignored a same-extent store and DELETES the old flagged loop on a new flag):
 *        The mod now NEVER deletes a memory loop it did not store itself in this session and confirm in the ANLZ read-back.
 *        There is no set-flag-in-place path in the firmware (CacheableCueInfo::doRegisterCue 0x1890808 ignores a store whose
 *        kind and in/out, in whole ms, equal an existing memory cue; storing a FLAGGED memory loop deregisters every other
 *        flagged one). So SHIFT+pad saves only when it can do so without losing anything:
 *          - an unflagged memory loop with the same in/out exists: armed only, "NOT saved: same loop already in memory";
 *          - one is already flagged (rekordbox's): armed, "already saved", nothing stored;
 *          - another flagged loop we do not own exists: armed only, "NOT saved: another loop is saved as Active" (the
 *            firmware would delete it);
 *          - otherwise a flagged memory copy is stored; ~5 s later the ANLZ read-back confirms it (one flagged loop there,
 *            the loop count we expected) and only then it may be deleted again by this mod (second press, DELETE+pad).
 *        Second press on a loop that is not ours-and-confirmed: Active Loop off, the saved flag STAYS and the toast says so.
 *        If our copy cannot be removed now (no store pointer / queue busy) the loop stays armed (no flag left behind silently).
 *        The memory limit counts memory points as well as loops (from the ANLZ). The store pointer is recognised by the real
 *        CueStoreMaintainer vtable (0x2951330) / task queue (the 0.2.9 check looked for 0x86b410 in the vtable: wrong), and
 *        also searched in the CueCallManager after any cue delete.
 *        Everything below is 0.2.9 (red pad, persist jobs, read-back) except where it says "ours".
 * 0.2.9: the Active Loop is KEPT, and its pad is RED.
 *        SHIFT+loop pad, first press: arms the loop (as 0.2.8) and saves it: a memory-kind copy of the loop with the rekordbox
 *          Active Loop flag (CueProperty+0xe1 = 1) is stored through the firmware's own store (0x86b410), so the firmware's cue
 *          write-back (database / ANLZ) persists it. Nothing is written by the mod itself.
 *        SHIFT+the red pad, second press: Active Loop off (cleared); the normal hot cue loop stays.
 *        DELETE+pad: as before it deletes the hot cue; OUR flagged memory copy is deleted with it.
 *        Red pad: on_frame edits the pad's LED cell (the pad of the armed loop, else of the flagged loop of the track) to FF0000
 *          at the cell's own brightness, only while HOT CUE is the lit pad mode. Stock rewrites the frame every cycle, so
 *          stopping the edit restores the pad's own colour. A track loaded with a flagged loop that equals a hot cue loop shows
 *          that pad red at once. The jog display tile is not recoloured.
 *        Persist jobs (store / delete) run in the tick, one per ~130 ms, in order.
 *        ~5 s after the last job the track's ANLZ is re-read (READ-ONLY) and logged as "persist check ... OK / MISMATCH".
 *        SHIFT+MEMORY, SHIFT+DELETE, SHIFT+point cue, DELETE+MEMORY and the rest of 0.2.8 are unchanged.
 *
 * 0.2.8 — SHIFT+pad / DELETE+pad (below).
 *
 * 0.2.8: SHIFT+pad arms a loop pad (as 0.2.7), DELETE+pad deletes a hot cue. Stock's SHIFT+pad
 *        delete cannot be swallowed: CueCallManager::deleteCue (0x861218) only queues the
 *        database/ANLZ write; the live hot cue is cleared earlier by CueLoopSetFacade::clearCue
 *        (0x1431720), so a deleteCue swallow armed the loop AND still deleted the cue (AZ-EMU).
 *          - SHIFT+pad on a hot cue loop: the filter eats the pad, the loop becomes the Active
 *            Loop. SHIFT+pad on a point cue: replayed to stock (40 ms), so it is deleted as
 *            before. SHIFT+pad on an empty pad: swallowed (stock would store a cue).
 *          - DELETE held (no SHIFT): the DELETE bit is eaten; a pad pressed meanwhile is sent to
 *            stock as SHIFT+pad, which deletes that hot cue (a loop pad too). DELETE released with
 *            no pad: replayed as a 40 ms tap, so plain DELETE (memory cue) still works.
 *        deleteCue (0x861218) is now an observer only (never changes behaviour; row off = pure
 *        pass-through, no event): it drops stale loops from the list and clears the Active Loop
 *        when its loop (hot or memory) is deleted. Memory-loop deletes match the nearest in/out
 *        within 0.5 s (the ANLZ-built loop can be ~0.2 s off). The loop list is also checked
 *        against the live hot-cue slots before it is used. The observer copies the cue and
 *        nothing else; the ring is pre-touched at init (AZ-EMU saw a 2.8 ms first call).
 *        0.2.8 (al028b round, AZ-EMU: the core switched the frame filter off in 3 of 3 sessions, own peak
 *        1801.8 us after bursts of cue stores/deletes):
 *          - The filter is O(1): bit ops and its own 64-byte state, no clock (one sample per 256 frames,
 *            2 reads), no log, no call, no read of live cue memory (every slot read moved to the tick),
 *            nothing allocated. Replay lengths are counted in FRAMES (8-bit frame counter), the tick
 *            calibrates the frame rate. The own-time sample is 1/256 (it was 1/64).
 *          - The strikes came from cold or evicted code (page faults / emulator translation) after the
 *            DB/ANLZ write bursts, not from the work in the filter. The mod's own read-only segments are
 *            mlock()ed at init (fallback: the tick touches them); every filter path is pre-run.
 *          - The deleteCue observer reads two words of the cue and pushes to the ring: no firmware call,
 *            no clock_gettime. Everything else (slot reads, matching, clearing) runs in the tick.
 *          - Row off: hooks pass straight through, the tick returns early. No log, no toast, no
 *            exit/auto-advance handling, no queue. One quiet clear when the row goes off.
 *          - Deck: hot-cue deletes are attributed from the live LoopState slot of each deck; memory
 *            deletes by the press / the one deck holding it; ambiguous = nothing changes.
 *          - Active Loop is cleared only for the same kind/slot it was armed from (never by extent alone).
 *          - A loop stored both as memory and as hot cue (same extent) counts once.
 *        0.2.8 (al028c round, AZ-EMU on the emulator; 0.2.8 was rebuilt, the version stays):
 *          - Filter: still no lock / log / allocation / syscall / call; its three 64-byte lines (state, diagnostics,
 *            tick-owned) are written once at init and mlock()ed with the code. EVERY call is timed (two cntvct
 *            reads): calls over 100 / 200 us and the gap between calls are reported in the 30 s timing line, so a
 *            strike can be told from a stall of the bus thread (the 1/256 sample could not see most strikes).
 *          - DELETE tap: a hold is "not a tap" once a pad or SHIFT has joined it (SHIFT leaving first used to
 *            make a tap = a stray memory-cue delete); a new DELETE press ends an older tap replay (its DELETE bit
 *            used to be added back onto the new DELETE+pad frames = stock deleted the load/called memory cue as
 *            well); a hold with a gap in the frames (the thread was stalled, a pad may have come and gone unseen)
 *            is NOT replayed. SHIFT leads a pad the filter itself raises (DELETE+pad, replay) by one frame.
 *          - Row: one line per switch (ON / OFF); hooks note the facades while off, so the decks bind the moment the
 *            row goes on; a deck load is logged again; the ERP frame rate comes from the filter's own stamps;
 *            routine chatter (toasts, store/delete observer lines) is capped at 40 lines per 10 s, so a stress run
 *            stays under the core's 128 KB rotation.
 *        0.2.8 (al028d round, AZ-EMU emulator, rebuilt again; the version stays):
 *          - SHIFT+pad on an EMPTY pad stores nothing (it used to be replayed to stock, which STORES a cue there); DELETE+pad
 *            on an empty pad likewise; a pad that went down under SHIFT stays eaten until it is released.
 *          - The stall test is relative to the measured frame interval (3x the typical interval, min 5 ms), so a plain
 *            DELETE tap is replayed at the emulator's 17 frames/s too; the replay length is known after ~1 s.
 *          - "track loaded" is logged by every path that sees a load; the core's deck event does not fire for a deck's first
 *            load or for the same track again.
 *        0.2.8 (al028g round, log-only fixes; the filter is untouched): the load cues run before the deck reports the new
 *          track, so "track loaded" printed the PREVIOUS track's content id and the stale id could start a second load
 *          (a second "reading stored loops"). The id of a load now comes from load_id(): 0 (not known yet) for the first
 *          second after the last load cue, then the deck's id, as try_restore and anlz_service use it.
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
 *    SHIFT+pad = that hot cue loop (0.2.8: DELETE+pad deletes a hot cue); SHIFT+MEMORY = next stored loop ahead (repeat cycles).
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
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE
#endif
#ifndef _GNU_SOURCE
#define _GNU_SOURCE   /* dl_iterate_phdr */
#endif
#include <stdint.h>
#include <link.h>
#include <sys/mman.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>
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
#define VA_DELETE    ((uintptr_t)0x861218u)    /* CueCallManager::deleteCue(this, CueKind, const CueProperty&) */
/* first two instructions of 0x861218 on 2.00/2.01: sub sp,sp,#0x310 ; mov w3,w1 */
#define DELETE_INS0  0xd10c43ffu
#define DELETE_INS1  0x2a0103e3u

#define CUE_SIZE     256
#define NDECK        4
#define NHOT         9      /* kinds 1..8 */
#define NMEM         32
#define MEM_SOFT_CAP 10
#define KIND_MEM     0
#define CP_FLAG      0xe1   /* CueProperty: the rekordbox Active Loop flag byte (memory cues only) */

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
static void *mgr_any;                   /* atomic: the CueCallManager deleteCue ran on (0.2.9: its +0x140 is the store) */

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
static void set_active_mem(int d, int i, const char *why);
static int     last_call_ok[NDECK];
static long     last_load_tick[NDECK];
static uint8_t  act_buf[NDECK][CUE_SIZE] __attribute__((aligned(64)));
static uint8_t  act_ok[NDECK];
static int autoarm_done[NDECK];
static void log_load(int d, const char *via);
static void load_cue_seen(int d);        /* 0.2.8 al028g: a load cue of deck d (handle_ev) */
static void track_changed(int d, const char *why);   /* 1 = mem_buf slot holds a constructed CueProperty */
static int  load_changed(int d, const char *why);    /* 0.2.8 al028f: track_changed once per load, whichever path saw it first */
static int     last_mem[NDECK] = { -1, -1, -1, -1 };
static uint8_t hot_buf[NDECK][NHOT][CUE_SIZE] __attribute__((aligned(64)));
static int     hot_ok[NDECK][NHOT];

static int side_deck[2] = { 0, 1 };

/* ---- hook -> tick ring ----
 * 0.1.4 SIGBUS: ev_t was {u32,4*i8,u32,cue[256]} so cue sat at +12; the firmware
 * cue copy (0x8504a8) does 8/16-byte accesses -> alignment fault in our .bss.
 * Now: slot 64-aligned, cue first at +0 (16-aligned), metadata naturally aligned,
 * atomics only on aligned u32. */
enum { SRC_CALL = 1, SRC_LOAD, SRC_STORE, SRC_SETOUT, SRC_DELETE };
#define RING 64
typedef struct __attribute__((aligned(64))) {
    uint8_t   cue[CUE_SIZE] __attribute__((aligned(16)));
    void     *facade;       /* raw arg, decoded in tick */
    uint64_t  t_ns;         /* the hook's cntvct read (counter ticks); the tick converts it to ns */
    int64_t   din, dout;    /* SRC_DELETE: the cue's in/out as read in the hook (no firmware call) */
    int32_t   draw;         /* SRC_DELETE: din/dout are valid */
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
 *   nibble counters (wrap at 16) at base+0 SET, +4 CLR, +8 PAD (SHIFT+pad), +12 PRESS,
 *   +16 SEC, +20 PRI; pad kind (1..8) at base+24; heartbeat frame counter at bits 56..63.
 * The tick diffs counters against its last copy, so no press is lost between ticks. */
enum { SG_SET = 0, SG_CLR, SG_PAD, SG_PRESS, SG_SEC, SG_PRI, NSIG };
/* 0.2.8 (al028c): three cache lines, each with ONE writer. Lines 0 and 1 are written only by the filter
 * (the tick reads them, and clears the diagnostic maxima with a plain load + store); line 2 is written
 * only by the tick / UI thread (the filter reads it; it clears `req` once). The old single line was
 * ping-ponged between cores by every tick write. */
#define STAMP_MASK 0xffffffffffull          /* stamp: wraps << 40 | counter & STAMP_MASK */
static struct __attribute__((aligned(64))) {
    struct __attribute__((aligned(64))) {       /* line 0: filter state, filter-owned */
        uint64_t snap;      /* filter writes (relaxed store), tick reads (relaxed load) */
        uint64_t stamp;     /* filter, once per 256 frames: (256-frame wraps << 40) | counter, for the frame rate */
        uint64_t last_t;    /* filter-only: counter at the previous call's entry */
        uint32_t prev;      /* filter-only: last level bits, 14 per side, bit 31 = seen */
        uint8_t  rp_t0[2];  /* filter-only: SHIFT+pad replay start per side (heartbeat count, 8 bits) */
        uint8_t  rp[4];     /* filter-only: [0..1] pad bits being replayed with SHIFT, [2..3] DELETE tap */
        uint8_t  dh[2];     /* filter-only: DELETE held (not with SHIFT first) since this frame count started */
        uint8_t  du[2];     /* filter-only: a pad or SHIFT joined that DELETE hold: it is not a tap */
        uint8_t  dh_t0[2];  /* filter-only: heartbeat count at DELETE down; after the release: start of the tap replay */
        uint8_t  sl[2];     /* filter-only: SHIFT was asserted by the filter on the previous frame (it leads the pad by 1 frame) */
        uint32_t fi;        /* filter-only (tick reads): running estimate of the TYPICAL frame interval, cntvct ticks (0.2.8 al028d) */
        uint8_t  fn;        /* filter-only (tick reads): intervals measured so far, saturates at 255 */
        uint8_t  pe[2];     /* filter-only: pad bits that went down under SHIFT (or on an empty pad under DELETE): kept eaten until the pad is up */
    };
    struct __attribute__((aligned(64))) {       /* line 1: filter diagnostics, filter-owned; the tick reads them and clears the maxima */
        uint32_t fmax;      /* longest call (entry to exit, wall clock), cntvct ticks (tick swaps to 0) */
        uint32_t fover;     /* calls over thr (100 us) */
        uint32_t fover2;    /* calls over thr2 (200 us = the core's budget) */
        uint32_t gmax;      /* longest gap between two calls (frame period incl. any stall), ticks */
        uint32_t gover;     /* gaps over the stall threshold (3x the typical frame interval, min 5 ms): the bus thread did not run */
        uint32_t tap;       /* DELETE-only tap replayed: seq << 16 | side << 8 | frames held */
        uint32_t tapx;      /* DELETE-only taps NOT replayed because the frames had a gap (stall) during the hold: a seq counter */
        uint32_t dlp;       /* DELETE+pad rewritten as SHIFT+pad: seq << 16 | side << 8 | pad kind (1..8) */
        uint32_t dle;       /* DELETE+pad on an EMPTY pad, swallowed: seq << 16 | side << 8 | pad kind (1..8) */
        uint32_t nedge;     /* frames that took the full path (an edge, a held SHIFT/DELETE or a pending replay) */
    };
    struct __attribute__((aligned(64))) {       /* line 2: tick-owned */
        uint32_t cfg;       /* tick writes (relaxed), filter reads: CFG_* | combo */
        uint32_t thr;       /* 100 us in cntvct ticks */
        uint32_t thr2;      /* 200 us */
        uint32_t gthr;      /* floor of the stall threshold, cntvct ticks (5 ms); the threshold is 3x the typical frame interval (fi), never below this */
        uint32_t req;       /* tick -> filter: replay SHIFT+pad: RQ_VALID | side << 9 | pad bits (0 = none) */
        uint32_t rp_frames; /* tick writes: replay length in FRAMES (40 ms at the measured frame rate) */
        uint32_t emp;       /* tick writes while DELETE is held on a side: pad bits known EMPTY (side A bits 0..7, side B 8..15), else 0 */
    };
} FS;
#define RQ_VALID 0x100u
#define FI_MIN 16u                  /* intervals needed before the stall test and the replay length are trusted */
#define FI_CAP 0x40000000ull        /* one interval sample, cntvct ticks (caps a stall that opens the measurement) */
_Static_assert(sizeof FS == 192 && __builtin_offsetof(typeof(FS), fmax) == 64 && __builtin_offsetof(typeof(FS), cfg) == 128, "FS: three lines, one writer each");
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
/* 0.2.8: hooks stamp events with the counter (no clock_gettime in a hook); the tick converts. */
static uint64_t cnt_hz = 1000000000ull;           /* set in init() from cntfrq_el0 */
static uint64_t cnt_to_ns(uint64_t v) { return (uint64_t)((unsigned __int128)v * 1000000000ull / cnt_hz); }
static int64_t  ring_off;                         /* tick: now_ns() - cnt_to_ns(cnt_now()), per drain */
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
static int h_pulse = -1;                    /* 0.2.11: Mod Menu row "Pulse armed pad" */
static uint32_t cfg_bits = (1u) | (0u << 1) | (1u << 3) | (1u << 4) | (1u << 5);
enum { CFG_ENABLE = 1u << 0, CFG_HOTCUE = 1u << 3, CFG_CLEAR = 1u << 4, CFG_TOAST = 1u << 5,
       CFG_AUTOADV = 1u << 6, CFG_REARM = 1u << 7, CFG_PULSE = 1u << 8 };
static int cfg_combo_val(uint32_t b) { return (int)((b >> 1) & 3u); }

enum { COMBO_SHIFT_MEMORY = 0, COMBO_OFF_PADS = 1, COMBO_PAD_ONLY = 2, NCOMBO = 3 };

static int plausible(uintptr_t p)
{
    return p >= 0x10000u && (p & 7u) == 0 && p < ((uintptr_t)1 << 48);
}

/* Fault-free read: process_vm_readv on ourselves returns EFAULT instead of SIGSEGV.
 * Tick-only (a syscall). */
#define LOG(...) do { if (C && AZ_HAS(C, log)) C->log(MOD_ID, __VA_ARGS__); } while (0)
/* 0.2.8 (al028c): the core keeps one log file of at most 256 KB per start, drops lines past that, and moves a file
 * over 128 KB to .log.1 at the next start (so a stress run "rotates at load": the run is in .log.1). The per-event
 * chatter (toasts, store/delete observer lines, set checks) is the bulk of it, so those lines go through LOGC: at
 * most 40 per 10 s (tick thread only); a summary line says how many were left out. Results (Active Loop <-,
 * armed loop deleted, pad:, timing, errors) are never throttled. */
static unsigned logc_n, logc_drop; static uint64_t logc_t0;
static int logc_ok(void)
{
    uint64_t now = now_ns();
    if (!logc_t0 || now - logc_t0 >= 10000000000ull) {
        if (logc_drop) { LOG("(%u routine log lines left out in the last 10 s: toasts, store/delete observer, set checks)", logc_drop); }
        logc_t0 = now; logc_n = 0; logc_drop = 0;
    }
    if (logc_n < 40) { logc_n++; return 1; }
    logc_drop++; return 0;
}
#define LOGC(...) do { if (logc_ok()) LOG(__VA_ARGS__); } while (0)

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
    LOGC("toast hidden (%s)", why);
}
static void do_toast_ms(const char *text, int ms, int ready_deck)  /* text: copied */
{
    if (!text) return;
    if (!(__atomic_load_n(&cfg_bits, __ATOMIC_RELAXED) & CFG_TOAST)) {
        LOGC("toast off: %s", text);
        return;
    }
    if (tsurf) {                                   /* updatable: replace in place, now */
        if (ts_shown) LOGC("toast superseded: '%s' -> '%s'", ts_text, text);
        snprintf(ts_text, sizeof ts_text, "%s", text);
        ts_ready_deck = ready_deck;
        ts_hide_ns = now_ns() + (uint64_t)ms * 1000000ull;
        C->surface_show(tsurf, 1); C->surface_redraw(tsurf); ts_shown = 1;
        LOGC("toast shown (%d ms): %s", ms, ts_text);
        return;
    }
    if (toast_pending) LOGC("toast replaced before showing: %s", toast_text);
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
    LOGC("toast queued (%d ms): %s", toast_ms, toast_text);
}

/* 0.2.6: set only when init() completed; until then (or after a failed init) every hook is a
 * pure pass-through to the firmware, so a partial claim leaves nothing active. */
static int al_live;

/* ---- hook side: copy raw args only. No facade deref, no core call, no log. ---- */
static int push_ev(void *facade, int kind, int src, const void *cue)
{
    if (!__atomic_load_n(&al_live, __ATOMIC_ACQUIRE)) return 0;
    uint64_t t0 = cnt_now();
    uint32_t i = __atomic_fetch_add(&ring_w, 1, __ATOMIC_ACQ_REL);
    ev_t *e = &ring[i % RING];
    /* 0.2.6: claim the slot (0 -> SEQ_BUSY) so two producers a lap apart cannot both write it */
    uint32_t free_seq = 0;
    if (!__atomic_compare_exchange_n(&e->seq, &free_seq, SEQ_BUSY, 0,
                                     __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        __atomic_add_fetch(&ring_drop, 1, __ATOMIC_RELAXED);
        return 0;
    }
    e->facade = facade;
    e->t_ns = t0;                           /* one counter read serves the stamp and the self-timing */
    e->draw = 0;
    e->kind = kind;
    e->src = src;
    e->has_cue = 0;
    /* The cue is a caller temporary, so it must be copied now; a raw byte copy would
     * keep dangling pointers of its owned members, so use the firmware copy-ctor
     * into the 16-aligned slot. Only for real cue kinds and a sane pointer. */
    if (src == SRC_DELETE) {
        /* 0.2.8: the delete observer reads two words of the caller's own cue, and calls nothing */
        if (cue && plausible((uintptr_t)cue)) {
            e->din = *(const int64_t *)((const uint8_t *)cue + CUE_IN_TIME);
            e->dout = *(const int64_t *)((const uint8_t *)cue + CUE_OUT_TIME);
            e->draw = 1;
        }
    } else if (cue && plausible((uintptr_t)cue) && kind >= 0 && kind <= 8) {
        fw_cue_copy(e->cue, cue);
        e->has_cue = 1;
    }
    __atomic_store_n(&e->seq, i + 1, __ATOMIC_RELEASE);
    max_upd(&hmax_cue, cnt_now() - t0);
    return 1;
}


/* Mod row off: every hook is a bare pass-through (no copy, no event, nothing for the tick). */
static inline int row_on(void) { return (__atomic_load_n(&cfg_bits, __ATOMIC_RELAXED) & CFG_ENABLE) != 0; }
/* 0.2.8 (al028c): with the row off the hooks still remember which facade objects they see (a pointer
 * store into 8 slots, nothing else, nothing logged) so the decks can be bound the moment the row goes
 * on again, without a track reload. The tick resolves them (player id), the hook never dereferences. */
#define NNF 8
static void *nf_tab[NNF];
static uint32_t nf_w;
static inline void note_facade(void *f)
{
    if (!f) return;
    for (int i = 0; i < NNF; i++) if (__atomic_load_n(&nf_tab[i], __ATOMIC_RELAXED) == f) return;
    uint32_t w = __atomic_load_n(&nf_w, __ATOMIC_RELAXED);
    __atomic_store_n(&nf_w, w + 1, __ATOMIC_RELAXED);
    __atomic_store_n(&nf_tab[w % NNF], f, __ATOMIC_RELAXED);
}
static void my_call(void *facade, uint32_t kind, void *cue)
{
    if (row_on()) push_ev(facade, (int)kind, SRC_CALL, cue); else note_facade(facade);
    orig_call(facade, kind, cue);
}
static void my_load(void *facade, uint32_t kind, void *cue)
{
    if (row_on()) push_ev(facade, (int)kind, SRC_LOAD, cue); else note_facade(facade);
    orig_load(facade, kind, cue);
}
static void my_store(void *store, int kind, void *cue)
{
    if (plausible((uintptr_t)store) && __atomic_load_n(&al_live, __ATOMIC_ACQUIRE)) __atomic_store_n(&store_any, store, __ATOMIC_RELEASE);
    if (row_on()) push_ev(NULL, kind, SRC_STORE, cue);
    orig_store(store, kind, cue);
}
static void my_setout(void *facade, int kind, int slip)
{
    orig_setout(facade, kind, slip);
    if (!row_on()) { note_facade(facade); return; }
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

/* 0.2.8: bind the decks whose facades the hooks saw while the row was off (tick only). Returns a
 * bit set of the decks bound now. Same player-id rule as deck_of_facade; a clash binds nothing. */
static unsigned bind_noted(void)
{
    unsigned got = 0;
    for (int i = 0; i < NNF; i++) {
        void *f = __atomic_load_n(&nf_tab[i], __ATOMIC_RELAXED);
        if (!f || !plausible((uintptr_t)f)) continue;
        int p = pid_of_facade((uintptr_t)f);
        if (p < 0 || engine_of[p]) continue;
        int clash = 0;
        for (int k = 0; k < fac_n; k++) if (fac_map[k].facade != f && fac_map[k].deck == p) clash = 1;
        if (clash) continue;
        fac_t *e = fac_get(f);
        if (!e) continue;
        e->pid = p; e->deck = p;
        bind_once(p, f);
        if (engine_of[p]) { got |= 1u << p; ready_sched(p); }
    }
    return got;
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

/* Per-side frame offsets (A = left, B = right), for the code that is not duplicated per side. */
static const struct { uint8_t sh_o, sh_b, mem_o, mem_b, del_o, del_b, pad_o; } SO[2] = {
    { A_SH_O, A_SH_B, A_MEM_O, A_MEM_B, A_DEL_O, A_DEL_B, A_PAD_O },
    { B_SH_O, B_SH_B, B_MEM_O, B_MEM_B, B_DEL_O, B_DEL_B, B_PAD_O },
};

/* 0.2.8 filter (fallback design; stock SHIFT+pad is a hot cue DELETE and cannot be swallowed):
 *   SHIFT+pad          pad eaten (no stock delete); the tick arms a loop pad, or asks for a replay
 *                      of the SHIFT+pad (FS.req) when the pad is a point cue, so stock deletes it.
 *   DELETE (alone)     the DELETE bit is eaten while held. A pad pressed meanwhile is rewritten to
 *                      SHIFT+pad (SHIFT bit set), so stock deletes that hot cue. Released with no
 *                      pad: DELETE is replayed for rp_frames frames (a tap), so plain DELETE works.
 *   SHIFT+MEMORY/DELETE  as 0.2.x (cycle / clear), bits eaten.
 * O(1) and wait-free: bit tests on the frame, edge counts into one u64 (one relaxed store), replay
 * timing by FRAME COUNT (the heartbeat). No lock, no allocation, no libc call, no read of cue or
 * LoopState memory (all of that is in the tick), and no clock read except 1 frame in 256 for the
 * self-measurement. The core's budget is 200 us per call, 8 strikes per session: what costs a call
 * 200 us under qemu is a cold code page or a translation-cache flush, not the work. */
/* The filter proper: no lock, no allocation, no libc call, no syscall, no log, no read of cue or
 * LoopState memory. It touches the frame (the core's buffer) and the three FS lines only; those
 * lines and this code are pre-run and mlock()ed at init. `t0` is the entry counter. */
static inline __attribute__((always_inline)) void erp_work(unsigned char *frame, uint64_t t0, int stall)
{
    uint32_t cfg = __atomic_load_n(&FS.cfg, __ATOMIC_RELAXED);
    uint64_t sn = FS.snap + (1ull << 56);                /* heartbeat = frame counter, 8 bits */
    unsigned hb = (unsigned)(sn >> 56);
    if (__builtin_expect(hb == 0, 0))                    /* once per 256 frames: the tick derives the frame rate */
        __atomic_store_n(&FS.stamp, (((FS.stamp >> 40) + 1) << 40) | (t0 & STAMP_MASK), __ATOMIC_RELAXED);
    if (!(cfg & CFG_ENABLE)) { __atomic_store_n(&FS.snap, sn, __ATOMIC_RELAXED); return; }
    uint32_t cur = SIDE_BITS(frame, A) | (SIDE_BITS(frame, B) << 14);
    uint32_t prev = FS.prev;
    uint32_t rpw; memcpy(&rpw, FS.rp, sizeof rpw);
    uint16_t pew; memcpy(&pew, FS.pe, sizeof pew);
    /* fast path: nothing relevant changed, SHIFT and DELETE up on both sides, nothing pending */
    if (__builtin_expect(((cur ^ prev) & 0x0fffffffu) == 0 && (prev >> 31) &&
                         !(cur & (5u | (5u << 14))) && !rpw && !pew &&
                         !__atomic_load_n(&FS.req, __ATOMIC_RELAXED), 1)) {
        __atomic_store_n(&FS.snap, sn, __ATOMIC_RELAXED);
        return;
    }
    uint32_t rise = (prev >> 31) ? (cur & ~prev) : 0u;
    uint32_t fall = (prev >> 31) ? (prev & ~cur & 0x0fffffffu) : 0u;
    FS.prev = cur | 0x80000000u;
    __atomic_store_n(&FS.nedge, FS.nedge + 1, __ATOMIC_RELAXED);
    int do_mem = cfg_combo_val(cfg) == COMBO_SHIFT_MEMORY;
    int do_clr = (cfg & CFG_CLEAR) != 0;
    int do_pad = (cfg & CFG_HOTCUE) != 0;
    uint32_t rq = __atomic_load_n(&FS.req, __ATOMIC_RELAXED);
    if (rq) {                                        /* the tick asks: replay SHIFT+pad once */
        unsigned rs = (rq >> 9) & 1u;
        __atomic_store_n(&FS.req, 0, __ATOMIC_RELAXED);
        FS.rp[rs] = (uint8_t)(rq & 0xffu); FS.rp_t0[rs] = (uint8_t)hb;
    }
    const uint32_t rpf = FS.rp_frames;
    const uint32_t emp = __atomic_load_n(&FS.emp, __ATOMIC_RELAXED);
    for (int s = 0; s < 2; s++) {
        unsigned c = (cur >> (14 * s)) & 0x3fffu, r = (rise >> (14 * s)) & 0x3fffu;
        unsigned f = (fall >> (14 * s)) & 0x3fffu;
        unsigned base = 28u * (unsigned)s, padnew = r >> 6;
        if (r) {
            if ((c & 1u) && (r & 2u) && do_mem) NIB_INC(sn, base + 0);
            if ((c & 1u) && (r & 4u) && do_clr) NIB_INC(sn, base + 4);
            if ((c & 1u) && padnew && do_pad) {
                NIB_INC(sn, base + 8);
                sn = (sn & ~(0xfull << (base + 24))) |
                     ((uint64_t)(__builtin_ctz(padnew) + 1) << (base + 24));
            }
            /* PRESS: MEMORY, CALL or a pad went down on this side. The tick uses it to
             * attribute the deleteCue / store hook events to a deck. */
            if ((r & (2u | 8u)) || padnew) NIB_INC(sn, base + 12);
            if (r & 16u) NIB_INC(sn, base + 16);
            if (r & 32u) NIB_INC(sn, base + 20);
        }
        const unsigned char shm = (unsigned char)(1u << SO[s].sh_b);
        const int sh = (c & 1u) != 0, dl = (c & 4u) != 0;
        if (do_pad) {
            /* DELETE hold state, kept until DELETE itself is released (0.2.8 al028c: it used to be cleared when
             * SHIFT joined, so SHIFT+DELETE, SHIFT up, DELETE up made a tap = a stray memory-cue delete).
             * A pad or SHIFT during the hold makes it "not a tap". A tap is replayed only when DELETE alone
             * was held and released. A new DELETE press ends an older tap replay at once (it used to add its
             * DELETE bit back onto the new DELETE+pad frames: stock then also deleted the load/called memory cue). */
            if (dl) {
                if (!FS.dh[s]) { FS.dh[s] = 1; FS.du[s] = 0; FS.dh_t0[s] = (uint8_t)hb; FS.rp[2 + s] = 0; }
                if (stall) FS.du[s] |= 2;                        /* frames were missing: a pad may have come and gone unseen */
                if (sh || (c >> 6)) FS.du[s] |= 1;
            } else {
                if (stall && FS.dh[s]) FS.du[s] |= 2;
                if ((f & 4u) && FS.dh[s] && FS.du[s] == 2 && !sh)    /* alone, but frames were missing: do NOT guess a tap */
                    __atomic_store_n(&FS.tapx, FS.tapx + 1, __ATOMIC_RELAXED);
                if ((f & 4u) && FS.dh[s] && !FS.du[s] && !sh) {      /* released, alone: replay a tap */
                    FS.tap = ((FS.tap & 0xffff0000u) + 0x10000u) | ((uint32_t)s << 8) | (uint8_t)(hb - FS.dh_t0[s]);
                    FS.rp[2 + s] = 1; FS.dh_t0[s] = (uint8_t)hb;
                    NIB_INC(sn, base + 12);                            /* PRESS, for the deleteCue event */
                }
                FS.dh[s] = 0; FS.du[s] = 0;
            }
        }
        /* eat / rewrite (level based).
         * pe (0.2.8 al028d): a pad that went down while SHIFT was held is SHIFT+pad's, and stays eaten until the
         * pad itself is up: if SHIFT came up first the pad level used to reach stock as a NEW plain press (on an
         * empty pad: a stored hot cue; on a loop pad: the loop). The same for a pad pressed under DELETE on an
         * EMPTY pad (emp, published by the tick while DELETE is held): swallowed, never sent as SHIFT+pad. */
        int lead = 0;                                   /* the filter makes SHIFT up on this frame itself */
        unsigned pe = FS.pe[s], padr = (r >> 6) & 0xffu;
        if (do_pad && sh) pe |= padr;
        if (sh) {
            if (do_mem) frame[SO[s].mem_o] &= (unsigned char)~(1u << SO[s].mem_b);
            if (do_clr) frame[SO[s].del_o] &= (unsigned char)~(1u << SO[s].del_b);
            if (do_pad) frame[SO[s].pad_o] = 0;
        } else if (do_pad && dl) {
            frame[SO[s].del_o] &= (unsigned char)~(1u << SO[s].del_b);
            const unsigned em = (emp >> (8 * s)) & 0xffu, sw = padr & em & ~pe;
            if (sw) {                                    /* a pad pressed now is EMPTY: nothing to delete, nothing to store */
                FS.dle = ((FS.dle & 0xffff0000u) + 0x10000u) | ((uint32_t)s << 8) | (uint32_t)(__builtin_ctz(sw) + 1);
                pe |= sw;
            }
            frame[SO[s].pad_o] &= (unsigned char)~pe;
            if (frame[SO[s].pad_o]) {                    /* DELETE+pad -> SHIFT+pad */
                frame[SO[s].sh_o] |= shm; lead = 1;
                if (padr & ~pe) {
                    FS.dlp = ((FS.dlp & 0xffff0000u) + 0x10000u) | ((uint32_t)s << 8) | (uint32_t)(__builtin_ctz(padr & ~pe) + 1);
                }
            }
        } else if (do_pad) frame[SO[s].pad_o] &= (unsigned char)~pe;
        if (do_pad) { pe &= (c >> 6) & 0xffu; FS.pe[s] = (uint8_t)pe; }
        if (FS.rp[s]) {
            if ((unsigned)((uint8_t)(hb - FS.rp_t0[s])) < rpf) {
                frame[SO[s].pad_o] |= FS.rp[s]; frame[SO[s].sh_o] |= shm; lead = lead | 2;
            } else FS.rp[s] = 0;
        }
        if (FS.rp[2 + s]) {
            if (!dl && (unsigned)((uint8_t)(hb - FS.dh_t0[s])) < rpf) frame[SO[s].del_o] |= (unsigned char)(1u << SO[s].del_b);
            else FS.rp[2 + s] = 0;
        }
        /* SHIFT leads the pad by one frame whenever the filter (not the player's finger) raises SHIFT: a real
         * SHIFT+pad always arrives SHIFT first, and stock may decode both bits of one frame in either order
         * (0.2.8 al028c: the first DELETE+pad after a quiet period acted as a plain pad + DELETE). */
        if (lead && !sh) {
            if (!FS.sl[s]) {
                FS.sl[s] = 1;
                unsigned char m = 0;
                if (lead & 1) m |= (unsigned char)(padr & ~pe);   /* pad bits that rose on this very frame */
                if (lead & 2) m |= FS.rp[s];                    /* a replay that starts now */
                frame[SO[s].pad_o] &= (unsigned char)~m;        /* this frame: SHIFT only; the pad follows on the next */
            }
        } else FS.sl[s] = 0;
    }
    __atomic_store_n(&FS.snap, sn, __ATOMIC_RELAXED);   /* the one published store */
}

/* 0.2.8 (al028c): every call is timed (two counter reads, no syscall), so a strike can be told
 * from a stall. `fmax`/`fover`/`fover2` are the call's own wall time: preemption inside the call
 * counts, which is what the core's timer counts too. `gmax`/`gover` are the gaps between two calls
 * (the frame period): a gap of many ms with a short call is the guest not running this thread. */
__attribute__((hot)) static int on_erp(int bus, unsigned char *frame, int len, void *user)
{
    (void)bus; (void)user;
    const uint64_t t0 = cnt_now();
    if (__builtin_expect(!frame || len < ERP_LEN, 0)) return AZ_FRAME_OK;
    const uint64_t last = FS.last_t, gap = t0 - last;
    FS.last_t = t0;
    int stall = 0;
    if (last != 0) {
        /* 0.2.8 al028d: the stall test is relative to the TYPICAL frame interval fi (the emulator serves ~17 frames/s, the
         * deck ~1000/s; a fixed 40 ms made every emulator frame a "stall"). fi is a running estimate that follows the
         * short intervals quickly (1/4 step) and the long ones slowly (1/16 step, each sample capped at 2*fi), i.e. it
         * sits at the lower-middle of the intervals and a stall cannot drag it up. stall = gap > 3*fi, never below the
         * floor (5 ms). Integer only, no call; judged only once FI_MIN intervals are known. */
        const uint32_t fi = __atomic_load_n(&FS.fi, __ATOMIC_RELAXED), fn = FS.fn;
        uint64_t lim = 3ull * fi, fl = __atomic_load_n(&FS.gthr, __ATOMIC_RELAXED);
        if (lim < fl) lim = fl;
        stall = fn >= FI_MIN && gap > lim;
        uint64_t g = gap > FI_CAP ? FI_CAP : gap, nf;
        if (!fi) nf = g ? g : 1;
        else if (g < fi) nf = fi - ((fi - g) >> 2);
        else { uint64_t cl = g > 2ull * fi ? 2ull * fi : g; nf = fi + ((cl - fi) >> 4); }
        __atomic_store_n(&FS.fi, (uint32_t)nf, __ATOMIC_RELAXED);
        if (fn < 255) FS.fn = (uint8_t)(fn + 1);
    }
    erp_work(frame, t0, stall);
    const uint64_t t1 = cnt_now();
    const uint64_t dt = t1 - t0;
    if (__builtin_expect(dt > FS.fmax, 0)) __atomic_store_n(&FS.fmax, (uint32_t)(dt > 0xffffffffu ? 0xffffffffu : dt), __ATOMIC_RELAXED);
    if (__builtin_expect(dt > __atomic_load_n(&FS.thr, __ATOMIC_RELAXED), 0)) {
        __atomic_store_n(&FS.fover, FS.fover + 1, __ATOMIC_RELAXED);
        if (dt > __atomic_load_n(&FS.thr2, __ATOMIC_RELAXED)) __atomic_store_n(&FS.fover2, FS.fover2 + 1, __ATOMIC_RELAXED);
    }
    if (__builtin_expect(last != 0 && gap > FS.gmax, 0)) __atomic_store_n(&FS.gmax, (uint32_t)(gap > 0xffffffffu ? 0xffffffffu : gap), __ATOMIC_RELAXED);
    if (__builtin_expect(stall, 0)) __atomic_store_n(&FS.gover, FS.gover + 1, __ATOMIC_RELAXED);
    return AZ_FRAME_OK;
}

/* Run every filter path once at init (before registering): the first execution of a code
 * path is the slow one (page-in, i-cache; on the qemu emulator, JIT translation). */
static void filter_thresholds(void)
{
    uint64_t fq = cnt_frq(); if (!fq) fq = 1000000000ull;
    FS.thr = (uint32_t)(fq / 10000u);      /* 100 us */
    FS.thr2 = (uint32_t)(fq / 5000u);      /* 200 us: the core's budget per call */
    FS.gthr = (uint32_t)(fq / 200u);       /* 5 ms: the floor of the stall threshold (3x the typical frame interval, measured by the filter) */
}
static void prewarm_filter(void)
{
    filter_thresholds();
    static const uint8_t pads[] = { 1, 2, 4, 128 };   /* pads still go through the SIDE_BITS / PRESS path */
    unsigned char f[ERP_LEN];
    uint32_t keep = FS.cfg;
    FS.cfg = CFG_ENABLE | CFG_CLEAR | CFG_HOTCUE;
    FS.rp_frames = 4;
    FS.snap = 255ull << 56;                   /* the first call lands on heartbeat 0: the timed path runs too */
    for (int rep = 0; rep < 3; rep++)
        for (int step = 0; step < 16; step++) {
            memset(f, 0, sizeof f);
            if (step & 1) { f[A_SH_O] |= 1u << A_SH_B; f[B_SH_O] |= 1u << B_SH_B; }
            if (step & 2) { f[A_MEM_O] |= (1u << A_MEM_B) | 0x10; f[B_MEM_O] |= (1u << B_MEM_B) | 0x20; }
            if (step & 4) { f[A_DEL_O] |= 1u << A_DEL_B; f[B_DEL_O] |= 1u << B_DEL_B;
                            f[A_SEC_O] |= 1u << A_SEC_B; f[B_PRI_O] |= 1u << B_PRI_B; }
            if (step & 8) { f[A_PAD_O] = pads[rep]; f[B_PAD_O] = pads[rep + 1]; }
            FS.emp = rep == 1 ? (uint32_t)pads[rep] | ((uint32_t)pads[rep + 1] << 8) : 0;   /* the empty-pad swallow path runs once */
            on_erp(AZ_BUS_ERP_IN, f, ERP_LEN, NULL);
            memset(f, 0, sizeof f);
            on_erp(AZ_BUS_ERP_IN, f, ERP_LEN, NULL);
        }
    memset(f, 0, sizeof f);
    FS.req = RQ_VALID | 0x01u; on_erp(AZ_BUS_ERP_IN, f, ERP_LEN, NULL);        /* replay path, side A */
    FS.req = RQ_VALID | (1u << 9) | 0x80u; on_erp(AZ_BUS_ERP_IN, f, ERP_LEN, NULL);   /* side B */
    on_erp(AZ_BUS_ERP_IN, f, ERP_LEN, NULL);
    FS.snap = 0; FS.prev = 0; FS.cfg = keep;
    FS.stamp = 0; FS.last_t = 0; FS.fmax = 0; FS.fover = 0; FS.fover2 = 0; FS.gmax = 0; FS.gover = 0; FS.tap = 0; FS.tapx = 0; FS.dlp = 0; FS.dle = 0; FS.nedge = 0;
    FS.fi = 0; FS.fn = 0; FS.emp = 0; memset(FS.pe, 0, sizeof FS.pe);
    FS.rp_frames = 12; memset(FS.rp, 0, sizeof FS.rp); memset(FS.rp_t0, 0, sizeof FS.rp_t0); memset(FS.dh, 0, sizeof FS.dh);
    memset(FS.du, 0, sizeof FS.du); memset(FS.dh_t0, 0, sizeof FS.dh_t0);
    FS.req = 0;
}

/* ---- 0.2.8: keep our own code resident ----
 * The core gives the filter 200 us per call and 8 strikes per session. Under memory pressure (the
 * firmware writing databases and analysis files in a burst of cue stores/deletes) the kernel can drop
 * clean file-backed pages of this library; the next frame then takes a major page fault, which is
 * milliseconds, not the ~100 ns the filter needs (al028b: first strike 1801.8 us, always after a burst).
 * So at init the mod's own read-only mapped segments (code and rodata, about 80 KB) are mlock()ed. If that is refused (the
 * memlock limit), the tick touches one byte per page of those segments a few times a second, which
 * keeps them young in the page cache instead. No allocation, nothing on the filter path. */
#define PIN_MAX 4
static struct { uintptr_t lo, hi; } pin_seg[PIN_MAX];
static int pin_n, pin_locked, pin_bytes, pin_dlocked;
static uintptr_t pin_dlo, pin_dhi;
static volatile uint8_t pin_sink;
static int pin_cb(struct dl_phdr_info *info, size_t sz, void *arg)
{
    (void)sz; (void)arg;
    uintptr_t me = (uintptr_t)&pin_cb;
    int mine = 0;
    for (int i = 0; i < info->dlpi_phnum; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD) continue;
        uintptr_t lo = info->dlpi_addr + ph->p_vaddr, hi = lo + ph->p_memsz;
        if (me >= lo && me < hi) mine = 1;
    }
    if (!mine) return 0;
    long pg = sysconf(_SC_PAGESIZE); if (pg <= 0) pg = 4096;
    for (int i = 0; i < info->dlpi_phnum && pin_n < PIN_MAX; i++) {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_R) || (ph->p_flags & PF_W)) continue;   /* code + rodata: clean, file-backed, evictable */
        uintptr_t lo = (info->dlpi_addr + ph->p_vaddr) & ~(uintptr_t)(pg - 1);
        uintptr_t hi = (info->dlpi_addr + ph->p_vaddr + ph->p_memsz + (uintptr_t)pg - 1) & ~(uintptr_t)(pg - 1);
        pin_seg[pin_n].lo = lo; pin_seg[pin_n].hi = hi; pin_n++;
    }
    return 1;
}
static void pin_code(void)
{
    pin_n = pin_locked = pin_bytes = 0;
    dl_iterate_phdr(pin_cb, NULL);
    int ok = pin_n > 0;
    for (int i = 0; i < pin_n; i++) {
        pin_bytes += (int)(pin_seg[i].hi - pin_seg[i].lo);
        if (mlock((const void *)pin_seg[i].lo, pin_seg[i].hi - pin_seg[i].lo) != 0) ok = 0;
    }
    pin_locked = ok;
    /* the filter's own data (the three FS lines, 192 bytes, may straddle a page): written once now so
     * the pages exist (no first-touch fault on the bus thread), then locked */
    {
        long pg = sysconf(_SC_PAGESIZE); if (pg <= 0) pg = 4096;
        uintptr_t lo = (uintptr_t)&FS & ~(uintptr_t)(pg - 1);
        uintptr_t hi = ((uintptr_t)&FS + sizeof FS + (uintptr_t)pg - 1) & ~(uintptr_t)(pg - 1);
        /* write one byte of each of the three lines (back the page, no read-only zero page, no copy-on-write
         * later); the value written is the one already there */
        for (unsigned ln = 0; ln < sizeof FS; ln += 64) { volatile uint8_t *q = (volatile uint8_t *)&FS + ln; *q = *q; }
        pin_dlo = lo; pin_dhi = hi;
        pin_dlocked = mlock((const void *)lo, hi - lo) == 0;
    }
    LOG("code pages: %d segments, %d KB, %s; filter data: %d page(s), %s", pin_n, pin_bytes / 1024,
        pin_n == 0 ? "not found" : ok ? "locked in memory (mlock)" : "mlock refused: touched from the tick instead",
        (int)((pin_dhi - pin_dlo) / (uintptr_t)(sysconf(_SC_PAGESIZE) > 0 ? sysconf(_SC_PAGESIZE) : 4096)),
        pin_dlocked ? "locked" : "mlock refused: touched from the tick instead");
}
static void pin_keepwarm(void)
{
    if ((pin_locked && pin_dlocked) || (tick_no & 7)) return;
    long pg = sysconf(_SC_PAGESIZE); if (pg <= 0) pg = 4096;
    uint8_t acc = 0;
    if (!pin_locked)
        for (int i = 0; i < pin_n; i++)
            for (uintptr_t a = pin_seg[i].lo; a < pin_seg[i].hi; a += (uintptr_t)pg) acc ^= *(volatile const uint8_t *)a;
    if (!pin_dlocked)
        for (unsigned ln = 0; ln < sizeof FS; ln += 64) acc ^= *((volatile const uint8_t *)&FS + ln);
    pin_sink = acc;
}

/* ---- tick-side caches ---- */
static int64_t cue_in(const void *c)  { return *(const int64_t *)((const uint8_t *)c + CUE_IN_TIME); }
static int64_t cue_out(const void *c) { return *(const int64_t *)((const uint8_t *)c + CUE_OUT_TIME); }
static const char *src_name(int s)
{
    return s == SRC_CALL ? "call" : s == SRC_LOAD ? "load" : s == SRC_STORE ? "store" :
           s == SRC_DELETE ? "delete" : "setout";
}

/* 0.2.1: loops read from ANLZ are in ms, so match within MATCH_TOL units (~2 ms) */
#define MATCH_TOL 100
static uint8_t mem_flag[NDECK][NMEM];   /* 0.2.2: rekordbox Active Loop flag (from ANLZ); 0.2.9: also the flag we set */
#define OWN_NONE 0
#define OWN_PEND 1                      /* stored by SHIFT+pad this session, read-back not confirmed yet */
#define OWN_OK   2                      /* stored by SHIFT+pad this session AND confirmed in the ANLZ read-back */
static uint8_t mem_mine[NDECK][NMEM];   /* 0.2.10: OWN_*. The mod deletes a memory loop ONLY when this is OWN_OK (never one it did not make and verify) */
static uint8_t mem_syn[NDECK][NMEM];    /* 1 = built from ANLZ (template + in/out), not yet seen live */
static int near(int64_t a, int64_t b) { int64_t x = a - b; return x <= MATCH_TOL && x >= -MATCH_TOL; }
/* 0.2.8: delete matching. The ANLZ-built memory loop can sit ~0.2 s off the firmware's cue
 * (AZ-EMU al028: 06:33.5 vs 06:33.7), so deletes match by the nearest in/out within 0.5 s. */
#define DEL_TOL 22050
static int near_del(int64_t a, int64_t b) { int64_t x = a - b; return x <= DEL_TOL && x >= -DEL_TOL; }

/* ---- 0.2.8: deleteCue OBSERVER (0x861218) ----
 * CueCallManager::deleteCue is the PERSISTENCE half of a cue delete (it queues the delete for the
 * rekordbox DB / ANLZ writer). The live hot cue is cleared earlier, by CueLoopSetFacade::clearCue
 * (0x1431720), so swallowing deleteCue cannot stop a delete (AZ-EMU al028: "-> arm" logged and the
 * hot cue was gone anyway). The hook therefore never changes behaviour: it copies (kind, cue) into
 * the ring and calls the original. The tick uses it to drop its stale copy of a deleted cue and to
 * clear the Active Loop when that loop was deleted. Mod row off: pure pass-through, no event. */
typedef void (*delete_fn)(void *mgr, int kind, const void *cue);
static delete_fn orig_delete;
static void my_delete(void *mgr, int kind, const void *cue)
{
    if (plausible((uintptr_t)mgr)) __atomic_store_n(&mgr_any, mgr, __ATOMIC_RELAXED);
    if (__atomic_load_n(&al_live, __ATOMIC_ACQUIRE) && row_on())
        (void)push_ev(NULL, kind, SRC_DELETE, cue);
    orig_delete(mgr, kind, cue);
}
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
    if (fresh) { mem_flag[d][i] = 0; mem_mine[d][i] = 0; }
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
    memset(mem_mine[d], 0, sizeof mem_mine[d]);
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
static void on_delete_ev(ev_t *e);
static void handle_ev(ev_t *e)
{
    int d = -1;
    e->t_ns = (uint64_t)((int64_t)cnt_to_ns(e->t_ns) + ring_off);    /* hook counter -> tick clock */
    if (e->src == SRC_DELETE) { on_delete_ev(e); return; }
    if (e->src == SRC_STORE) {
        static int store_armed;
        if (!store_armed) { store_armed = 1; LOG("store armed (store=%p)", __atomic_load_n(&store_any, __ATOMIC_ACQUIRE)); }
        d = press_deck_for(e->t_ns);          /* the MEMORY press side, never a default */
        if (d < 0) { LOGC("store kind=%d: no single recent press, not attributed", e->kind); return; }
    } else {
        d = deck_of_facade(e->facade, e->src, e->t_ns);
        if (d >= 0 && e->src == SRC_LOAD) {
            load_cue_seen(d);
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
    LOGC("%s deck %d kind=%d loop=%d", src_name(e->src), d + 1, e->kind, loop);
    if (e->src == SRC_LOAD || e->src == SRC_CALL) base_note(d, e);
    if (e->kind == KIND_MEM && e->has_cue && (e->src == SRC_CALL || e->src == SRC_STORE)) {
        last_call_in[d] = cue_in(e->cue); last_call_out[d] = cue_out(e->cue); last_call_ok[d] = 1;
    }
    if (!loop) {
        /* 0.2.8: the firmware now holds a point cue (or nothing) in this hot slot, so a loop we
         * cached for it is stale (a delete or overwrite that did not reach the delete hook). */
        if (e->kind >= 1 && e->kind <= 8 && hot_ok[d][e->kind]) {
            LOG("deck %d: hot cue %d is no longer a loop (%s), dropped from the loop list", d + 1, e->kind, src_name(e->src));
            fw_cue_dtor(hot_buf[d][e->kind]); hot_ok[d][e->kind] = 0;
        }
        return;
    }
    if (e->kind == KIND_MEM) {
        int i = mem_put(d, e->cue);
        if (mem_syn[d][i]) LOG("deck %d: ANLZ memory loop #%d now seen live (real cue replaces the built one)", d + 1, i);
        mem_live[d][i] = 1; mem_syn[d][i] = 0;
        {   /* 0.2.9 evidence for the AZ-EMU run: the flag byte the firmware itself hands us (logged, never acted on) */
            static unsigned e1_log;
            if ((e->cue[CP_FLAG] || e->src == SRC_STORE) && e1_log < 24) {
                e1_log++;
                LOG("deck %d: live memory loop #%d (%s) CueProperty+0xe1=%u, ANLZ flag=%u", d + 1, i, src_name(e->src), e->cue[CP_FLAG], mem_flag[d][i]);
            }
        }
        if (e->src != SRC_LOAD) last_mem[d] = i;
        else if (last_mem[d] < 0) last_mem[d] = i;
    } else if (e->kind >= 1 && e->kind <= 8) {
        hot_put(d, e->kind, e->cue);
    }
}

static void drain_ring(void)
{
    ring_off = (int64_t)now_ns() - (int64_t)cnt_to_ns(cnt_now());
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
/* 0.2.8 al028f: the Active Loop of the PREVIOUS load of a deck, kept only to restore it when the same track is loaded again
 * (the live armed state - act_ok / active_* / arm_in - is always cleared on a content change, so an old track's loop can
 * never be armed, engaged or counted on a new one). */
static uint8_t  rem_buf[NDECK][CUE_SIZE] __attribute__((aligned(64)));
static uint8_t  rem_ok[NDECK];
static uint32_t rem_cid[NDECK];
static int64_t  rem_in[NDECK], rem_out[NDECK];
static uint8_t  clear_owed[NDECK];        /* tick: the firmware's Active Loop of this deck must be cleared (a track change dropped ours) */
static uint8_t  tc_seen[NDECK];           /* load_changed: a load was handled for this deck ... */
static long     tc_tick[NDECK];           /* ... at this tick ... */
static uint32_t tc_cid[NDECK];            /* ... with this content id (0 = not known yet) */
static const char *ld_pend[NDECK];        /* "track loaded" line waiting for the content id */
static long     ld_pend_tick[NDECK];
static uint32_t deck_evt;   /* atomic: deck-event flags for tick */
static uint32_t deck_cid(int d)
{
    az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
    if (!AZ_HAS(C, deck_info) || C->deck_info(d, &di) != 0 || !di.loaded) return 0;
    return di.content_id;
}
/* al028g: the content id of the load in progress. The load cues run BEFORE the deck reports the new track, so for about a second
 * after the last load cue deck_cid() still says the PREVIOUS track: it is not used then (0 = not known yet). Once no load cue has
 * come for 60 ticks (1 s) it is the id try_restore and anlz_service use. Every id the load code keeps or logs comes from here. */
static uint32_t load_id(int d)
{
    if (last_load_tick[d] && tick_no - last_load_tick[d] < 60) return 0;
    return deck_cid(d);
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
    int memcues;                            /* 0.2.10: memory cues (points AND loops) in the file: the firmware's 10-cue limit counts both */
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
static int anlz_loops(const uint8_t *b, long n, al_t *e, int cap, int *nn, int *nmem)
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
                    if (hot == 0 && ty >= 1 && ty <= 2 && nmem) ++*nmem;
                    if (ty == 2 && tout != 0xffffffffu && tout > tin && *nn < cap) e[(*nn)++] = (al_t){ (int32_t)hot, tin, tout, 0 };
                } else if (pcob && !memcmp(c, "PCPT", 4) && le >= 0x28) {
                    uint32_t hot = be32(c + 0x0c), ty = c[0x1c], tin = be32(c + 0x20), tout = be32(c + 0x24);
                    if (hot == 0 && ty >= 1 && ty <= 2 && nmem) ++*nmem;
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
    int got = -1; r->n = 0; r->memcues = 0;
    snprintf(p, sizeof p, "%s/ANLZ0000.EXT", dir);
    long n = slurp(p, anlz_buf, sizeof anlz_buf);
    if (n > 0 && anlz_loops(anlz_buf, n, r->e, NAL, &r->n, &r->memcues) == 0) got = 0;
    /* 0.2.2: the Active Loop flag only exists in PCPT (PCOB); EXT's PCO2 has none, so the DAT
     * is always read too: its flags are copied onto the EXT entries (same in/out within 2 ms),
     * or its entries are used when the EXT gave no loops. */
    {
        static al_t dat[NAL]; int dn = 0, dmem = 0;
        snprintf(p, sizeof p, "%s/ANLZ0000.DAT", dir);
        n = slurp(p, anlz_buf, sizeof anlz_buf);
        if (n > 0 && anlz_loops(anlz_buf, n, dat, NAL, &dn, &dmem) == 0) {
            if (dmem > r->memcues) r->memcues = dmem;                /* the firmware writes memory cues to the DAT: the larger count wins */
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
static int      act_src_idx[NDECK];       /* tick: hot kind (1..8) or memory index it was armed from */
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
static int spent[NDECK], ex_inside[NDECK], ex_past[NDECK];
/* 0.2.7: one-shot diagnostics per armed loop (AZ-EMU D3 run: armed, never inside) */
static int      dg_near[NDECK], dg_stall[NDECK];
static int64_t  dg_pos[NDECK];
static uint64_t dg_ns[NDECK];
static void drop_rem(int d)
{
    if (rem_ok[d]) { fw_cue_dtor(rem_buf[d]); rem_ok[d] = 0; }
    rem_cid[d] = 0;
}
/* A content change (or eject): the armed Active Loop of the old load stops being live NOW (act_ok = 0, so the tick no longer
 * publishes it to the hook, counts it, watches it or lets Auto-advance use it) and, if it was still armed, the firmware's copy
 * is cleared by loop_guard(). A copy is kept in rem_* for the restore of a reload of the SAME track; a loop that already
 * exited (spent) is not kept. Nothing is kept for a deck that had nothing armed (an older remembered loop stays). */
static void stash_active(int d)
{
    if (!act_ok[d]) return;
    drop_rem(d);
    if (!spent[d] && active_cid[d]) {
        memset(rem_buf[d], 0, CUE_SIZE);
        fw_cue_copy(rem_buf[d], act_buf[d]);
        rem_ok[d] = 1; rem_cid[d] = active_cid[d]; rem_in[d] = active_in[d]; rem_out[d] = active_out[d];
    }
    if (!spent[d]) clear_owed[d] = 1;
    fw_cue_dtor(act_buf[d]); act_ok[d] = 0; active_cid[d] = 0; spent[d] = 0;
    ex_inside[d] = 0; ex_past[d] = 0; dg_near[d] = 0; dg_stall[d] = 0; dg_pos[d] = -1; dg_ns[d] = 0;
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
    LOGC("set check deck %d: holder loop=%d, LoopState has-loop=%d looping=%d", d + 1, held, reg, looping);
}
/* tick: finish / fall back */
static void pend_service(void)
{
    for (int d = 0; d < NDECK; d++) {
        uint32_t op = __atomic_load_n(&pend_op[d], __ATOMIC_ACQUIRE);
        if (op == OP_DONE) {
            __atomic_store_n(&pend_op[d], OP_IDLE, __ATOMIC_RELEASE);
            if (row_on()) { LOGC("deck %d: %s applied on the firmware playhead thread", d + 1, pend_why[d]); log_set_check(d); }
        } else if ((op == OP_SET || op == OP_CLEAR) && (!orig_echeck || tick_no - pend_tick[d] > 30)) {
            uint32_t exp = op;
            if (!__atomic_compare_exchange_n(&pend_op[d], &exp, OP_BUSY, 0, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                continue;
            void *h = holder_of(d);
            if (h) { if (op == OP_SET) fw_set(h, set_buf[d]); else fw_clear(h); }
            __atomic_store_n(&hk_need[d], 0, __ATOMIC_RELAXED);
            __atomic_store_n(&pend_op[d], OP_IDLE, __ATOMIC_RELEASE);
            if (row_on()) { LOG("deck %d: %s applied from tick (playhead hook idle, deck stopped?)", d + 1, pend_why[d]); log_set_check(d); }
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
static void hot_refresh(int d);
/* 0.2.8: the same loop can be stored as a memory loop and as a hot cue (same in/out). It is ONE
 * loop for cycling and counting: a hot cue that repeats a live memory loop (or an earlier hot
 * cue) is skipped, so "N loops ahead" is the number of stops SHIFT+MEMORY will really make. */
static int hot_dup(int d, int k)
{
    int64_t in = cue_in(hot_buf[d][k]), out = cue_out(hot_buf[d][k]);
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_ok[d][i] && mem_live[d][i] && fw_is_loop(mem_buf[d][i]) &&
            near(cue_in(mem_buf[d][i]), in) && near(cue_out(mem_buf[d][i]), out)) return 1;
    for (int j = 1; j < k; j++)
        if (hot_ok[d][j] && fw_is_loop(hot_buf[d][j]) &&
            near(cue_in(hot_buf[d][j]), in) && near(cue_out(hot_buf[d][j]), out)) return 1;
    return 0;
}
static int hot_usable(int d, int k)
{
    return hot_ok[d][k] && fw_is_loop(hot_buf[d][k]) && !hot_dup(d, k);
}
static const uint8_t *next_ahead(int d, int64_t after, int *is_hot, int *idx)
{
    hot_refresh(d);
    const uint8_t *best = NULL; int64_t bi = 0;
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_ok[d][i] && mem_live[d][i] && fw_is_loop(mem_buf[d][i]) && cue_in(mem_buf[d][i]) > after &&
            (!best || cue_in(mem_buf[d][i]) < bi)) { best = mem_buf[d][i]; bi = cue_in(best); *is_hot = 0; *idx = i; }
    for (int k = 1; k < NHOT; k++)
        if (hot_usable(d, k) && cue_in(hot_buf[d][k]) > after &&
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
    hot_refresh(d);
    int n = 0;
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_ok[d][i] && mem_live[d][i] && fw_is_loop(mem_buf[d][i]) && cue_in(mem_buf[d][i]) > after) n++;
    for (int k = 1; k < NHOT; k++)
        if (hot_usable(d, k) && cue_in(hot_buf[d][k]) > after) n++;
    return n;
}
static int pick_quiet;   /* 0.2.8: caller toasts itself */
static int pick_and_set(int d, const uint8_t *c, int hot, int idx, const char *why)
{
    char t[32]; fmt_time(t, sizeof t, cue_in(c));
    if (request(d, OP_SET, c, why) != 0) { LOG("deck %d: set busy, try again", d + 1); return -1; }
    active_note(d, c, hot);
    act_src_idx[d] = idx;
    spent[d] = 0; ex_inside[d] = 0; ex_past[d] = 0;
    dg_near[d] = 0; dg_stall[d] = 0; dg_pos[d] = -1; dg_ns[d] = 0;
    if (!hot) last_mem[d] = idx;
    LOG("deck %d: Active Loop <- %s %d%s at %s in=%lld out=%lld (%s), queued for the playhead thread",
        d + 1, hot ? "hot cue" : "memory", idx, (!hot && mem_syn[d][idx]) ? " (from ANLZ)" : "", t,
        (long long)cue_in(c), (long long)cue_out(c), why);
    if (!pick_quiet) toastf(hot ? "Active Loop: hot cue at %s" : "Active Loop: memory loop at %s", t);
    return 0;
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
static int clear_active(int d, const char *why)
{
    if (request(d, OP_CLEAR, NULL, why) != 0) { LOG("deck %d: clear busy, try again", d + 1); return -1; }
    if (act_ok[d]) { fw_cue_dtor(act_buf[d]); act_ok[d] = 0; }
    active_cid[d] = 0; restore_pending[d] = 0; spent[d] = 0; drop_rem(d);
    LOGC("cleared deck %d (queued)", d + 1);
    return 0;
}
static void apply_clear(int d)
{
    if (!holder_of(d)) { LOG("clear abort: deck %d not bound", d + 1); do_toast("Active Loop: deck not ready"); return; }
    if (clear_active(d, "clear") == 0) do_toast("Active Loop cleared");
}

/* ---- 0.2.9: ACTIVE LOOP PERSISTENCE ----
 * The rekordbox Active Loop is a flag on a MEMORY loop (CueProperty+0xe1 == 1; the firmware converts it to Cue+0x28 only for
 * memory cues, and ActiveLoopUpdater 0x9a2e40 scans the MemoryCueSet only). A hot cue can never carry it. So SHIFT+pad keeps
 * the Active Loop by storing a memory-kind copy of the loop with that flag, through the firmware's own store function
 * (0x86b410 = the call stock MEMORY makes) and removes it again through the firmware's own memory delete (0x86b3f8 = what
 * deleteCue's task calls). The mod writes no file: the firmware's cue write-back (database / ANLZ) does the persisting.
 * One flagged loop per track: the previous one is un-flagged first (delete + store without the flag).
 * Everything here runs in the tick. The jobs are run one per PJ_GAP ticks, in order. */
#define VA_STORE_DEL ((uintptr_t)0x86b3f8u)   /* CueStoreMaintainer: delete a MEMORY cue (store, const CueProperty&) */
typedef void (*store_del_fn)(void *store, const void *cue);
static store_del_fn fw_store_del;
#define PJ_DEL    1
#define PJ_STORE  2
#define NPJ       8
#define PJ_GAP    4                             /* ticks between two jobs (~130 ms) */
#define PV_DELAY  150                           /* ticks after the last job: read the ANLZ back (read-only) and log */
typedef struct {
    int op, d, must;                            /* must: second half of a pair, runs even if the track changed */
    uint32_t cid;
    uint8_t cue[CUE_SIZE] __attribute__((aligned(64)));
} pjob_t;
static pjob_t   pj[NPJ];
static int      pj_r, pj_cnt;
static long     pj_due;
static unsigned pj_nstore, pj_ndel;             /* counters for the log / tests */
static long     pv_due[NDECK];                  /* tick the read-back is due (0 = none) */
static int      pv_active[NDECK];
static int64_t  pv_in[NDECK], pv_out[NDECK];
static int      pv_want[NDECK];                 /* 1: a flagged loop at pv_in/pv_out is expected, 0: none expected there */
static int      pv_pre[NDECK];                  /* 0.2.10: memory loops the ANLZ must hold after our store (ownership check) */
static int      pv_idx[NDECK] = { -1, -1, -1, -1 };   /* 0.2.10: cache slot of the copy we stored (-1 none) */
_Static_assert(NDECK == 4, "pv_idx initialiser");

/* 0.2.10: the real CueStoreMaintainer is recognised by its own vtable (primary vtable = slot 0 at 0x2951330; 0x86b410 and 0x86b3f8
 * are plain members, NOT vtable slots: the 0.2.9 check looked for them there and refused the genuine object) or by its task queue
 * (*(store+0x150) is an object whose vtable slot 0 is 0x86ff40, the test 0x86b410 itself makes). */
#define VT_CSM    ((uintptr_t)0x2951330u)
#define VA_QUEUE0 ((uintptr_t)0x86ff40u)
static int store_vt_ok(uintptr_t store)
{
    uintptr_t vp = 0;
    if (!plausible(store) || (store & 7) || safe_read(store, &vp, sizeof vp) != 0) return 0;
    if (vp == VT_CSM) return 1;
    uintptr_t q = 0, qv = 0, q0 = 0;
    if (safe_read(store + 0x150, &q, sizeof q) != 0 || !plausible(q) || (q & 7)) return 0;
    if (safe_read(q, &qv, sizeof qv) != 0 || !plausible(qv)) return 0;
    return safe_read(qv, &q0, sizeof q0) == 0 && q0 == VA_QUEUE0;
}
/* the CueStoreMaintainer: seen as the first argument of 0x86b410 (any stock store), else found from the CueCallManager that
 * deleteCue ran on: *(mgr + 0x140) is the field the delete task passes to 0x86b3f8; if that field fails the check the manager's
 * first 0x400 bytes are scanned for exactly one pointer that passes it. NULL = not known yet. */
static void *store_ptr(void)
{
    if (!fw_store_del) return NULL;
    void *s = __atomic_load_n(&store_any, __ATOMIC_ACQUIRE);
    if (s) return s;
    void *m = __atomic_load_n(&mgr_any, __ATOMIC_ACQUIRE);
    if (!m || !plausible((uintptr_t)m)) return NULL;
    static int logged, failed;
    uintptr_t p = safe_ptr((uintptr_t)m + 0x140), how = 0x140;
    if (!p || !store_vt_ok(p)) {
        uintptr_t found = 0; int nfound = 0;
        for (uintptr_t off = 0; off < 0x400; off += 8) {
            uintptr_t c = safe_ptr((uintptr_t)m + off);
            if (c && store_vt_ok(c)) { if (c != found) nfound++; found = c; how = off; }
        }
        if (nfound != 1) {
            if (!failed) { failed = 1; LOG("store pointer: CueCallManager %p known, +0x140 = %p fails the check, scan found %d candidates", m, (void *)p, nfound); }
            return NULL;
        }
        p = found;
    }
    if (!logged) { logged = 1; LOG("store pointer %p taken from the CueCallManager %p (+%#lx, checked by its vtable/queue)", (void *)p, m, (unsigned long)how); }
    return (void *)p;
}
/* does a stored/unflagged loop of this deck belong to one pad's extent? (pad = firmware cue, memory entry = firmware or ANLZ-built) */
static int ext_loose(const void *a, int64_t in, int64_t out)
{
    return near_del(cue_in(a), in) && near_del(cue_out(a), out);
}
static int mem_loop_ok(int d, int i)
{
    return mem_ok[d][i] && (mem_live[d][i] || mem_syn[d][i]) && fw_is_loop(mem_buf[d][i]);
}
/* the memory loop carrying the flag (rekordbox's or ours); -1 none. A memory loop the firmware showed us live wins over a built one. */
static int flagged_idx(int d)
{
    int best = -1;
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_loop_ok(d, i) && mem_flag[d][i]) { if (best < 0 || (mem_live[d][i] && !mem_syn[d][i])) best = i; }
    return best;
}
/* the memory loop with the same extent as pad cue c: exact (near) among live ones, within 0.5 s for an ANLZ-built one */
static int mem_ident(int d, const void *c)
{
    int64_t in = cue_in(c), out = cue_out(c); int best = -1;
    for (int i = 0; i < mem_n[d]; i++) {
        if (!mem_loop_ok(d, i)) continue;
        if (mem_live[d][i] && !mem_syn[d][i]) { if (near(cue_in(mem_buf[d][i]), in) && near(cue_out(mem_buf[d][i]), out)) return i; }
        else if (near_del(cue_in(mem_buf[d][i]), in) && near_del(cue_out(mem_buf[d][i]), out)) best = i;
    }
    return best;
}
static int live_loops(int d)
{
    int n = 0;
    for (int i = 0; i < mem_n[d]; i++) n += mem_loop_ok(d, i);
    return n;
}
static void mem_drop(int d, int i)       /* forget a memory loop (the job queue deletes it in the firmware) */
{
    if (mem_ok[d][i]) { fw_cue_dtor(mem_buf[d][i]); mem_ok[d][i] = 0; }
    mem_live[d][i] = 0; mem_syn[d][i] = 0; mem_flag[d][i] = 0; mem_mine[d][i] = 0;
    if (last_mem[d] == i) last_mem[d] = -1;
}
static int pj_push(int op, int d, uint32_t cid, const void *src, int flag, int must)
{
    if (pj_cnt >= NPJ) return -1;
    pjob_t *j = &pj[(pj_r + pj_cnt) % NPJ];
    memset(j, 0, sizeof *j);
    j->op = op; j->d = d; j->cid = cid; j->must = must;
    fw_cue_copy(j->cue, src);
    if (flag >= 0) j->cue[CP_FLAG] = (uint8_t)flag;
    pj_cnt++;
    return 0;
}
static void pv_note(int d, int64_t in, int64_t out, int want)
{
    pv_in[d] = in; pv_out[d] = out; pv_want[d] = want; pv_due[d] = tick_no + PV_DELAY + PJ_GAP * (pj_cnt + 1);
}
/* tick: one job per PJ_GAP ticks */
static void pj_service(void)
{
    if (!pj_cnt || tick_no < pj_due) return;
    pjob_t *j = &pj[pj_r % NPJ];
    int d = j->d; char t[32]; fmt_time(t, sizeof t, cue_in(j->cue));
    void *store = store_ptr();
    if (!store) {
        if (j->op == PJ_STORE && j->must) { pj_due = tick_no + 30; return; }     /* never lose the second half: wait */
        LOG("deck %d: persist job (%s at %s) dropped: no store pointer", d + 1, j->op == PJ_STORE ? "store" : "delete", t);
    } else if (!j->must && deck_cid(d) != j->cid) {
        LOG("deck %d: persist job (%s at %s) dropped: another track is loaded now", d + 1, j->op == PJ_STORE ? "store" : "delete", t);
    } else if (j->op == PJ_DEL) {
        fw_store_del(store, j->cue); pj_ndel++;
        LOG("deck %d: persist: memory loop %s in=%lld out=%lld deleted through the firmware (0x86b3f8)", d + 1, t,
            (long long)cue_in(j->cue), (long long)cue_out(j->cue));
    } else {
        orig_store(store, 0, j->cue); pj_nstore++;
        LOG("deck %d: persist: memory loop %s in=%lld out=%lld stored through the firmware (0x86b410), Active Loop flag=%d", d + 1, t,
            (long long)cue_in(j->cue), (long long)cue_out(j->cue), j->cue[CP_FLAG]);
    }
    fw_cue_dtor(j->cue);
    pj_r = (pj_r + 1) % NPJ; pj_cnt--;
    pj_due = tick_no + PJ_GAP;
}
/* ---- 0.2.10: what the firmware does, and the rule the mod keeps ----
 * Read from the firmware (CacheableCueInfo::doRegisterCue 0x1890808, doDeregisterCue 0x1887870):
 *  - a store whose kind and in/out (compared in whole ms) equal a memory cue that exists is IGNORED, flag or not: there is no
 *    "set the flag in place" path, a same-extent store never changes the stored loop;
 *  - storing a FLAGGED memory loop DELETES every other flagged memory loop of the track (single Active Loop rule: the old one is
 *    deregistered, not un-flagged);
 *  - a delete removes the cue whose kind and in/out (whole ms) match, flagged or not;
 *  - at most 10 memory cues (points and loops together).
 * So the mod NEVER deletes a memory loop it did not itself store in this session and confirm in the ANLZ read-back (OWN_OK),
 * and it never stores a flagged loop while another flagged loop it does not own would be deleted by the firmware for it.
 * In those cases the loop is armed only and the toast says why. */
static uint32_t anlz_for[NDECK];     /* content id requested for this load (0 = not yet) */
static int owned(int d, int i) { return mem_ok[d][i] && mem_mine[d][i] == OWN_OK; }
/* the ANLZ of the loaded track is read and current: the loops/flags/cue count in anlz_res[d] can be trusted for this track */
static int anlz_ready(int d, uint32_t cid)
{
    return __atomic_load_n(&anlz_state[d], __ATOMIC_ACQUIRE) == 2u && anlz_res[d].cid == cid && anlz_for[d] == cid;
}
static int ms_near(uint32_t ms, int64_t samples) { return near_del((int64_t)(ms * 44.1), samples); }
/* ANLZ memory loop within DEL_TOL of [in,out] (-1 none); *flag gets its rekordbox flag */
static int anlz_twin(int d, int64_t in, int64_t out, int *flag)
{
    for (int i = 0; i < anlz_res[d].n; i++) {
        const al_t *e = &anlz_res[d].e[i];
        if (e->hot) continue;
        if (ms_near(e->in_ms, in) && ms_near(e->out_ms, out)) { if (flag) *flag = e->flag != 0; return i; }
    }
    return -1;
}
/* memory loops the ANLZ will hold: its own list, plus live ones (stock MEMORY since the last read) that it does not list yet */
static int mem_loops_expected(int d)
{
    int n = 0;
    for (int i = 0; i < anlz_res[d].n; i++) n += !anlz_res[d].e[i].hot;
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_loop_ok(d, i) && mem_live[d][i] && !mem_syn[d][i] && anlz_twin(d, cue_in(mem_buf[d][i]), cue_out(mem_buf[d][i]), NULL) < 0) n++;
    return n;
}
/* a flagged memory loop (cache or ANLZ) that is not ours-and-confirmed: the firmware would delete it for a new flag */
static int foreign_flag(int d, int64_t *in)
{
    for (int i = 0; i < mem_n[d]; i++)
        if (mem_loop_ok(d, i) && mem_flag[d][i] && !owned(d, i)) { if (in) *in = cue_in(mem_buf[d][i]); return 1; }
    for (int i = 0; i < anlz_res[d].n; i++) {
        const al_t *e = &anlz_res[d].e[i];
        if (e->hot || !e->flag) continue;
        int mine = 0;
        for (int k = 0; k < mem_n[d]; k++)
            if (owned(d, k) && mem_flag[d][k] && ms_near(e->in_ms, cue_in(mem_buf[d][k])) && ms_near(e->out_ms, cue_out(mem_buf[d][k]))) mine = 1;
        if (!mine) { if (in) *in = (int64_t)(e->in_ms * 44.1); return 1; }
    }
    return 0;
}
/* SHIFT+pad, first press: keep this pad's loop as the Active Loop. */
#define PS_OK 0
#define PS_NOSTORE 1
#define PS_FULL 2
#define PS_BUSY 3
#define PS_HAVE 4                       /* an Active Loop flag already sits on a memory loop of this extent: nothing to store */
#define PS_TWIN 5                       /* an UNflagged memory loop of this extent exists: the firmware would ignore a store */
#define PS_FOREIGN 6                    /* another flagged loop we do not own: the firmware would delete it */
#define PS_NOCHK 7                      /* the track's stored loops are not read yet: cannot check */
static int persist_set(int d, const uint8_t *c)
{
    uint32_t cid = load_id(d);               /* 0 while a load is landing: the cue lists are not this track's yet */
    if (!cid) return PS_BUSY;
    char t[32]; fmt_time(t, sizeof t, cue_in(c));
    if (!anlz_ready(d, cid)) { LOG("deck %d: Active Loop not saved: this track's stored loops are not read yet (cannot check for a same-extent loop or another flag)", d + 1); return PS_NOCHK; }
    int m = mem_ident(d, c), af = 0, at = anlz_twin(d, cue_in(c), cue_out(c), &af);
    if (m >= 0 || at >= 0) {
        if ((m >= 0 && mem_flag[d][m]) || (at >= 0 && af)) { LOG("deck %d: Active Loop at %s is already flagged on a stored memory loop: nothing to store", d + 1, t); return PS_HAVE; }
        LOG("deck %d: Active Loop at %s NOT saved: a memory loop with the same in/out already exists and the firmware ignores a same-extent store (it has no set-flag-in-place path); the mod never deletes a loop it did not store", d + 1, t);
        return PS_TWIN;
    }
    if (!store_ptr()) { LOG("deck %d: Active Loop not saved: no store pointer yet (a stock MEMORY press or any cue delete gives it)", d + 1); return PS_NOSTORE; }
    int64_t fin = 0;
    if (foreign_flag(d, &fin)) {
        char tf[32]; fmt_time(tf, sizeof tf, fin);
        LOG("deck %d: Active Loop at %s NOT saved: the track already has a flagged memory loop at %s that this mod did not store; the firmware deletes the old flagged loop when a new one is flagged, so nothing is stored", d + 1, t, tf);
        return PS_FOREIGN;
    }
    if (pj_cnt + 2 > NPJ) { LOG("deck %d: Active Loop not saved: earlier persist jobs still queued", d + 1); return PS_BUSY; }
    int fi = flagged_idx(d);                 /* ours and confirmed (foreign_flag was 0): the firmware removes it when the new flag is stored */
    int pts = anlz_res[d].memcues - 0; { int ml = 0; for (int i = 0; i < anlz_res[d].n; i++) ml += !anlz_res[d].e[i].hot; pts -= ml; if (pts < 0) pts = 0; }
    int est = mem_loops_expected(d) + pts - (fi >= 0 ? 1 : 0);
    if (est >= MEM_SOFT_CAP) {
        LOG("deck %d: Active Loop not saved: the memory list holds %d cues (%d loops + %d points, cap %d)", d + 1, est, est - pts, pts, MEM_SOFT_CAP);
        return PS_FULL;
    }
    pv_pre[d] = mem_loops_expected(d) + 1 - (fi >= 0 ? 1 : 0);
    pj_push(PJ_STORE, d, cid, c, 1, 0);
    int idx = mem_put(d, c);
    mem_buf[d][idx][CP_FLAG] = 1; mem_live[d][idx] = 1; mem_syn[d][idx] = 0; mem_flag[d][idx] = 1; mem_mine[d][idx] = OWN_PEND;
    if (fi >= 0 && fi != idx) {
        char to[32]; fmt_time(to, sizeof to, cue_in(mem_buf[d][fi]));
        LOG("deck %d: the firmware replaces our earlier saved loop at %s with this one", d + 1, to);
        mem_drop(d, fi);
    }
    LOG("deck %d: Active Loop: a flagged memory copy of the pad's loop at %s is stored (%d memory loops expected after the store)", d + 1, t, pv_pre[d]);
    pv_note(d, cue_in(c), cue_out(c), 1);
    pv_idx[d] = idx;
    return PS_OK;
}
/* SHIFT+pad again on the armed pad, or DELETE+pad: take OUR saved flag away. Only a copy this mod stored this session and
 * confirmed in the read-back is deleted; anything else stays (its flag too). */
#define PC_NONE 0                       /* no flagged loop of that extent */
#define PC_DONE 1                       /* our confirmed copy: delete queued */
#define PC_KEPT 2                       /* a flagged loop we do not own: left alone */
#define PC_FAIL 3                       /* ours, but it cannot be removed now (no store pointer, queue busy) */
static int persist_clear(int d, const uint8_t *c)
{
    int fi = flagged_idx(d);
    if (fi < 0 || !ext_loose(mem_buf[d][fi], cue_in(c), cue_out(c))) return PC_NONE;
    uint32_t cid = load_id(d);
    if (!cid) return PC_NONE;
    char t[32]; fmt_time(t, sizeof t, cue_in(mem_buf[d][fi]));
    if (!owned(d, fi)) {
        LOG("deck %d: the flagged memory loop at %s %s: left in place, flag included (the mod only deletes what it stored and confirmed this session)", d + 1, t,
            mem_mine[d][fi] == OWN_PEND ? "was stored a moment ago and is not confirmed yet" : "was not stored by this mod in this session");
        return PC_KEPT;
    }
    if (!store_ptr()) { LOG("deck %d: saved Active Loop at %s kept: no store pointer to delete it with", d + 1, t); return PC_FAIL; }
    if (pj_cnt + 1 > NPJ) { LOG("deck %d: saved Active Loop at %s kept: earlier persist jobs still queued", d + 1, t); return PC_FAIL; }
    int64_t in = cue_in(mem_buf[d][fi]), out = cue_out(mem_buf[d][fi]);
    pj_push(PJ_DEL, d, cid, mem_buf[d][fi], -1, 0);
    LOG("deck %d: Active Loop flag removed: our memory copy at %s is deleted", d + 1, t);
    mem_drop(d, fi);
    pv_note(d, in, out, 0); pv_idx[d] = -1;
    return PC_DONE;
}
/* a hot cue loop of deck d was deleted (sweep after a deleteCue): OUR flagged memory copy with the same extent goes with it,
 * unless another pad still holds that loop. Another flagged loop stays (logged). hot_buf[d][k] is still the deleted cue. */
static void persist_hot_deleted(int d, int k)
{
    if (!hot_ok[d][k]) return;
    int fi = flagged_idx(d);
    if (fi < 0 || !ext_loose(mem_buf[d][fi], cue_in(hot_buf[d][k]), cue_out(hot_buf[d][k]))) return;
    for (int j = 1; j <= 8; j++)
        if (j != k && hot_ok[d][j] && fw_is_loop(hot_buf[d][j]) && ext_loose(mem_buf[d][fi], cue_in(hot_buf[d][j]), cue_out(hot_buf[d][j]))) return;
    (void)persist_clear(d, hot_buf[d][k]);
}

/* ---- 0.2.8 tick side of the deleteCue observer ----
 * deleteCue(this, kind, cue): `this` is the ONE shared CueCallManager (a SharedTaskDrivenThread,
 * not one per deck) and the cue carries no player, so the hook cannot say which deck. Deck and
 * identity come from state the tick can read instead:
 *  - hot cue (kind 1..8): the deck whose LIVE hot-cue slot for that kind stopped being a loop
 *    while we had it cached as one (ground truth: only the deck that was deleted from changes,
 *    also when both decks hold the same track). The Active Loop is cleared only if it was armed
 *    from THAT hot cue (kind identity), never by extent alone.
 *  - memory cue (kind 0): the deck of the MEMORY/DELETE press, else the one deck that has a
 *    matching memory loop (nearest in/out within 0.5 s); two candidates = unknown = nothing done.
 *    The Active Loop is cleared only if it was armed from that memory loop (slot identity). */
static long hot_dirty_until;            /* tick: a hot cue delete was seen; sweep the live slots until then */

static void drop_hot(int d, int k)
{
    if (hot_ok[d][k]) { fw_cue_dtor(hot_buf[d][k]); hot_ok[d][k] = 0; }
}
/* Active Loop armed from hot cue `k` of deck d, which is gone: clear it (quietly if the row is off). */
static void hot_gone_active(int d, int k)
{
    if (!act_ok[d] || !act_src_hot[d] || act_src_idx[d] != k) return;
    if (deck_cid(d) != active_cid[d]) return;            /* another track is loaded: not this cue */
    if (!holder_of(d)) { fw_cue_dtor(act_buf[d]); act_ok[d] = 0; active_cid[d] = 0; spent[d] = 0; }
    else if (clear_active(d, "hot cue deleted") != 0) return;
    LOG("deck %d: the armed loop (hot cue %d) was deleted -> Active Loop cleared", d + 1, k);
    do_toast("Active Loop cleared (loop deleted)");
}
/* Compare each cached hot cue loop with its live slot. The slot read is a tick job. */
static long hot_chk[NDECK];
static void hot_refresh_deck(int d, int sweep)
{
    for (int k = 1; k <= 8; k++) {
        if (!hot_ok[d][k]) {                              /* learn a loop we were not told about (row was off) */
            void *l2 = live_hot(d, k);
            if (l2 && fw_is_loop(l2)) hot_put(d, k, l2);
            continue;
        }
        void *live = live_hot(d, k);
        if (!live) continue;                              /* unreadable: unknown, leave it */
        if (!fw_is_loop(live)) {
            LOGC("deck %d: hot cue %d is not a loop any more (live slot), dropped from the loop list", d + 1, k);
            if (sweep) persist_hot_deleted(d, k);          /* 0.2.9: DELETE+pad takes the flagged memory loop of that pad with it */
            drop_hot(d, k);
            if (sweep) hot_gone_active(d, k);
        } else if (!near(cue_in(live), cue_in(hot_buf[d][k])) || !near(cue_out(live), cue_out(hot_buf[d][k]))) {
            hot_put(d, k, live);
        }
    }
}
static void hot_refresh(int d)
{
    if (hot_chk[d] == tick_no) return;
    hot_chk[d] = tick_no;
    hot_refresh_deck(d, (unsigned long)tick_no <= (unsigned long)hot_dirty_until);
}
/* tick: while a delete is fresh, look at every deck's live slots each tick */
static void hot_sweep(void)
{
    if ((unsigned long)tick_no > (unsigned long)hot_dirty_until) return;
    for (int d = 0; d < NDECK; d++) { hot_chk[d] = tick_no; hot_refresh_deck(d, 1); }
}

static int raw_loop(int64_t in, int64_t out) { return out != INT64_MAX && out > in; }
/* the memory loop of deck d nearest to [in,out] within DEL_TOL (-1 none) */
static int mem_nearest(int d, int64_t in, int64_t out, int64_t *score)
{
    int bi = -1; int64_t bs = 0;
    for (int i = 0; i < mem_n[d]; i++) {
        if (!mem_ok[d][i] || !mem_live[d][i]) continue;
        int64_t di = cue_in(mem_buf[d][i]) - in, dout = cue_out(mem_buf[d][i]) - out;
        if (!near_del(cue_in(mem_buf[d][i]), in) || !near_del(cue_out(mem_buf[d][i]), out)) continue;
        int64_t sc = (di < 0 ? -di : di) + (dout < 0 ? -dout : dout);
        if (bi < 0 || sc < bs) { bi = i; bs = sc; }
    }
    if (score) *score = bs;
    return bi;
}
static void mem_gone(int d, int i)
{
    LOG("deck %d: memory loop #%d deleted, dropped from the loop list", d + 1, i);
    mem_live[d][i] = 0; mem_flag[d][i] = 0; mem_mine[d][i] = 0;   /* 0.2.9: a deleted flagged loop takes its flag with it */
    if (act_ok[d] && !act_src_hot[d] && act_src_idx[d] == i && deck_cid(d) == active_cid[d]) {
        if (!holder_of(d)) { fw_cue_dtor(act_buf[d]); act_ok[d] = 0; active_cid[d] = 0; spent[d] = 0; }
        else if (clear_active(d, "memory cue deleted") != 0) return;
        LOG("deck %d: the armed loop (memory #%d) was deleted -> Active Loop cleared", d + 1, i);
        do_toast("Active Loop cleared (loop deleted)");
    }
}
static void on_delete_ev(ev_t *e)
{
    if (!row_on()) return;                               /* row off: silent */
    if (!e->draw || e->kind < 0 || e->kind > 8) return;
    int loop = raw_loop(e->din, e->dout);
    LOGC("deleteCue seen: kind %d loop=%d at in=%lld out=%lld", e->kind, loop, (long long)e->din,
        (long long)(e->dout == INT64_MAX ? -1 : e->dout));
    if (e->kind >= 1) {                                  /* hot cue: the live slots say which deck */
        hot_dirty_until = tick_no + 8;
        hot_sweep();
        return;
    }
    if (!loop) return;                                   /* a memory point cue: nothing of ours */
    int d = press_deck_for(e->t_ns);
    if (d < 0) {                                         /* no press: the one deck that has this loop */
        int hit = -1, n = 0;
        for (int dd = 0; dd < NDECK; dd++)
            if (mem_nearest(dd, e->din, e->dout, NULL) >= 0) { hit = dd; n++; }
        if (n != 1) {
            LOG("deleteCue kind 0: %s deck, nothing changed", n ? "ambiguous" : "unknown");
            return;
        }
        d = hit;
    }
    int64_t sc = 0;
    int i = mem_nearest(d, e->din, e->dout, &sc);
    if (i < 0) return;
    mem_gone(d, i);
}

/* SHIFT+pad (the filter ate the pad): a loop pad becomes the Active Loop; anything else is handed
 * back to stock by replaying the SHIFT+pad (FS.req), so a point cue is deleted as it always was. */
/* a hot-cue slot with no cue: its Position is not valid (the firmware's own is_loop / valid_loop test the same fields). A loop
 * or a point cue has a valid Position. Tick-only, like every slot read. */
static int slot_empty(const void *live)
{
    return !fw_is_loop((void *)live) && !pos_valid((const uint8_t *)live + 0x28);
}
/* the pad mode of deck d is KNOWN to be BEAT LOOP or SLIP LOOP: then the pads are not hot cue pads (the core reports HOT CUE, BEAT JUMP
 * and sampler as unknown, -1), and what stock does with SHIFT+pad / DELETE+pad there is not ours to swallow. Tick only. */
static int pads_not_hotcue(int d)
{
    az_deck_info di; memset(&di, 0, sizeof di); di.size = sizeof di;
    if (!C || !AZ_HAS(C, deck_info) || C->deck_info(d, &di) != 0) return 0;
    return di.pad_mode == 1 || di.pad_mode == 2;
}
/* tick: while DELETE is held on a side, publish which of that deck's pads are EMPTY, so the filter can swallow DELETE+empty pad
 * (a rewrite to SHIFT+pad would make stock STORE a cue there). 0 = unknown (the old behaviour) for a side whose DELETE is up. */
static void emp_refresh(void)
{
    uint32_t emp = 0;
    for (int s = 0; s < 2; s++) {
        if (!__atomic_load_n(&FS.dh[s], __ATOMIC_RELAXED)) continue;
        int d = side_to_deck(s);
        if (pads_not_hotcue(d)) continue;
        for (int k = 1; k <= 8; k++) {
            void *l = live_hot(d, k);
            if (l && slot_empty(l)) emp |= 1u << (8 * s + k - 1);
        }
    }
    if (emp != __atomic_load_n(&FS.emp, __ATOMIC_RELAXED)) __atomic_store_n(&FS.emp, emp, __ATOMIC_RELAXED);
}
/* 0.2.9: the pad colour. A pad is RED while its loop is the Active Loop: the armed one, else the flagged memory loop of the track
 * (what was saved). The tick publishes one bit per pad and side; on_led (the core's LED-frame callback, the deck I/O path) paints
 * those pads and nothing else: no lock, no call, no state of its own. Stock rewrites the whole frame every cycle, so when the bit
 * goes away the pad has its own colour again at the very next frame. */
static int red_extent(int d, int64_t *in, int64_t *out)
{
    if (act_ok[d] && !spent[d]) { *in = active_in[d]; *out = active_out[d]; return 2; }    /* 2 = ARMED (0.2.11: it breathes) */
    int i = flagged_idx(d);
    if (i >= 0) { *in = cue_in(mem_buf[d][i]); *out = cue_out(mem_buf[d][i]); return 1; }
    return 0;
}
static uint32_t led_pub[2];                 /* atomic: bit k-1 = pad k of that side is red */
static uint32_t led_arm[2];                 /* atomic, 0.2.11: the subset of led_pub that is the ARMED Active Loop (breathes) */
static uint32_t led_lvl = 255;              /* atomic, 0.2.11: the red byte of a breathing pad, published by the tick */
static void led_refresh(void)
{
    for (int s = 0; s < 2; s++) {
        int d = side_to_deck(s); uint32_t m = 0, am = 0; int64_t in, out; int re;
        if (holder_of(d) && (re = red_extent(d, &in, &out)) != 0)
            for (int k = 1; k <= 8; k++)
                if (hot_ok[d][k] && fw_is_loop(hot_buf[d][k]) && ext_loose(hot_buf[d][k], in, out)) {
                    m |= 1u << (k - 1);
                    if (re == 2) am |= 1u << (k - 1);
                }
        if (am != __atomic_load_n(&led_arm[s], __ATOMIC_RELAXED)) __atomic_store_n(&led_arm[s], am, __ATOMIC_RELAXED);
        if (m != __atomic_load_n(&led_pub[s], __ATOMIC_RELAXED)) {
            __atomic_store_n(&led_pub[s], m, __ATOMIC_RELAXED);
            LOGC("deck %d (side %d): red pads mask %02x", d + 1, s + 1, m);
        }
    }
}
/* 0.2.12: the breath. Tick only (UI thread). Phase from CLOCK_MONOTONIC (the tick rate varies: never count ticks), raised cosine
 * w = 0.5 - 0.5 cos(2 pi phase) (0 at phase 0 = OFF, 1 at half a cycle = full red). PERCEIVED level p = w^0.6: the exponent lifts the
 * bottom so the fade leaves black quickly and the pad does not sit dark (a plain cosine would be byte 0 for ~16% of the cycle; this
 * is ~6%), while the top stays flat and smooth. LED byte = p^2.2 (the frame is linear light, so equal steps of p look like equal
 * steps of brightness). The floor is 0: the red byte reaches 0 and the cell is 00 00 00 there. Pulse row off: constant 255 = the
 * 0.2.10 solid red. Levels by tenth of the cycle: 0 11 63 146 223 255 223 146 63 11 0. */
#define BREATH_PERIOD_NS 4000000000ull
#define BREATH_SHAPE     0.6f
static void led_level_update(uint32_t cf)
{
    uint32_t lv = 255;
    if (cf & CFG_PULSE) {
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        uint64_t t = ((uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec) % BREATH_PERIOD_NS;
        float ph = (float)t / (float)BREATH_PERIOD_NS;
        float w = 0.5f - 0.5f * cosf(6.2831853f * ph);
        float p = powf(w, BREATH_SHAPE);
        int v = (int)(powf(p, 2.2f) * 255.0f + 0.5f);
        lv = (uint32_t)(v < 0 ? 0 : v > 255 ? 255 : v);
    }
    if (lv != __atomic_load_n(&led_lvl, __ATOMIC_RELAXED)) __atomic_store_n(&led_lvl, lv, __ATOMIC_RELAXED);
}
/* LED frame (128 bytes, CRC restamped by the core): side s at +s*0x38, pad k (0..7) = 4-byte cell {R,G,B,0} at 0x1e + 4k, pad-mode
 * buttons at 0x0e + 4j (j 0 = HOT CUE). Linear light: red is FF0000. Only while HOT CUE is the lit mode button (full brightness,
 * not the lime secondary page), only a pad that is lit already (dark stays dark), at the cell's own brightness (full, else the
 * stock 12/255 envelope). */
#define LED_PAD_OFF  0x1e
#define LED_MODE_OFF 0x0e
#define LED_SIDE     0x38
static uint32_t led_sig[2];                 /* on_led's own (I/O thread only): what was last logged per side */
static unsigned led_dbg_n;
static int on_led(int bus, unsigned char *f, int len)
{
    if (bus != AZ_BUS_ERP_LED || !f || len < 0x78) return AZ_FRAME_OK;
    for (int s = 0; s < 2; s++) {
        uint32_t m = __atomic_load_n(&led_pub[s], __ATOMIC_RELAXED);
        if (!m) { led_sig[s] = 0; continue; }
        const uint32_t am = __atomic_load_n(&led_arm[s], __ATOMIC_RELAXED);      /* 0.2.11: armed pads breathe */
        const unsigned char lvl = (unsigned char)__atomic_load_n(&led_lvl, __ATOMIC_RELAXED);
        const unsigned char *mb = f + LED_MODE_OFF + s * LED_SIDE;
        unsigned mx = mb[0] > mb[1] ? mb[0] : mb[1]; if (mb[2] > mx) mx = mb[2];
        int lime = mb[1] >= mb[0] && mb[2] == 0 && mb[0] > 0 && mb[0] < mb[1];
        int gate = !(mx < 0x40 || lime);                       /* HOT CUE is the live mode on this side */
        int first = -1; unsigned char was[3] = { 0, 0, 0 };
        if (gate) {
            for (int k = 0; k < 8; k++) {
                if (!((m >> k) & 1u)) continue;
                unsigned char *c = f + LED_PAD_OFF + 4 * k + s * LED_SIDE;
                unsigned cm = c[0] > c[1] ? c[0] : c[1]; if (c[2] > cm) cm = c[2];
                if (!cm) continue;
                if (first < 0) { first = k; was[0] = c[0]; was[1] = c[1]; was[2] = c[2]; }
                c[0] = cm >= 0x40 ? (((am >> k) & 1u) ? lvl : 0xff) : 12; c[1] = 0; c[2] = 0;
            }
        }
        /* evidence for the emulator run (it has no LEDs): one queued line per change of what is painted (at most 24 per session) */
        uint32_t sig = (m << 2) | (uint32_t)gate << 1 | (first >= 0);
        if (sig != led_sig[s] && led_dbg_n < 24) {
            led_sig[s] = sig; led_dbg_n++;
            if (first >= 0) {
                const unsigned char *c = f + LED_PAD_OFF + 4 * first + s * LED_SIDE;
                LOG("led side %d: red pads %02x, HOT CUE cell %02x%02x%02x, pad %d cell (RGB) %02x%02x%02x -> %02x%02x%02x", s + 1, m,
                    mb[0], mb[1], mb[2], first + 1, was[0], was[1], was[2], c[0], c[1], c[2]);
            } else
                LOG("led side %d: red pads %02x wanted, nothing painted: %s (HOT CUE cell %02x%02x%02x)", s + 1, m,
                    gate ? "the pad cells are dark" : "HOT CUE is not the lit mode", mb[0], mb[1], mb[2]);
        }
    }
    return AZ_FRAME_OK;
}
/* SHIFT+pad on the armed pad: the Active Loop is off (cleared); the normal hot cue loop stays. Our own saved copy is deleted; a
 * flagged loop we do not own stays, flag included, and the toast says so. If our copy cannot be removed now the loop stays ARMED
 * (never a flag left behind with the pad disarmed). */
static void al_off(int d, int kind)
{
    const uint8_t *c = hot_buf[d][kind];
    char t[32]; fmt_time(t, sizeof t, cue_in(c));
    int pc = persist_clear(d, c);
    if (pc == PC_FAIL) {
        LOG("deck %d: SHIFT+pad on the armed pad (hot cue %d at %s): saved copy cannot be removed now, Active Loop stays on", d + 1, kind, t);
        do_toast("Active Loop stays on: saved copy not removable now (press MEMORY once, try again)");
        return;
    }
    if (act_ok[d] && clear_active(d, "SHIFT+pad toggle off") != 0) return;     /* busy: press again */
    LOG("deck %d: SHIFT+pad on the armed pad (hot cue %d at %s): Active Loop off%s", d + 1, kind, t,
        pc == PC_DONE ? ", saved flag removed" : pc == PC_KEPT ? ", the saved flag of another loop stays" : " (nothing saved for it)");
    if (pc == PC_KEPT) toastf("Active Loop off at %s (its saved flag stays in the track)", t);
    else toastf("Active Loop off: hot cue at %s", t);
}
static void apply_pad(int s, int d, int kind)
{
    if (kind < 1 || kind > 8) { LOG("pad abort: bad kind %d", kind); return; }
    void *live = live_hot(d, kind);
    if (live && fw_is_loop(live)) hot_put(d, kind, live);
    else if (live && hot_ok[d][kind]) { fw_cue_dtor(hot_buf[d][kind]); hot_ok[d][kind] = 0; }
    if (hot_ok[d][kind] && fw_is_loop(hot_buf[d][kind])) {
        if (!holder_of(d)) { LOG("pad abort: deck %d not bound", d + 1); do_toast("Active Loop: deck not ready"); return; }
        if (act_ok[d] && !spent[d] && ext_loose(hot_buf[d][kind], active_in[d], active_out[d])) { al_off(d, kind); return; }   /* the pad's loop is armed: second press */
        pick_quiet = 1;
        int pr = pick_and_set(d, hot_buf[d][kind], 1, kind, "SHIFT+pad hot cue");
        pick_quiet = 0;
        if (pr != 0) return;
        char t[32]; fmt_time(t, sizeof t, cue_in(hot_buf[d][kind]));
        int ps = persist_set(d, hot_buf[d][kind]);
        toastf(ps == PS_OK ? "Active Loop saved: hot cue at %s" :
               ps == PS_HAVE ? "Active Loop armed at %s (already saved)" :
               ps == PS_NOSTORE ? "Active Loop armed at %s (not saved yet)" :
               ps == PS_FULL ? "Active Loop armed at %s, NOT saved: memory list full" :
               ps == PS_TWIN ? "Active Loop armed at %s, NOT saved: same loop already in memory" :
               ps == PS_FOREIGN ? "Active Loop armed at %s, NOT saved: another loop is saved as Active" :
               ps == PS_NOCHK ? "Active Loop armed at %s, NOT saved yet (reading the track)" :
               "Active Loop armed at %s (not saved now)", t);
        return;
    }
    if (!live) { LOG("pad: deck %d hot cue %d unreadable, left alone", d + 1, kind); do_toast("Active Loop: deck not ready"); return; }
    if (slot_empty(live) && !pads_not_hotcue(d)) {
        /* 0.2.8 al028d: stock stores a hot cue when SHIFT+pad is replayed on an EMPTY pad. SHIFT+empty pad must do nothing. */
        const uint8_t *P = (const uint8_t *)live + 0x28;
        LOG("pad: deck %d hot cue %d is EMPTY (position invalid: key %02x%02x%02x%02x, sample %lld): SHIFT+pad swallowed, nothing stored", d + 1, kind,
            P[8], P[12], P[14], P[16], (long long)*(const int64_t *)(P + 24));
        return;
    }
    {   /* al028f: the same raw Position words as the EMPTY line, so the two can be compared */
        const uint8_t *P = (const uint8_t *)live + 0x28;
        LOG("pad: deck %d hot cue %d is a point cue (position valid: key %02x%02x%02x%02x, sample %lld, in=%lld), SHIFT+pad passed to stock so it is deleted", d + 1, kind,
            P[8], P[12], P[14], P[16], (long long)*(const int64_t *)(P + 24), (long long)cue_in(live));
    }
    __atomic_store_n(&FS.req, RQ_VALID | ((uint32_t)(s & 1) << 9) | (1u << (kind - 1)), __ATOMIC_RELAXED);
}

static void *live_hot(int d, int kind)
{
    void *ls = ls_of[d];
    uint64_t probe;
    if (!plausible((uintptr_t)ls) || !fw_loop_slot || safe_read((uintptr_t)ls, &probe, 8)) return NULL;
    void *c = fw_loop_slot(ls, kind);
    return plausible((uintptr_t)c) ? c : NULL;
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
                LOGC("deck %d: playhead inside the Active Loop %s-%s", d + 1, ti, to);
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
    uint32_t b = CFG_ENABLE | CFG_HOTCUE | CFG_CLEAR | CFG_TOAST | CFG_REARM | CFG_AUTOADV | CFG_PULSE;
    if (AZ_HAS(C, row_value)) {
        if (h_enable >= 0 && C->row_value(h_enable) != 1) b &= ~CFG_ENABLE;
        if (h_autoadv >= 0 && C->row_value(h_autoadv) != 1) b &= ~CFG_AUTOADV;
        if (h_rearm >= 0 && C->row_value(h_rearm) != 1) b &= ~CFG_REARM;
        if (h_pulse >= 0 && C->row_value(h_pulse) != 1) b &= ~CFG_PULSE;
    }
    b |= ((uint32_t)COMBO_SHIFT_MEMORY & 3u) << 1;
    __atomic_store_n(&cfg_bits, b, __ATOMIC_RELAXED);
}

/* tick: hand ANLZ loops to the cache. Memory loops not seen live yet get a CueProperty built
 * from a constructed loop cue of the same track (firmware copy-ctor, so its Position source
 * info and refcounts are the firmware's own), with only the in/out sample positions changed.
 * Units: calibrated per track from a hot cue loop that is in both lists (cue units / ms),
 * else from the playhead hook vs deck_info seconds, else 44.1 per ms. */
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
/* ---- read-back (read-only): the ANLZ of the track, re-read some seconds after the last job, is logged against what was stored ---- */
static void pv_report(int d, const anlz_res_t *r)
{
    int mem = 0, fl = 0, hit = 0;
    for (int i = 0; i < r->n; i++) {
        const al_t *e = &r->e[i];
        if (e->hot) continue;
        mem++;
        if (e->flag) {
            fl++;
            double want_in = (double)pv_in[d] / 44.1;
            if (e->in_ms > want_in - 60.0 && e->in_ms < want_in + 60.0) hit++;
            char ti[32], to[32]; fmt_time(ti, sizeof ti, (int64_t)(e->in_ms * 44.1)); fmt_time(to, sizeof to, (int64_t)(e->out_ms * 44.1));
            LOG("  persist check deck %d: ANLZ flagged memory loop %s-%s (in %.0f ms, out %.0f ms)", d + 1, ti, to, (double)e->in_ms, (double)e->out_ms);
        }
    }
    char t[32]; fmt_time(t, sizeof t, pv_in[d]);
    int ok = pv_want[d] ? hit > 0 : hit == 0;
    LOG("deck %d: persist check: ANLZ has %d memory loops (%d cues in all), %d flagged; expected %s at %s: %s (cache: %d memory loops)", d + 1, mem, r->memcues, fl,
        pv_want[d] ? "a flagged loop" : "no flag", t, ok ? "OK" : "MISMATCH (the firmware has not written it, or wrote it differently)", live_loops(d));
    /* 0.2.10 ownership: our copy becomes deletable only when the file shows exactly what a stored copy leaves: one flagged loop, there,
     * and the loop count we expected. Anything else: the mod never deletes it. */
    int i = pv_idx[d]; pv_idx[d] = -1;
    if (pv_want[d] && i >= 0 && i < NMEM && mem_ok[d][i] && mem_mine[d][i] == OWN_PEND && ext_loose(mem_buf[d][i], pv_in[d], pv_out[d])) {
        if (hit == 1 && fl == 1 && mem == pv_pre[d]) {
            mem_mine[d][i] = OWN_OK;
            LOG("deck %d: our saved copy at %s is confirmed in the stored data (one flagged loop, %d memory loops as expected): it may be deleted again by this mod", d + 1, t, mem);
        } else if (hit == 0) {
            LOG("deck %d: the firmware did not store the flagged copy at %s: forgotten, nothing will be deleted", d + 1, t);
            mem_drop(d, i);
            do_toast("Active Loop: NOT saved (the firmware did not store it)");
        } else {
            mem_mine[d][i] = OWN_NONE;
            LOG("deck %d: not sure the flagged loop at %s is our copy (%d flagged, %d memory loops, expected %d): the mod will not delete it", d + 1, t, fl, mem, pv_pre[d]);
        }
    }
}
static void pv_service(void)
{
    for (int d = 0; d < NDECK; d++) {
        if (!pv_due[d] || tick_no < pv_due[d] || pj_cnt) continue;
        pv_due[d] = 0;
        if (!deck_cid(d)) continue;
        pv_active[d] = 1; anlz_for[d] = 0;               /* anlz_service re-reads it and calls pv_report, without merging */
    }
}
static void anlz_service(void)
{
    if (tick_no % 15) return;
    for (int d = 0; d < NDECK; d++) {
        uint32_t cid = load_id(d);                      /* al028g: never the previous track's id while the load is landing */
        if (!cid || !holder_of(d)) continue;
        if (anlz_for[d] != cid) {                       /* new load (or first sight): ask the worker */
            anlz_for[d] = cid; anlz_merged[d] = 0; anlz_wait[d] = 0; anlz_parsed_logged[d] = 0;
            anlz_request(d, cid);
            LOG("deck %d: reading stored loops for content id %u from the stick (read-only)", d + 1, cid);
        }
        if (anlz_merged[d]) continue;
        uint32_t st = __atomic_load_n(&anlz_state[d], __ATOMIC_ACQUIRE);
        if (st == 3) {
            anlz_merged[d] = 1;
            if (pv_active[d]) { pv_active[d] = 0; LOG("deck %d: persist check: ANLZ not readable: %s", d + 1, anlz_res[d].msg); continue; }
            LOG("deck %d: stored loops not read: %s (loops known once called/stored)", d + 1, anlz_res[d].msg); auto_arm(d, cid); continue;
        }
        if (st != 2 || anlz_res[d].cid != cid) continue;
        if (pv_active[d]) { pv_active[d] = 0; anlz_merged[d] = 1; pv_report(d, &anlz_res[d]); continue; }   /* 0.2.9: read-back only, no merge */
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

/* 0.2.1: own timing (filter, hooks), in us. 0.2.8: every 30 s (the 10 s lines were more than half of the
 * log, and the core moves a log over 128 KB to .log.1 at the next start), and the filter figures cover
 * EVERY call: calls over 100 us / over 200 us (the core's budget; each one can be a strike), the longest
 * call, and the gaps between calls (frame period + any stall of the bus thread). */
static void timing_log(void)
{
    static uint64_t last; static uint32_t fq; static uint32_t smax_f, smax_e, smax_ea, smax_c, smax_g;
    static unsigned tot_o2, tot_go;
    uint64_t now = now_ns();
    if (!fq) { uint64_t f = cnt_frq(); fq = f ? (uint32_t)f : 1000000000u; }
    if (!last) { last = now; return; }
    if (now - last < 30000000000ull) return;
    last = now;
    uint32_t f = take0(&FS.fmax);
    uint32_t fo = take0(&FS.fover);
    uint32_t fo2 = take0(&FS.fover2);
    uint32_t g = take0(&FS.gmax);
    uint32_t go = take0(&FS.gover);
    uint32_t ne = take0(&FS.nedge);
    static uint64_t st_last; uint64_t st_now = __atomic_load_n(&FS.stamp, __ATOMIC_RELAXED);
    double nfr = (double)((((st_now >> 40) - (st_last >> 40)) & 0xffffffull) * 256u); st_last = st_now;
    uint32_t e = take0(&hmax_ec_own);
    uint32_t ea = take0(&hmax_ec_all);
    uint32_t c = take0(&hmax_cue);
    uint32_t er = take0(&hmax_ec_rare);
    if (f > smax_f) smax_f = f;
    if (g > smax_g) smax_g = g;
    if (e > smax_e) smax_e = e;
    if (ea > smax_ea) smax_ea = ea;
    if (c > smax_c) smax_c = c;
    tot_o2 += fo2; tot_go += go;
#define US(x) ((double)(x) * 1e6 / (double)fq)
    LOG("timing 30s (us, own code, cntvct): filter, every call: max %.1f (session %.1f), >100us %u, >200us %u (session %u); "
        "frame gap max %.0f (session %.0f), typical interval %.0f, stalls (gap > 3x typical) %u (session %u); frames %.0f, edge frames %u; "
        "engage hot path max %.1f (session %.1f) w/fw %.1f rare %.1f; cue hooks max %.1f (session %.1f)",
        US(f), US(smax_f), fo, fo2, tot_o2, US(g), US(smax_g), US(__atomic_load_n(&FS.fi, __ATOMIC_RELAXED)), go, tot_go, nfr, ne, US(e), US(smax_e), US(ea), US(er), US(c), US(smax_c));
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
    /* 0.2.8: the main row says it once, from the tick that acts on it (row_went_off / row_went_on);
     * the two sub-rows log here as before, but not while the main row is off */
    if (strcmp((const char *)user, "Active Loop") != 0 && row_on())      /* with the main row off: silent */
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
    stash_active(d);                       /* al028f: FIRST: nothing of the old load stays armed (see stash_active) */
    ready_drop_others(d);
    autoarm_done[d] = 0;
    __atomic_store_n(&src_ok[d], 0u, __ATOMIC_RELEASE);
    if (base_ok[d]) { fw_cue_dtor(base_buf[d]); base_ok[d] = 0; }
    tk_kok[d] = 0;
    if (__atomic_load_n(&snap_state[d], __ATOMIC_ACQUIRE) == 2) { fw_cue_dtor(snap_buf[d]); }
    __atomic_store_n(&snap_state[d], 0u, __ATOMIC_RELEASE);
    anlz_for[d] = 0;                       /* re-read the stored loops for this load */
    pv_active[d] = 0; pv_due[d] = 0; pv_idx[d] = -1;       /* 0.2.9: a read-back of the old load must never eat this load's ANLZ merge */
    spent[d] = 0; ex_inside[d] = 0; ex_past[d] = 0;
    dg_near[d] = 0; dg_stall[d] = 0; dg_pos[d] = -1; dg_ns[d] = 0;
    last_call_ok[d] = 0; act_src_hot[d] = 0; act_src_idx[d] = 0; hot_chk[d] = 0;
    restore_pending[d] = rem_ok[d] && rem_cid[d] != 0;      /* only a remembered loop of the previous load can come back */
    restore_wait[d] = 0;
}
/* 0.2.8 al028f: the three paths that see a load (the core's deck event, the load cues of the hooks, the content-id poll)
 * report ONE load, so only the first of them within the load's window runs track_changed (and logs it). A repeat is
 * recognised by its content id (same id, or the first call came before the id was known) inside a window of 2 s (5 s while
 * the id is still unknown). A different id, or a call after the window, is a new load. Ejects and a row switch reset it. */
#define LOAD_DUP_TICKS      120
#define LOAD_DUP_TICKS_UNK  300
static int load_changed(int d, const char *why)
{
    uint32_t cid = load_id(d);
    if (tc_seen[d]) {
        long win = tc_cid[d] ? LOAD_DUP_TICKS : LOAD_DUP_TICKS_UNK;
        if (tick_no - tc_tick[d] < win && (!cid || !tc_cid[d] || cid == tc_cid[d])) {
            if (cid && !tc_cid[d]) { tc_cid[d] = cid; tc_tick[d] = tick_no; }
            return 0;                       /* the same load, already handled */
        }
    }
    tc_seen[d] = 1; tc_tick[d] = tick_no; tc_cid[d] = cid;
    track_changed(d, why);
    return 1;
}
/* tick: the content id of a load handled before it was known; and the "track loaded" lines that waited for it */
static void load_service(void);
static void load_cue_seen(int d)
{
    int first = !last_load_tick[d] || tick_no - last_load_tick[d] > 60;
    last_load_tick[d] = tick_no;                        /* before load_changed: load_id() must see this load cue */
    if (first && load_changed(d, "load events")) log_load(d, "seen from the load cues");
}
static void poll_cids(void)
{
    if (++cid_poll % 30) return;
    for (int d = 0; d < NDECK; d++) {
        uint32_t c = deck_cid(d);
        if (c && seen_cid[d] && c != seen_cid[d]) { if (load_changed(d, "content id changed")) log_load(d, "content id changed"); }
        if (c) seen_cid[d] = c;
    }
}
/* One "track loaded" line per deck per 2 s (the SDK's log-quiet gate; a load every 2 s is not a human), whichever path saw the
 * load first. 0.2.8 al028d: the core's deck event does NOT fire for a deck's first known state after the core started (that is
 * its baseline) nor for the same track loaded again (no content id change), so a load was sometimes not logged although the mod
 * handled it (the hook path and the content-id poll call track_changed too). Every path logs through here now.
 * al028f: the load cues run BEFORE the deck reports the track, so the first line used to say "content id 0". The line now waits
 * (load_service) until the content id is known; after 3 s without one it is logged as "not known yet". */
static uint64_t ld_ns[NDECK]; static unsigned ld_skip[NDECK];
static void log_load_now(int d, const char *via)
{
    uint64_t tn = now_ns();
    if (ld_ns[d] && tn - ld_ns[d] < 2000000000ull) { ld_skip[d]++; return; }
    ld_ns[d] = tn;
    uint32_t cid = load_id(d);
    if (cid) LOG("deck %d: track loaded (content id %u; %s)%s", d + 1, cid, via, ld_skip[d] ? " [earlier loads within 2 s not logged]" : "");
    else     LOG("deck %d: track loaded (content id not known yet; %s)%s", d + 1, via, ld_skip[d] ? " [earlier loads within 2 s not logged]" : "");
    ld_skip[d] = 0;
}
static void log_load(int d, const char *via)
{
    if (!load_id(d)) {                                  /* wait for the id */
        if (!ld_pend[d]) { ld_pend[d] = via; ld_pend_tick[d] = tick_no; }
        return;
    }
    ld_pend[d] = NULL;
    log_load_now(d, via);
}
static void load_service(void)
{
    for (int d = 0; d < NDECK; d++) {
        if (!tc_seen[d] && !ld_pend[d]) continue;
        uint32_t cid = load_id(d);
        if (tc_seen[d] && !tc_cid[d] && cid) { tc_cid[d] = cid; tc_tick[d] = tick_no; }   /* the id of a load handled before it was known */
        if (ld_pend[d] && (cid || tick_no - ld_pend_tick[d] > 180)) {
            const char *via = ld_pend[d]; ld_pend[d] = NULL;
            log_load_now(d, via);
        }
    }
}
static void deck_events(void)
{
    uint32_t ev = __atomic_exchange_n(&deck_evt, 0, __ATOMIC_RELAXED);
    for (int d = 0; d < NDECK; d++) {
        int loaded = (ev >> d) & 1, ejected = (ev >> (d + 4)) & 1;
        if (!loaded && !ejected) continue;
        if (loaded) {
            if (load_changed(d, "deck event")) log_load(d, "core deck event");
        } else {
            mem_reset(d); restore_pending[d] = 0; __atomic_add_fetch(&n_eject, 1, __ATOMIC_RELAXED);   /* ejects are counted, not logged */
            stash_active(d);                      /* al028f: an armed loop never stays armed on an empty deck; a reload of the same track may restore it */
            tc_seen[d] = 0; ld_pend[d] = NULL; anlz_for[d] = 0; pv_active[d] = 0; pv_due[d] = 0; pv_idx[d] = -1;
        }
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
        if (!rem_ok[d]) { restore_pending[d] = 0; continue; }
        uint32_t cid = deck_cid(d);
        if (!cid) { if (++restore_wait[d] > 60 * 30) { restore_pending[d] = 0; drop_rem(d); LOG("restore deck %d: gave up (no content id)", d + 1); } continue; }
        if (cid != rem_cid[d]) {                          /* another track: its loop is not this one's, it is forgotten */
            restore_pending[d] = 0;
            LOG("restore deck %d: other track (%u, was %u), nothing to restore", d + 1, cid, rem_cid[d]);
            drop_rem(d);
            continue;
        }
        int i = mem_find_live(d, rem_buf[d]);
        if (i < 0) {
            if (++restore_wait[d] == 1)
                LOG("restore deck %d: waiting until memory loop in=%lld out=%lld is seen live (CALL it)", d + 1,
                    (long long)rem_in[d], (long long)rem_out[d]);
            continue;
        }
        restore_pending[d] = 0;
        set_active_mem(d, i, "restore");
        LOG("restore deck %d: same track %u, live memory loop in=%lld out=%lld re-applied", d + 1, cid,
            (long long)rem_in[d], (long long)rem_out[d]);
        drop_rem(d);
    }
}
/* al028f: the invariant behind all of the above, checked every tick: an armed Active Loop belongs to the content id that was
 * loaded when it was armed. If the deck now reports another id (a load none of the paths reported), it is dropped and the
 * firmware's copy cleared, so it can never engage on the new track. Also issues the firmware clear that a track change owes. */
static void loop_guard(void)
{
    for (int d = 0; d < NDECK; d++) {
        if (act_ok[d]) {
            uint32_t cid = deck_cid(d);
            if (cid && active_cid[d] && cid != active_cid[d]) {
                LOG("deck %d: the armed Active Loop belongs to content id %u, but content id %u is loaded: dropped", d + 1, active_cid[d], cid);
                stash_active(d); drop_rem(d); restore_pending[d] = 0;
            }
        }
        if (clear_owed[d]) {                              /* what a track change owes the firmware */
            if (!holder_of(d)) clear_owed[d] = 0;
            else if (request(d, OP_CLEAR, NULL, "track change") == 0) clear_owed[d] = 0;      /* busy: next tick */
        }
    }
}
static unsigned nib(uint64_t v, unsigned pos) { return (unsigned)((v >> pos) & 0xfu); }

/* replay length in frames for a typical frame interval fi (ticks of hz): 40 ms, at least 6, at most 200 (0.2.8 al028d) */
static uint32_t rate_frames(uint64_t fi, uint64_t hz)
{
    if (!fi || !hz) return 12;
    uint64_t n = (hz / 25u + fi / 2u) / fi;
    return (uint32_t)(n < 6 ? 6 : n > 200 ? 200 : n);
}
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
            LOG("frame filter is not running (no ERP frames for ~3 s): SHIFT+MEMORY / SHIFT+DELETE off "
                "until EP147 restarts; SHIFT+pad / DELETE+pad are stock again (SHIFT+pad deletes a hot cue, a loop too); "
                "auto-arm, engage and Auto-advance still on");
            do_toast("Active Loop on; SHIFT combos off until restart");
        }
    } else {
        if (filter_dead) { filter_dead = 0; LOG("frame filter running again"); }
        tk_hb_same = 0; tk_hb_seen = 1;
    }
    uint64_t now = now_ns();
    {   /* 0.2.8 al028d: the frame rate comes from the filter's own running estimate of the typical frame interval (fi), so
         * it is known after FI_MIN+ frames (about 1 s on the emulator's 17 frames/s, 16 ms on the deck), not after 2048.
         * The replay length is 40 ms in frames (min 6); the stall threshold is the filter's own (3x fi). */
        static int fr_logged; static double fr_last;
        uint32_t fi = __atomic_load_n(&FS.fi, __ATOMIC_RELAXED);
        if (fi && FS.fn >= 2 * FI_MIN) {
            uint32_t n = rate_frames(fi, cnt_hz);
            double fps = (double)cnt_hz / (double)fi;
            if (n != FS.rp_frames) __atomic_store_n(&FS.rp_frames, n, __ATOMIC_RELAXED);
            if ((cfg & CFG_ENABLE) && (!fr_logged || fps < fr_last * 0.9 || fps > fr_last * 1.1)) {
                double lim = 3.0 * fi; if (lim < (double)FS.gthr) lim = (double)FS.gthr;
                LOG("ERP frame rate %.0f/s (typical interval %.1f ms): SHIFT+pad / DELETE replay = %u frames (40 ms), a stall = a gap over %.1f ms (3x the interval)",
                    fps, 1e3 / fps, n, lim * 1e3 / (double)cnt_hz);
                fr_logged = 1; fr_last = fps;
            }
        }
    }
    {   /* DELETE+pad rewritten as SHIFT+pad (the filter saw both): proof for the log that the press reached the filter */
        static uint32_t dlp_seen, tapx_seen;
        uint32_t dp = __atomic_load_n(&FS.dlp, __ATOMIC_RELAXED), tx = __atomic_load_n(&FS.tapx, __ATOMIC_RELAXED);
        if ((dp >> 16) != (dlp_seen >> 16)) {
            unsigned n = ((dp >> 16) - (dlp_seen >> 16)) & 0xffffu;
            dlp_seen = dp;
            if (cfg & CFG_ENABLE) LOG("filter: DELETE+pad %u seen (last: side %u pad %u), sent to stock as SHIFT+pad", n, (dp >> 8) & 1u, dp & 0xffu);
        }
        static uint32_t dle_seen;
        uint32_t de = __atomic_load_n(&FS.dle, __ATOMIC_RELAXED);
        if ((de >> 16) != (dle_seen >> 16)) {
            unsigned n = ((de >> 16) - (dle_seen >> 16)) & 0xffffu;
            dle_seen = de;
            if (cfg & CFG_ENABLE) LOG("filter: DELETE+pad %u on an EMPTY pad swallowed (last: side %u pad %u): nothing deleted, nothing stored", n, (de >> 8) & 1u, de & 0xffu);
        }
        if (tx != tapx_seen) {
            unsigned n = tx - tapx_seen; tapx_seen = tx;
            if (cfg & CFG_ENABLE) LOG("filter: %u DELETE hold(s) released with no pad seen, but a frame gap over 3x the typical interval came with it (the bus thread was stalled): "
                                      "NOT replayed, so stock deletes nothing", n);
        }
    }
    {   /* a DELETE with no pad was replayed to stock as a plain DELETE (a tap): say how long it was held */
        static uint32_t tap_seen;
        uint32_t tp = __atomic_load_n(&FS.tap, __ATOMIC_RELAXED);
        if ((tp >> 16) != (tap_seen >> 16)) {
            tap_seen = tp;
            if (cfg & CFG_ENABLE) {
                unsigned fh = tp & 0xffu;
                uint64_t fiv = __atomic_load_n(&FS.fi, __ATOMIC_RELAXED);
                LOG("DELETE released with no pad (side %u, held %u frames = ~%u ms): replayed as a plain DELETE, stock deletes the called/load memory cue",
                    (tp >> 8) & 1u, fh, (unsigned)(fh * fiv * 1000u / (cnt_hz ? cnt_hz : 1u)));
            }
        }
    }
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
        if (dn[SG_PAD]) apply_pad(s, side_to_deck(s), (int)nib(sn, base + 24));
    }
    tk_snap = sn;
}

/* Row off: "fully silent and inert". The hooks pass through (no events), the filter passes the frame
 * through, the tick below only keeps the deck/press bookkeeping and the config. Nothing is logged,
 * toasted, set, cleared or advanced. At the moment the row goes off the loops this mod armed are
 * cleared (quietly, once) so no armed loop is left behind to trap the playhead with nobody
 * watching it; going on again re-learns everything from the live slots and re-reads the ANLZ
 * files, without arming anything by itself. */
static int tk_row_was = 1;
static void drop_ring(void)
{
    ring_off = (int64_t)now_ns() - (int64_t)cnt_to_ns(cnt_now());
    uint32_t w = __atomic_load_n(&ring_w, __ATOMIC_ACQUIRE);
    for (int k = 0; k < RING; k++) {
        ev_t *e = &ring[k];
        uint32_t sq = __atomic_load_n(&e->seq, __ATOMIC_ACQUIRE);
        if (sq && sq != SEQ_BUSY) {
            if (e->has_cue) fw_cue_dtor(e->cue);
            __atomic_store_n(&e->seq, 0, __ATOMIC_RELEASE);
        }
    }
    ring_r = w; ring_stall = 0;
}
static void row_went_off(void)
{
    int n = 0;
    for (int d = 0; d < NDECK; d++) {
        drop_rem(d); restore_pending[d] = 0; clear_owed[d] = 0; ld_pend[d] = NULL; tc_seen[d] = 0; pv_active[d] = 0; pv_due[d] = 0; pv_idx[d] = -1;
        if (!act_ok[d]) continue;
        if (holder_of(d)) (void)request(d, OP_CLEAR, NULL, "row off");
        fw_cue_dtor(act_buf[d]); act_ok[d] = 0; active_cid[d] = 0; spent[d] = 0; restore_pending[d] = 0;
        n++;
    }
    LOG("row Active Loop -> OFF (applied now): %d armed loop(s) cleared; hooks and filter pass through, silent until it is on again", n);
}
static void row_went_on(void)
{
    unsigned bound = bind_noted();
    for (int d = 0; d < NDECK; d++) {
        tc_seen[d] = 0;
        track_changed(d, "row on");
        autoarm_done[d] = 1;               /* learn only: arming is still your choice (SHIFT+pad / SHIFT+MEMORY) */
        hot_chk[d] = 0;
    }
    LOG("row Active Loop -> ON (applied now): loop lists re-learned from the live slots and the ANLZ files; decks bound from the facades seen while off: %s%s%s%s%s",
        bound & 1u ? "1 " : "", bound & 2u ? "2 " : "", bound & 4u ? "3 " : "", bound & 8u ? "4 " : "", bound ? "" : "none (a deck binds at its next load or CALL)");
}
/* the Active Loop's in point for the playhead hook's re-arm test (INT64_MAX = nothing armed): nothing of a previous load */
static void publish_arm(int d)
{
    uint32_t cf = __atomic_load_n(&cfg_bits, __ATOMIC_RELAXED);
    int64_t a = (act_ok[d] && !spent[d] && (cf & CFG_ENABLE) && (cf & CFG_REARM)) ? active_in[d] : INT64_MAX;
    __atomic_store_n(&arm_in[d], a, __ATOMIC_RELAXED);
}
static void my_tick(void)
{
    tick_no++;
    pin_keepwarm();
    sync_cfg();
    uint32_t cf0 = __atomic_load_n(&cfg_bits, __ATOMIC_RELAXED);
    __atomic_store_n(&FS.cfg, cf0, __ATOMIC_RELAXED);
    int on = (cf0 & CFG_ENABLE) != 0;
    if (!on) {
        if (tk_row_was) { tk_row_was = 0; row_went_off(); }
        drop_ring();
        (void)__atomic_exchange_n(&deck_evt, 0, __ATOMIC_RELAXED);
        poll_filter();                       /* side / press bookkeeping only: it is silent while off */
        pend_service();                      /* applies the one clear queued above, silently */
        pj_service();                        /* 0.2.9: a persist pair already started is finished (never half a delete + store) */
        __atomic_store_n(&led_pub[0], 0u, __ATOMIC_RELAXED); __atomic_store_n(&led_pub[1], 0u, __ATOMIC_RELAXED);
        __atomic_store_n(&led_arm[0], 0u, __ATOMIC_RELAXED); __atomic_store_n(&led_arm[1], 0u, __ATOMIC_RELAXED);
        for (int d = 0; d < NDECK; d++) {
            __atomic_store_n(&hold_pub[d], holder_of(d), __ATOMIC_RELAXED);
            __atomic_store_n(&arm_in[d], INT64_MAX, __ATOMIC_RELAXED);
        }
        return;
    }
    if (!tk_row_was) { tk_row_was = 1; row_went_on(); }
    deck_events();
    poll_cids();            /* al028f: before loop_guard, so a load only the poll sees is handled as a load, not as a violation */
    load_service();
    loop_guard();
    poll_filter();          /* presses first, so events in this tick can correlate */
    emp_refresh();
    drain_ring();
    hot_sweep();
    try_restore();
    pend_service();
    watch_loops();
    pj_service();
    pv_service();
    anlz_service();
    led_refresh();
    led_level_update(cf0);
    timing_log();
    for (int d = 0; d < NDECK; d++) {
        __atomic_store_n(&hold_pub[d], holder_of(d), __ATOMIC_RELAXED);
        publish_arm(d);
    }
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
        "SHIFT+loop pad: arm + save (pad red).",
        "SHIFT+red pad again: off, flag removed.",
        "DELETE+pad: delete the hot cue and its flag.",
        "SHIFT+DELETE: clear. No playhead jump.",
        "Filter off: SHIFT+pad deletes a hot cue!",
    };
    int nl = filter_dead ? 6 : 5;   /* 0.2.7: the core switched the frame filter off */
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

/* 0.2.8: deleteCue (0x861218) must start with the exact two instructions we disassembled on
 * 2.00/2.01, else it is not claimed (the observer only keeps the loop list fresh). */
static int delete_target_ok(void)
{
    uint32_t ins[2];
    if (!target_ok(VA_DELETE, "deleteCue")) return 0;
    if (safe_read(VA_DELETE, ins, sizeof ins) != 0 || ins[0] != DELETE_INS0 || ins[1] != DELETE_INS1) {
        LOG("precheck deleteCue %#lx: unexpected prologue (%08x %08x); claiming nothing", (unsigned long)VA_DELETE,
            ins[0], ins[1]);
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
    /* last: after init() every hook is a pass-through until al_live, and a refused or
     * mismatching deleteCue claim only costs the delete observer (list refresh, clear on delete) */
    orig_delete = NULL;
    if (!delete_target_ok()) LOG("deleteCue observer NOT installed (loop list is refreshed from the live slots instead)");
    else if (C->claim_hook(MOD_ID, VA_DELETE, (void *)my_delete, (void **)&orig_delete) != 0) {
        LOG("claim_hook deleteCue refused (observer off; loop list is refreshed from the live slots)");
        orig_delete = NULL;
    } else LOG("deleteCue observer installed at %#lx (never swallows)", (unsigned long)VA_DELETE);
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
    { uint64_t f = cnt_frq(); if (f) cnt_hz = f; }
    pin_code();
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
    {   /* 0.2.9: the firmware's memory-cue delete (mov x2,x1; mov w1,#0; b 0x86b218), used only to take our flagged copy / flag away */
        uint32_t di[2];
        if (safe_read(VA_STORE_DEL, di, sizeof di) == 0 && di[0] == 0xaa0103e2u && di[1] == 0x52800001u) fw_store_del = (store_del_fn)VA_STORE_DEL;
        else LOG("memory delete 0x86b3f8: unexpected prologue; the Active Loop is not saved (armed for this session only)");
    }

    if (AZ_HAS(C, register_row)) {
        const az_row_v1 en = {
            .size = sizeof en, .key = "enable", .label = "Active Loop",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON,
            .help = "SHIFT+MEMORY next loop, SHIFT+pad arm+save a hot loop (red pad; again = off), DELETE+pad delete, SHIFT+DELETE clear.",
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
        const az_row_v1 pu = {
            .size = sizeof pu, .key = "pulse", .label = "Pulse armed pad",
            .kind = AZ_ROW_TOGGLE, .flags = AZ_ROW_DEFAULT_ON, .parent = "enable",
            .help = "The armed Active Loop pad breathes (full red fades to off, 4 s). Off: solid red.",
            .on_change = row_changed, .user = (void *)"Pulse armed pad",
        };
        h_pulse = C->register_row(MOD_ID, &pu);
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
    tk_row_was = row_on();       /* 0.2.8: a start with the row off is silent; only a toggle is logged */
    memset(ring, 0, sizeof ring);                 /* pre-touch: no first-use page fault in a hook */
    __atomic_store_n(&al_live, 1, __ATOMIC_RELEASE);
    LOG("loaded 0.2.12; filter: SHIFT+MEMORY/DELETE, SHIFT+pad arm+save when nothing of yours is lost (second press off), red pad, DELETE+pad (+ our own saved copy); deleteCue observer; template-free ANLZ build, flags at parse, wait-free filter/hot path, updatable toast (%s); setout=%d",
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
    .fw_tags = FW_TAGS, .init = init, .tick = my_tick, .on_frame = on_led,
};

const az_mod_v1 *az_mod_v1_entry(void) { return &MOD; }
