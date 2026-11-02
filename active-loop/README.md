# Active Loop Picker

Community mod for AZ-MODS on the Pioneer XDJ-AZ (EP147 2.00 / 2.01).

## Combos

| Combo | Action |
|---|---|
| **SHIFT + MEMORY** | Next stored loop ahead of the playhead becomes the Active Loop (press again to cycle). The MEMORY bit is cleared for the hold. |
| **SHIFT + DELETE** | Clear the Active Loop. The DELETE bit is cleared for the hold. |
| **SHIFT + pad** on a hot cue that holds a **loop**, first press | Arm that loop as the Active Loop and, **when no loop of yours would be lost**, save it (0.2.9/0.2.10): the pad turns **red** and a memory loop with the Active Loop flag is stored by the deck's own software. The pad press is eaten, so the stock delete does **not** run; the strip says *Active Loop saved: hot cue at ...*, or why it only armed it (see 0.2.10). |
| **SHIFT + the armed pad** again | Active Loop **off**: it is cleared. A saved copy **this mod made and confirmed** is deleted; a saved flag on any other loop **stays** (the strip says so). The normal hot cue loop stays. |
| **SHIFT + pad** on a hot cue that is a **point** | Passed to stock (replayed for 40 ms): the point cue is deleted, as without the mod. |
| **DELETE + pad** (hold DELETE, press the pad) | Delete that hot cue, loop or point. The mod sends the pad to stock as SHIFT+pad (0.2.13: SHIFT first, 40 ms ahead, then the pad for 40 ms). If it was the Active Loop, the Active Loop is cleared, and a saved copy this mod made and confirmed is deleted with it (any other memory loop stays). |
| **DELETE** alone (tap) | Stock DELETE (memory cue), replayed a few milliseconds late when you release it. Deleting the memory loop that is the Active Loop clears the Active Loop too. |
| **SHIFT + pad** in sampler, keyboard or any other pad mode | The mod treats the pad as a hot cue pad. A pad that is not a loop is replayed to stock, so it still does what stock does; only a pad that holds a hot cue **loop** is armed instead. Since al028d a pad whose hot cue slot is **empty** is swallowed (SHIFT+pad and DELETE+pad) unless the core reports BEAT LOOP or SLIP LOOP mode; the core cannot tell sampler, keyboard, HOT CUE and BEAT JUMP apart, so there SHIFT+empty pad does nothing. |
| Mod row off, or the core has switched the frame filter off | Everything above is stock. **SHIFT+pad then deletes a hot cue, a loop too**, as it always did. |

SHIFT+CALL is not used by the mod. (Earlier notes said stock doubles/halves the loop with it. Correction: stock CALL picks double or half from the loop state, not from SHIFT.)

## 0.2.14 - fewer pop-ups

0.2.13 and earlier showed a pop-up for almost everything. Now a pop-up appears **only when the track's loops change**, and then **once**:

- a loop is **saved** (*Active Loop saved: hot cue at ...*), or armed but **not saved** (*NOT saved: ...*, including when the firmware did not store it);
- the Active Loop is turned **off** (SHIFT+the armed pad, or SHIFT+DELETE);
- the Active Loop's loop is **deleted** (*Active Loop cleared (loop deleted)*), including the case where its saved copy cannot be removed right now;
- the SHIFT combos are switched off by the core (*SHIFT combos off until restart*).

The **ready, auto-arm, SHIFT+MEMORY pick and done** notices (and *deck not ready* / *no stored loop ahead*) are now **log-only**: they are written to the mod's log but nothing is shown on the deck.

**Repeat filter:** the same pop-up text is shown at most once in a **4-second window**; a repeat inside that window goes to the log only.

New Mod Menu row **Pop-up messages** (default **on**). With it **off** no pop-up is shown at all, and the **pulsing pad** (see 0.2.11/0.2.12) is the only indicator that a loop is armed. The **Pulse armed pad** row still works on its own.

Nothing else changed: DELETE+pad, SHIFT+pad arm/save, the safety rules and the pad colours are as in 0.2.13.

## 0.2.13 - DELETE + pad works on the deck

Holding DELETE and pressing a hot cue pad (a loop pad too) removed nothing on the real deck in 0.2.12, and because SHIFT+pad on a loop pad *arms* it instead of deleting it, the pad could not be removed at all. The mod turns DELETE+pad into SHIFT+pad for stock, and it used to put the SHIFT bit one input frame ahead of the pad. One frame is 60 ms or more on the emulator (17 frames/s, where every test passed) but about 1 ms on the deck (about 1000 frames/s), so stock saw SHIFT and the pad at the same instant. Now the pad pressed under DELETE is eaten and sent as a proper SHIFT+pad: **SHIFT alone for 40 ms, then SHIFT+pad for 40 ms, then both up** (one frame ahead at the emulator's rate, as before). A quick tap is delivered whole. A plain pad pressed in the 80 ms after a DELETE+pad is held back until the mod's SHIFT is gone, so it can never be read as SHIFT+pad. If a DELETE+pad is not followed by a hot cue delete within 0.8 s, the log says so and the next one leads by twice as long (80, then 160 ms). The SHIFT+pad replay for a point cue uses the same lead. Nothing else changed.

**If a pad still will not delete:** switch the mod off in the Mod Menu (Active Loop row), delete the pad with stock SHIFT+pad, and switch it back on; or delete it in rekordbox.

## 0.2.12 - slower, and all the way out

The breath of the **armed** pad now takes **4 seconds** per cycle and fades from full red all the way to **off** (the pad's colour goes to zero) and back. The curve leaves black quickly, so the pad is dark only for a brief moment (about a quarter of a second at the bottom) and reads as lit the rest of the time. Saved-but-not-armed pads stay solid red; **Pulse armed pad** off = solid red. Nothing else changed.

## 0.2.11 - the armed pad breathes

The pad of the **armed** Active Loop fades smoothly between full red and a dim red that stays clearly lit, one cycle about every 2 seconds (0.2.12: 4 s, down to off, see above). A loop that is only **saved** (flagged, not armed) stays solid red. Only the pad's colour in the LED frame changes: the tick works out the level from the monotonic clock and publishes one byte; the LED callback reads that byte. Mod Menu: **Pulse armed pad** (default on; off = solid red as in 0.2.10). Nothing else changed: every 0.2.10 rule stands.

## 0.2.10 - nothing of yours is ever deleted

AZ-EMU found that 0.2.9 could delete one of your own memory loops. The firmware (read from the deck software) works like this:

- storing a memory loop whose in/out (whole milliseconds) equal one that exists is **ignored**, flagged or not; there is no
  "set the Active Loop flag on this loop" call;
- storing a **flagged** memory loop makes the firmware **delete** every other flagged memory loop of the track (it does not un-flag it);
- a delete removes the memory cue with the same in/out, flagged or not;
- at most 10 memory cues (points and loops together).

So the rule now is: **the mod deletes only a saved copy it stored itself in this session and then saw in the track's analysis
file** (the read-back, about 5 s later). Everything else is left alone. SHIFT+pad therefore does one of:

| What it finds | What happens |
|---|---|
| nothing in the way | arms the loop and stores a flagged memory copy (*Active Loop saved*); confirmed by the read-back |
| a memory loop with the same in/out, not flagged | armed only: *NOT saved: same loop already in memory* |
| that loop is already flagged (rekordbox's) | armed, *already saved*, nothing stored |
| another flagged loop that this mod did not store (e.g. rekordbox's Active Loop) | armed only: *NOT saved: another loop is saved as Active* (saving would make the firmware delete it) |
| the memory list is full (loops + points, 10) | armed only: *NOT saved: memory list full* |
| no store object yet (just after a restart) | armed only: *not saved yet* |
| the track's stored loops are not read yet | armed only: *NOT saved yet (reading the track)* |

Second press / DELETE+pad: our confirmed copy is deleted, nothing else is. If the saved flag belongs to another loop, the Active
Loop is turned off but the flag **stays** (it comes back at the next load) and the strip says so. If our copy cannot be removed right
now (no store object, queue busy) the loop **stays armed**. A pad that is red only because of a saved flag arms again on the next press.

The store object is now recognised by the real CueStoreMaintainer's vtable/queue (0.2.9 tested the wrong thing) and is also searched in
the CueCallManager after any cue delete.

## 0.2.9 - the Active Loop is kept, and its pad is red

(Where this section differs from 0.2.10 above, 0.2.10 wins: no delete + store of your loops, no un-flagging.)

What changed (everything else is 0.2.8):

- **SHIFT+loop pad, first press.** The loop is armed as before, and **saved**. The rekordbox "Active Loop"
  is a flag on a **memory** loop (`CueProperty+0xe1`; the firmware only honours it on memory cues, never on
  hot cues), so the mod stores a memory-kind copy of the pad's loop with that flag, through the firmware's own
  store function (`0x86b410`, the call stock MEMORY makes). The deck's normal cue write-back then writes it to
  the track (database / ANLZ): **the mod writes no file itself.** If a memory loop with the same in/out already
  exists (within 2 ms) that one is flagged instead (delete + store with the flag), so no copy is added.
  Only one loop per track is flagged: the previous one is un-flagged first (a copy made by this mod is deleted,
  another memory loop of yours is re-stored without the flag).
- **SHIFT+the red pad, second press.** Active Loop off: the armed loop is cleared, the flag is removed (our own copy deleted, or
  a memory loop of yours re-stored without the flag) and the pad shows its own colour again. The normal hot cue loop stays.
- **DELETE+pad.** Unchanged (the hot cue is deleted), and the flagged memory loop with that pad's extent is deleted too
  (not if another pad still holds the same loop).
- **Red pad.** While HOT CUE is the lit pad mode, the pad whose loop is the Active Loop (the armed one, otherwise the flagged
  loop of the track) is red. Done in the core's `on_frame` LED callback: one cell edit at the cell's own brightness, no state of
  its own; stock rewrites the frame every cycle, so a pad goes back to its colour the moment it stops being red.
  A track that loads with a flagged loop equal to a hot cue loop shows that pad red at once. The jog display tile is **not** recoloured.
- **Needs the store.** The first save of a session needs the firmware's store object: it is picked up from any stock cue store
  (MEMORY, a hot cue pad press) or any cue delete. Before that SHIFT+pad still arms the loop, says
  *Active Loop armed at ... (not saved yet)* and saves nothing. Memory list full (10 loops): armed, not saved, says so.
- **Read-back.** About 5 s after the last save/delete job the track's ANLZ is re-read (read-only) and the log says
  `persist check: ANLZ has N memory loops, F flagged; expected ...: OK / MISMATCH`.
- Jobs (store / delete) run in the tick, one per ~130 ms, in order; a delete + store pair is always finished (also if the track changes).
- The frame filter, the hooks and every other combo are unchanged from 0.2.8.

Limits: a flagged memory loop that the firmware never showed live (built from ANLZ only) cannot be matched exactly, so it is
not un-flagged (logged); the track can then hold two flags. A copy made by this mod before a restart is not remembered as ours,
so after a restart the second press leaves that memory loop in place without its flag.

## 0.2.8 - arm a loop pad with SHIFT+pad, delete with DELETE+pad

A first 0.2.8 build tried to swallow stock's delete inside the firmware: SHIFT+pad arms a loop
pad, the same pad again deletes it. It did not work on the emulator: the first press armed the
loop **and** the hot cue was deleted anyway. Reason (firmware 2.00/2.01):
`CueCallManager::deleteCue` (`0x861218`) is the *persistence* half of a delete: it posts the
database / ANLZ write. The live hot cue is cleared earlier, by `CueLoopSetFacade::clearCue`
(`0x1431720`) on the player thread, so swallowing `deleteCue` cannot keep the cue.

So this release goes back to the frame filter for SHIFT+pad, and adds a deterministic delete:

- **SHIFT+pad**: the pad bits are zeroed while SHIFT is held (as 0.2.7). The tick looks at the
  live hot cue slot: a loop is armed; a point cue is handed back to stock by replaying SHIFT+pad
  for 40 ms, so it is still deleted by SHIFT+pad. An **empty pad is swallowed** (0.2.8, al028d:
  stock stores a hot cue on SHIFT+empty pad, which the replay used to trigger), and a pad that went
  down under SHIFT stays eaten until it is released, so letting go of SHIFT first no longer
  presses the pad.
- **DELETE+pad**: while DELETE is held alone, the filter eats the DELETE bit; a pad pressed
  meanwhile is passed on together with SHIFT, which is stock's delete. DELETE released without a
  pad is replayed as a 40 ms tap, so plain DELETE keeps working (about 1 frame late).
- **Emulator round 3 fixes (same version, rebuilt):** a DELETE hold that a pad or SHIFT joined is
  never replayed as a tap; a new DELETE press cancels an older tap replay (it used to add its
  DELETE bit onto the next DELETE+pad, and stock then also deleted the load / called memory
  cue); a hold with a gap in the frames (the bus thread was stalled, a pad may have come and gone
  unseen) is not replayed; SHIFT leads the pad by one frame when the filter raises it. Every filter
  call is timed (calls over 100 / 200 us, gaps between calls) and reported every 30 s, the ERP frame
  rate comes from the filter's own stamps, the row says ON / OFF once, a deck load is logged, the
  decks bind when the row goes on, and routine log lines are capped (40 per 10 s) so a stress run
  stays inside the core's 128 KB log rotation.
- **Emulator round 5 fixes (al028d, same version, rebuilt):** SHIFT+pad on an EMPTY pad stores
  nothing (the mod reads the slot's Position: invalid = empty) and DELETE+pad on an empty pad is
  swallowed the same way (the tick publishes the empty pads while DELETE is held, so DELETE must
  be down about one tick, ~30 ms, before the pad); a pad pressed under SHIFT stays eaten until it
  is up. The stall test for a plain DELETE tap is relative to the measured frame interval (a gap
  over 3x the typical interval, never under 5 ms): the emulator serves ~17 frames/s, where the old
  fixed 40 ms made every frame a stall and no tap was ever replayed. The replay length (40 ms in
  frames, min 6) is known after ~1 s instead of 2 min. A "track loaded" line is written by every
  path that sees a load (the core's deck event does not fire for a deck's first load after the
  core started, nor for the same track again).
- `deleteCue` is still hooked, but only as an **observer**: it never changes what the firmware
  does. It lets the mod drop a deleted loop from its list and clear the Active Loop if that
  loop was deleted. With the mod row off the hook is a bare pass-through.
- Memory-loop deletes match the nearest in/out within 0.5 s (the loop the mod builds from the
  ANLZ file can sit ~0.2 s from the firmware's). The loop list is also checked against the live
  hot cue slots before it is used, so a hot cue deleted by the firmware cannot linger in it.

Limits: if the core switches the frame filter off (see below), SHIFT+pad is stock and a loop pad
is deleted, a log line and the page say so. With several pads pressed in the same frame under
SHIFT only the lowest is used. DELETE must be pressed before the pad.

### 0.2.8, second round (AZ-EMU al028b)

- **The core switched the frame filter off in 3 of 3 sessions** (own peak 1801.8 us, bursts of cue
  stores/deletes). The filter body is a few dozen instructions and costs ~0.1 us warm; the long
  calls were cold or evicted code (page faults / emulator translation) right after the
  database/ANLZ write bursts, and the old own-time sample (1 frame in 64) saw only a few of them.
  Now: the filter is bit operations on its own 64-byte state and nothing else. No clock (one
  2-read sample per 256 frames), no log, no call, no read of live cue memory, no allocation.
  Replays are counted in frames (the tick calibrates the frame rate). Every filter path is
  pre-run at init, and the mod's own read-only segments are `mlock()`ed (the tick keeps them
  warm if the lock is refused); the log shows `code pages: ...`. The `deleteCue` observer reads
  two words of the cue and pushes them to the ring: no firmware call, no clock. All slot reads,
  matching and clearing run in the tick. `test/filter_bench.c` times every filter path.
- **Row off is silent and inert**: no "exited", no "auto-advance off", no "Active Loop done",
  nothing queued. One quiet clear when you switch the row off; the lists are re-learned
  (arming nothing) when you switch it on.
- **Deck attribution**: `deleteCue` has one shared manager and no player in its arguments, so
  the deck comes from state, never from track identity. A hot cue delete is attributed from each deck's live
  slot (the one that no longer holds the loop); a memory-loop delete from the press that
  caused it, else from the only deck holding that loop. If it cannot be told, nothing changes.
- **Active Loop vs hot cue / memory with the same extent**: the Active Loop is cleared only when
  the deleted cue is the one it was armed from (kind or slot), not because the extent matches.
- **Count**: a loop stored both as memory and as hot cue (same extent) is one stop. After
  deleting one of three loops the count is 2, and "only loop ahead" only appears when one is left.

### 0.2.8, track changes (AZ-EMU al028e)

- **An Active Loop never outlives its track.** On every content change (and on an eject) the armed
  loop and everything per deck that belongs to it (live state, the value the playhead hook re-arms
  from, the exit watch, the loop counts) are cleared in the same tick, **before** anything is
  restored, and the firmware's own copy is cleared too. Before this, loading another track left the
  old track's loop "armed" (a stale "Active Loop at 00:04.2 armed" line on a track with no loops).
  A check every tick backs it up: a deck that reports another content id than the one the loop was
  armed on drops the loop, even if no load event was seen.
- **One load, one restore.** The core's deck event, the hooks' load cues and the content-id poll all
  see the same load; only the first runs the load handling, so "restore deck N" and "reading stored
  loops" are logged once per load. A different id, or the same id after 2 s, is a new load.
- **Restore** only brings back the loop of the previous load of the same track (once its memory loop
  is seen live again, as before); another track forgets it. A loop that already exited is not restored.
- **Log:** "track loaded (content id N; ...)" waits until the deck reports the id (the load cues run
  before it does; it used to say "content id 0"). Point-cue pad lines print the raw Position words
  (`key ..., sample ...`) like the EMPTY lines.

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
  (filter stopped)". From then until EP147 restarts, SHIFT+MEMORY / SHIFT+DELETE are stock
  again (and SHIFT+pad / DELETE+pad too: SHIFT+pad then deletes a hot cue, a loop included). The SDK has no query or callback
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
- Tests: `test/filter_test.c`, `test/delete_test.c`, `test/echeck_test.c`, `test/ring_test.c`, `test/anlz_test.c`, `test/filter_bench.c` (host clang; the bench on arm64 in the SDK container).

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
- SHIFT+pad (hot cue loop): that loop becomes the Active auto loop. No jump, no store. (0.2.8: DELETE+pad deletes a hot cue.)
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
