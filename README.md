# JC's AZ Mods

Community mods for the Pioneer DJ XDJ-AZ running [AZ-MODS](https://github.com/Kyle-Hosman/xdj-az-mods).
Built with the [AZ-MODS SDK](https://github.com/Kyle-Hosman/az-mods-sdk) and written to its
[Guidelines](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/GUIDELINES.md).
Every mod requires **AZ-MODS 1.2.3 or newer** (`min_core` in each manifest).

| Mod | Latest | Folder | What it changes |
|---|---|---|---|
| Active Loop Picker (`active-loop`) | [0.2.16](https://github.com/johnnycoombes/az-mods/releases/tag/active-loop-0.2.16) (7 Nov 2026) | [active-loop/](active-loop/) ([README](active-loop/README.md)) | Arms the rekordbox Active Loop (or the next stored loop) at track load so it engages when the playhead reaches it; SHIFT+MEMORY jumps to the next loop; SHIFT+pad arms a hot cue loop as the Active Loop and saves it (red pad) through the deck's own software, DELETE+pad deletes a loop (one SHIFT hold per DELETE hold); optional Auto-advance; **Pop-up messages** (on by default). **Pulse overview marker** (off by default, menu only) breathes the overview marker at the current Active Memory or Cue Loop on decks 1–4, including layer switches 1/3 and 2/4. Reads the stick's analysis files read-only and writes no file itself. **Patches the deck software's code.** |
| Custom Tempo Range (`tempo-range`) | [0.1.21](https://github.com/johnnycoombes/az-mods/releases/tag/tempo-range-0.1.21) (15 Nov 2026) | [tempo-range/](tempo-range/) ([README](tempo-range/README.md)) | Remaps the TEMPO RANGE slots ±6/10 to ±4/8 (±16 and WIDE stay stock), so the cycle is 4/8/16/WIDE on all four decks; each slot has an on/off row under one **Custom Tempo Range** Mod Menu group. Draws the BPM with two decimals and a colour-coded range label (green ±4, orange ±8). **Patches the deck software's code.** Turn the `bpm2dp` mod off while it is on. |
| Fader Start (`fader-start`) **BETA** | [0.0.7](https://github.com/johnnycoombes/az-mods/releases/tag/fader-start-0.0.7) (15 Nov 2026) | [fader-start/](fader-start/) ([README](fader-start/README.md)) | Channel fader start: bringing a closed channel fader up plays its deck, exiting a loop first (never reloops). **Off by default**: install, press **Allow**, switch the **Fader Start** row on, press PLAY once per deck to teach it, and press LOOP once to teach the loop exit. Not while automix hooks Play. **Patches the deck software's behaviour.** |

All mods support **firmware 2.00/2.01 only** (`ep147-9e9454e357389ece`); the deck
will not load them on any other firmware.

## Latest updates

- **15 Nov 2026 — [Fader Start 0.0.7](https://github.com/johnnycoombes/az-mods/releases/tag/fader-start-0.0.7) (BETA).** First published release. Channel fader start, off by default. Learns each deck from a PLAY press and the loop exit from a LOOP press (no memory scan; 0.0.6's scan slowed the deck's filters). Never reloops. Not while automix hooks Play. Its manifest carries `"beta": true` (it is not a GitHub prerelease).
- **15 Nov 2026 — [Custom Tempo Range 0.1.21](https://github.com/johnnycoombes/az-mods/releases/tag/tempo-range-0.1.21).** Removes the BETA flag from [0.1.20](https://github.com/johnnycoombes/az-mods/releases/tag/tempo-range-0.1.20); the code is the same (0.1.20 is the deck-tested build). 0.1.20 added colour-coded range labels (green ±4, orange ±8), on/off rows for the ±6→±4 and ±10→±8 slots, ±8% in 0.02% steps, and one Mod Menu group. The old `pct6`/`pct10`/`pct16` settings are ignored: if a slot had been switched off, switch the new row off again.
- **7 Nov 2026 — [Active Loop Picker 0.2.16](https://github.com/johnnycoombes/az-mods/releases/tag/active-loop-0.2.16).** **Pulse overview marker** (off by default, menu only) breathes the overview marker at the current Active Memory or Cue Loop on decks 1–4, including layer switches 1/3 and 2/4.
- **3 Nov 2026 — [Active Loop Picker 0.2.15](https://github.com/johnnycoombes/az-mods/releases/tag/active-loop-0.2.15).** One SHIFT hold per DELETE hold, so the deck no longer sticks in SHIFT mode after deleting pads; **Pop-up messages** row (on by default). A DELETE tap within 0.3 s of a DELETE+pad is not passed on.

Earlier releases are on the [Releases](https://github.com/johnnycoombes/az-mods/releases) page.

## Install

This repository is listed in the official AZ-MODS community index,
[Kyle-Hosman/az-mods-index](https://github.com/Kyle-Hosman/az-mods-index), so every XDJ-AZ running
AZ-MODS shows it by default, with nothing to add. On the deck: **Mod Menu > Community Mods**, find
**johnnycoombes/az-mods** under **Repositories**, pick a mod in **Browse**, install it, press
**Allow** if the deck asks about a new build, then **Reload now**. The index only lists the
repository; the mods themselves come from this repository's GitHub releases.

Mods run with full access to the deck: install what you trust, and keep a spare stick for trying new mods.

## Build

```sh
azmod-pack build active-loop      # or tempo-range, fader-start
make -C active-loop test AZ_SDK=/path/to/az-mods-sdk
```

Releases are built with `azmod-pack build` in the SDK container (`az-mods-sdk:1`) and attached to a GitHub release by hand.

## License

GPL-3.0-only (see `LICENSE` and each mod's own `LICENSE`). This repository contains no firmware,
update files, keys, decompiled code or deck artwork. AZ-MODS and these mods are community
projects, not made, supported or endorsed by Pioneer DJ or AlphaTheta.
