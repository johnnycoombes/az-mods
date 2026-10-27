# Active Loop Picker

Community mod for AZ-MODS on the Pioneer XDJ-AZ (EP147 2.00 / 2.01).

## Combos

| Combo | Action |
|---|---|
| **SHIFT + MEMORY** | Last-called loop → Active. MEMORY bit cleared for the hold. |
| **SHIFT + DELETE** | Clear Active Loop. DELETE bit cleared for the hold. |
| **SHIFT + pad** on a hot-cue **LOOP** (default on) | Stock-copy that loop into the memory-cue list, then Active. |

SHIFT+CALL is unused (stock doubles/halves the loop).

## 0.1.4

- Toasts: `banner` is called straight from `tick()`; built with `-fno-optimize-sibling-calls` so no core call becomes a tail branch (that made the core see its own `post_ui` runner as the caller).
- Deck = LocalDjPlayerID read from the facade (`facade+0x40` -> `+0xf8`, checked against `+0x108`). It is the same value DjPlayerUnit stores at `[[unit+0x1a0]+0x1f18]`. Each deck's engine is bound once and never rebound.
- Memory loops and hot-cue loops are cached separately. SHIFT+MEMORY, dup and copy all call set-active with a memory cue.
- call/load/store/setLoopOutPoint hooks only copy the cue into a ring. Logs and caches run in `tick()`.

## 0.1.3

- Toasts via `post_ui` → `banner` (2500 ms); every toast is also logged.
- SHIFT+pad: on cache miss, read the hot-cue CueProperty from LoopState (`facade+0x60` / the function at `0x149ef50`); also hooks `setLoopOutPoint` to learn live loops; every early exit is logged with its reason.
- ERP filter: bit tests + one relaxed atomic OR. No deck bind, `side_deck`, resolve, `row_value`, or other shared state with the load hook.
- Deck bind prefers DjPlayerUnit vtable on the engine pointer, then `deck_for_ds(LoopState)`.

## After a failed init / community suspend

```bash
rm -f /home/root/settings/azmod-community/.starting
```

Then **Reload**.

## Build

```bash
make CC=aarch64-linux-gnu-gcc
python3 ../../az-mods-sdk/tools/azmod_pack.py package .
```

`min_core` is `1.2.3`. Does not hook MoveCheckingStrategy.

## 0.1.5
Crash fix (aligned ring), hooks copy raw args only, deck decoded in tick with fault-free
reads and a validated player id (pid >= 4 rejected), ready banner deferred ~5 s.

## 0.1.6
Deck binding by input correlation: a CALL/MEMORY/pad press on a side followed within 100 ms
by a call event from a facade counts as a match; two consistent matches lock that facade to
the deck (secondary deck select respected; one facade per deck). Player-id search only
confirms. "Active Loop ready" shows on the first lock.

## 0.1.7
- Decks lock at the first event via the player id (facade+0xa8); CALL-press correlation is the fallback.
- The frame filter is ~77 instructions: no clock, no atomics except one relaxed store, no calls.
- If AZ-MODS switches the filter off (200 us x 8 overruns, cumulative, per session), the mod
  notices within ~3 s (ERP frame heartbeat stops), logs it and shows "Active Loop: off
  (filter stopped)". From then until EP147 restarts, SHIFT+MEMORY / SHIFT+DELETE / SHIFT+pad
  are stock again (SHIFT+pad = stock behaviour on hot cues). The SDK has no query or callback
  for this; the heartbeat is the only signal.
- MEMORY store events are attributed by the press side only, never by a default.

## 0.1.8
- Fixed: cache slots were destroyed before ever being constructed (EP147 logged
  "fatal error at ~PositionWithSourceInfo", non-fatal). Every set now logs "set check" with
  the holder loop and LoopState has-loop/looping (auto-engage needs no LoopState loop).
- Filter returns after one xor+mask when nothing relevant changed and SHIFT is up.
  Filter switch-offs on the emulator (e.g. RELOOP + fast BEAT JUMP bursts) are most likely
  qemu stalls: the core times only our call (cntvct before/after), so firmware work on the
  bus thread before us is not charged; only preemption during our ~100 ns call is.
- When the filter is off, frames are untouched: the filter never changes SHIFT or CALL
  bits and ERP frames are full-state, so no eaten bit can persist.
- "Active Loop ready" ~3 s after the first deck lock. Same-track reload re-applies the
  remembered active loop once its memory cue is seen ("restore deck N" log line).

## 0.1.9
Fresh SHIFT+pad copy sets active ~100 ms after the store via the dup path; track reload detected from load-event bursts / content id (log "track (re)load detected"), same track restores the saved active loop; per-deck ready banner at 1 s, 2.5 s, 4 s after lock.

## 0.1.10
Stock DELETE (no SHIFT) is tracked: the deleted memory cue leaves the cache, and if it was the Active Loop it is cleared and forgotten. The dup check and restore only use memory cues seen live (CALL/store) since the last load; after a reload, restore happens when the loop is first CALLed.

## 0.1.11
No spurious startup DELETE; the fresh-copy set is skipped (logged, "Memory loop stored (not Active)") when LoopState holds a non-looping loop.

## 0.1.12
SHIFT+pad on a hot-cue loop that is not already a memory loop now ARMS it: press stock MEMORY while inside that loop and the stored memory loop becomes Active. No synthetic store, so no MEMORY pre-press is needed. Stock DELETE is treated as pending (no clear) until the cue is seen again.

## 0.2.5

- A library row without its audio file (e.g. content id 34) is skipped: `stored loops not read: no audio file for content id N (...), build skipped`.
- Loop builds still waiting after 60 s stop with one line: `stopped building N memory loops after 60 s (no base cue or source key)`.

## 0.2.4

- **Flags at parse.** The rekordbox Active Loop flags (PCPT status 4) are counted when the ANLZ
  is read: `deck N: ANLZ parsed: L loops (M memory), F flagged ...`. This no longer depends on a
  template. Checked on JC's new export: Cola, Hold On, Good Life and The Fog each have exactly
  one flagged memory loop, and all 67 DAT files with loops have exactly one.
- **No template needed.** If no loop cue exists on any deck, the loop is built from a
  firmware-constructed cue of the same deck: a point hot cue from the load, or else a copy of
  the ActiveLoop holder's own CueProperty slot, made on the playhead thread. This deck's
  Position source key (bytes +8..+0x13, the fields the firmware's is_loop checks) goes into both
  Positions, with in/out samples at 44.1 per ms. The firmware's own `is_loop` must accept the
  result, or it is not used. While the track stays loaded the mod never gives up; it arms the
  flagged loop as soon as it is built.
- I did not hook or call a firmware constructor: every cue still comes from the firmware's own
  copy constructor, which is the safest route.
- **Filter switch-offs on the emulator are the emulator.** The filter-probe control mod (a
  filter that does nothing) was switched off too when it ran alone. No fix is needed.

## 0.2.3

- **Every deck builds its ANLZ memory loops.** Template order: a loop cue of that deck, then a
  loop from that deck's LoopState hot cue slots, then another deck's loop cue with this deck's
  Position source key (captured from the playhead hook) written into its in/out Positions.
  Units are shared between decks (else 44.1 per ms). Loops still waiting for a template are
  retried every 0.25 s for up to 30 s; auto-arm runs after that, so deck 2 arms and advances
  like deck 1.
- **Wait-free filter and hot path.** Neither path takes a lock or does an atomic
  read-modify-write, and nothing they touch sits behind a lock. The tick clears the timing
  maxima with a plain load and store. The core's filter wrapper only times our call (no lock,
  no hook or tick work inside it). Spikes of 200-1500 us on ~20 instructions mean the thread was
  preempted: the guest load was 3.6-8.7 on 6 vCPUs, EP147 ran at 140-164 % CPU, and qemu used
  ~3.5 host cores. `../filter-probe` is a no-op control filter to show this on the emulator.
- **One updatable toast** (core surface, above EP147). A newer toast replaces the text at once;
  a stale 'ready (deck N)' is taken down when another deck loads. If the core has no surfaces,
  the 0.2.2 banner path is used.

## 0.2.2

- **Active Loop at load from rekordbox.** rekordbox keeps its per-track "Active Loop" flag in the
  ANLZ `.DAT` memory-cue list: PCPT entry, `+0x10` status = **4** (0 on every other cue). Checked on
  all 36 DAT files with loops we have: exactly one flagged loop each. EXT's PCO2 has no such
  field, so the DAT is now always read too (read-only) and its flags are copied onto the EXT
  entries. `exportLibrary.db` was not used (encrypted; not needed).
  At load: the earliest flagged loop is armed (`source: rekordbox flag`); if none is flagged
  and Auto-advance is on, the first stored loop ahead (`source: fallback`). SHIFT+MEMORY still
  picks any later loop without a jump. Auto-advance prefers the next flagged loop ahead,
  then any stored loop ahead.
- **Engage-check hook (0x14b37a0)**: O(1) and lock-free, no read-modify-write atomics on the
  hot path; it compares the move against a per-deck arm point the tick precomputes. Set/clear
  and re-arm run only on the rare move that needs them (timed separately as "rare path").
- **Filter**: no clock reads on the fast path (own timing sampled 1 frame in 64). The core
  times each call itself (`isb; cntvct` around the call, the same clock) and counts any
  preemption inside that window; 0.2.1's own window was narrower, which is why it read 189.5 us
  while the core counted >200 us.
- **Menu**: every row change is logged when the core saves it (`row Auto-advance -> OFF`).
  The missed first knob press after a tap happens in the core's menu input (the docked menu is
  not yet focused when the press arrives), not in the mod.
- **Toasts**: one pending toast, coalesced ~300 ms (fast SHIFT+MEMORY shows the latest pick);
  pick toasts 1.5 s; the ready toast shows once per deck, and a load on another deck drops it.
- Tests: `test/filter_test.c`, `test/echeck_test.c`, `test/anlz_test.c` (host gcc).

## 0.2.1

- **Stored loops known at load.** When a track loads, a worker thread reads the track's
  `ANLZ0000.EXT` (PCO2) or `ANLZ0000.DAT` (PCOB) from the stick, **read-only** (`O_RDONLY`; nothing
  is ever written). The audio path comes from the library (`lib_tracks`), the ANLZ folder from a
  one-time index of `PIONEER/USBANLZ` by each file's PPTH tag. Memory loops not called yet get a
  cue built from a loop cue of the same track (firmware copy, only in/out changed); units are
  calibrated from a hot cue loop in both lists. Everything is logged (`anlz deck N: ...`).
- **Exit detection by playhead.** Inside the loop, then more than 150 ms past its out point for
  3 ticks = exited. Auto-advance (default **on**) picks the next stored loop ahead; with nothing
  ahead, or Auto-advance off, the Active Loop is cleared so an exited loop cannot trap again on a
  later pass. Every decision is logged.
- **SHIFT+MEMORY cycles** after the queued/Active loop (not the playhead), wraps to the first
  ahead, and says so when the Active Loop is the only one ahead.
- **Timing log every 10 s**: the filter's and hooks' own maximum durations (CPU counter).
- **Menu**: just *Active Loop*, *Auto-advance* and *Re-arm*. The combo, pad, clear and toast rows
  are gone (always on with Active Loop).

## 0.2.0 (redesign)
- SHIFT+pad (hot cue loop): that loop becomes the Active auto loop. No jump, no store.
- SHIFT+MEMORY: next stored loop ahead of the playhead becomes Active; press again to cycle
  to the one after (wraps to the first ahead). Toast shows its time. No jump.
- SHIFT+DELETE: clear.
- Menu: "Auto-advance" (on by default; unreleased change after 0.2.0): after the Active Loop is exited, the next stored loop ahead
  becomes Active. "Engage even after another loop" (on): re-arm, see below.
- Set/clear run on EP147's playhead thread from a hook on the auto-engage check 0x14b37a0
  (tick fallback when the deck is stopped). Re-arm: when an earlier loop still sits in
  LoopState (normally blocks auto-engage), the firmware's own engage 0x14b31f0 runs at the
  in point, replacing that loop.
- Memory loops are pickable once seen this load (called or stored); EP147 does not pass
  memory cues through the load hook. Hot cue loops are known from load.

## 0.2.6
Release build: source link https://github.com/johnnycoombes/az-mods, user-facing description, own icon. `/proc/self/maps` (fallback only) is read once in init with O_CLOEXEC, never on the tick path.
