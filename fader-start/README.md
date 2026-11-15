# Fader Start (fader-start) 0.0.7 BETA

**What it does.** Channel fader start for the XDJ-AZ: bringing a closed channel fader up plays the
deck on that channel (channel n = deck n) from where it stands, the same call the deck's own PLAY
makes. If the deck is looping and **Exit loop on fader up** is on, the loop is exited first and play
is called only if the deck is paused. **Reloop is never called.** It **patches the deck software's
behaviour**: it hooks the mixer's fader handler (pass-through, the deck runs first) and calls the
deck's own play and loop-exit functions. It uses no network and writes only its own log file.

**Firmware:** XDJ-AZ 2.00 / 2.01 only (`ep147-9e9454e357389ece`). Needs AZ-MODS 1.2.3 or newer; tested under AZ-MODS 1.2.9.

**Status: BETA.** Its manifest carries `"beta": true`, so the deck may mark it BETA. It has passed the
AZ-EMU emulator tests and JC's deck tests, but it is new: try it on a spare stick first.

**Package:** `fader-start-0.0.7.azmod`, 94159 bytes,
sha256 `1c66c0ef505b5d745aa25c9a682e4bc92ac593674045b3b681928f0a9b497656`
(`fader-start.so` sha256 `7dc500a0cda08e0453ed20d9a465b6cf4c46f334179f5713e95ef600fb42dce4`).
Built in the SDK container (`az-mods-sdk:1`); `azmod-pack validate` OK. These are the exact bytes
tested on the deck; nothing was rebuilt for this release.

## Setup

1. **Install and Allow.** On the deck: **Mod Menu > Community Mods**, pick **Fader Start** under
   **johnnycoombes/az-mods** in **Browse**, install it, press **Allow** when the deck asks about the
   new mod, then **Reload now**.
2. **Turn it on.** The **Fader Start** master row (Mod Menu > Fader Start) is **off by default**.
   While it is off the mod only logs what it would have done. Switch it on to let it act.
3. **Press PLAY once on each deck.** The mod learns each deck from your first PLAY press on it after
   loading, so press PLAY once per deck you want fader start on.
4. **Press LOOP once to teach the loop exit.** One LOOP press on a deck teaches the mod that deck's
   loop object. Until then, a paused deck in a loop still plays on fader up (it just can't exit the
   loop first).

Then close a channel fader (fully down for a moment) and bring it up: the deck plays.

## Rows (Mod Menu > Fader Start)

| Row | Default | |
|---|---|---|
| Fader Start | **off** | Master. Off: fader moves are only logged |
| Exit loop on fader up | on | Off: a looping deck is treated like any other (play if paused) |
| Logging | on | `/tmp/azmod-community/fader-start.log` (saved with the deck's log export) |
| On-Air hook (Reload) | off | Record-only timing hook; leave off |

## How it decides

- A fader counts as "brought up" after sitting closed (≤ 16/1023) for at least 250 ms and then
  passing 64/1023. One action per fader edge.
- Nothing happens in the first 2 s after the mod loads or 2 s after a track loads on that deck.
- Channels 3 and 4 follow the deck's own On-Air input rule.
- Not playing: play. Playing: nothing. Looping with Exit loop on: exit the loop, then play if paused.
- Every object is re-checked before each call. If any check fails, the mod **disarms** (a banner
  shows) and does nothing until you switch the master row off and on again.
- It refuses to act while the automix mod hooks the deck's Play function.

## What's new in 0.0.7

- The loop object is learned from your LOOP press (matched against the deck's Play objects), so no
  memory scan runs; 0.0.6's scan slowed the deck's filters on the real hardware.
- The mod's own loop-exit calls never count as learning.
- A Play object is only accepted if the full set of four decks checks out.
- The log's FILTERS lines report the longest fader-start action time (`act_max_us`).

Earlier test builds: 0.0.6 learned decks from PLAY/STOP without DECK SELECT and fixed a false disarm on
slow fader moves; 0.0.5 read the loop state with the deck's own has-loop check; 0.0.4 fixed the
fader position being read as an integer instead of a float.

## Testing

- **Real deck:** JC tested this package on his XDJ-AZ (firmware 2.00/2.01, AZ-MODS 1.2.9) on 15 Nov 2026.
- **Emulator:** passed the AZ-EMU test script.
- **Host tests (not the deck):** `make test-harness` (plain, ASan+UBSan, TSan) on the test-only
  `AZ_HARNESS` build, plus `test/unit_test.c` and `test/unit_test_007.c` under ASan/UBSan.

## Known limits

- Firmware 2.00/2.01 only.
- Each deck must see one PLAY press (and one LOOP press for loop exit) after every mod load.
- Not compatible with automix while automix hooks Play.
