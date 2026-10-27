# JC's AZ Mods

Community mods for the Pioneer DJ XDJ-AZ running [AZ-MODS](https://github.com/Kyle-Hosman/xdj-az-mods).
Built with the [AZ-MODS SDK](https://github.com/Kyle-Hosman/az-mods-sdk) v1.2.3 and written to its
[Guidelines](https://github.com/Kyle-Hosman/az-mods-sdk/blob/main/docs/GUIDELINES.md).

| Mod | Folder | What it changes |
|---|---|---|
| Active Loop Picker (`active-loop`) | [active-loop/](active-loop/) | Arms the rekordbox Active Loop (or the next stored loop) at track load so it engages when the playhead reaches it; SHIFT+MEMORY cycles loops; optional Auto-advance. Reads the stick's analysis files, never writes them. **Patches the deck software's code.** |
| Custom Tempo Range (`tempo-range`) | [tempo-range/](tempo-range/) | Remaps the TEMPO RANGE slots ±6/10/16 to ±4/8/16 (WIDE stays ±100%) on all four decks and draws the BPM with two decimals. **Patches the deck software's code.** Turn the `bpm2dp` mod off while it is on. |

Both mods are **BETA** and support **firmware 2.00/2.01 only** (`ep147-9e9454e357389ece`); the deck
will not load them on any other firmware.

## Install

On the deck: **Mod Menu > Community Mods > Repositories > Add**, enter `johnnycoombes/az-mods`,
then install from **Browse** and press **Reload now**.

This repository is not (yet) in the Mod Index, so its mods show "Not in the Mod Index". Mods run
with full access to the deck: install what you trust, and keep a spare stick for trying new mods.

## Build

```sh
azmod-pack build active-loop      # or tempo-range
make -C active-loop test AZ_SDK=/path/to/az-mods-sdk
```

Releases are built by `.github/workflows/release-<id>.yml` when a tag `<id>-<version>` is pushed.

## License

GPL-3.0-only (see `LICENSE` and each mod's own `LICENSE`). This repository contains no firmware,
update files, keys, decompiled code or deck artwork. AZ-MODS and these mods are community
projects, not made, supported or endorsed by Pioneer DJ or AlphaTheta.
