# Custom Tempo Range (`tempo-range`)

Maps the non-WIDE TEMPO RANGE slots ±6% and ±10% to ±4% and ±8% by default, so the
TEMPO RANGE cycle is 4% → 8% → 16% → WIDE. Each of the two slots has an on/off row in the
Mod Menu; off gives the firmware's stock range back. ±16% and WIDE (±100%) stay stock.
The on-screen range label is green for ±4 and orange for ±8.

**This mod patches EP147.** It declares `"hooks": true` and only loads on
firmware tag `ep147-9e9454e357389ece` (stock EP147 for XDJ-AZ 2.00 and 2.01).

## 0.1.21 - what changed since the published 0.1.16

The published 0.1.16 release was the 0.1.15 code with the BETA flag removed. The development
builds numbered 0.1.16 to 0.1.19 below were never released on their own; 0.1.20 includes all of them.

- **0.1.21: BETA flag removed.** 0.1.21 is 0.1.20 with `"beta": true` removed from the manifest;
  only the version and the startup log line (`loaded 0.1.21`) change. The code is functionally
  identical (the `.so` differs only in its ELF build-id and that log digit).

- **0.1.20: colour-coded range label.** The ±NN label background is green `#00C853` for ±4 and
  orange `#FF8C00` for ±8, with black text on both, on every theme. ±16 and WIDE are stock, so the
  label never draws for them.
- **0.1.19: range slots are on/off rows (bug fix).** Before, every slot row had the same
  `std/4/8/16` list, so switching the ±10 slot to `std` and back gave ±4% and the cycle became
  4, 4, 16, WIDE. Now each row has one target: **+/-6% becomes +/-4%** (key `use4`) and
  **+/-10% becomes +/-8%** (key `use8`), both on by default, so a fresh start is 4/8/16. The ±16
  row is gone because ±16 is the same as stock. **The old `pct6`/`pct10`/`pct16` settings are
  ignored**: if you had switched a slot off, switch it off again in the Mod Menu.
- **0.1.18: ±8% moves in 0.02% steps.** 0.1.16 and 0.1.17 tried 0.01% steps for ±8%; on the
  deck the fader skipped values unevenly. ±4% keeps 0.01% steps and ±16% 0.05%.
- **0.1.17: one Mod Menu group.** Every setting (range slots, range label, BPM with 2 decimals,
  4-deck view, label outline) sits under one **Custom Tempo Range** group.
- **0.1.16: finer ±8%.** ±8% was quantised to 0.01% instead of 0.05% (changed again in 0.1.18).

Tested by JC on his XDJ-AZ on 15 Nov 2026 (firmware 2.00/2.01, AZ-MODS 1.2.9): the deck-tested build is 0.1.20.

## Conflicts

**bpm2dp**: tempo-range 0.1.11+ draws its own two-decimal BPM box over the stock BPM digits,
the same place the older `bpm2dp` mod draws. Running both overlaps the digits. Turn bpm2dp off
(`community-mods.bpm2dp = off` in `MODS/azmods.cfg`, or Disable it under Community Mods) when
tempo-range is on.

## Known limits (read before a gig)

- **4-deck view: labels *or* BPM, not both.** A mod may draw only 4 overlays. In the 2-deck view
  that is 2 ±NN range labels + 2 BPM boxes. In the 4-deck and 2/4-deck views the four overlays
  are one per deck, so the Mod Menu row **4-deck view shows** picks either `range label`
  (default) or `BPM 125.00`. The other one is not shown in those views.
- **The master deck's digits stay white.** The stock BPM readout colours the master deck's
  digits, but the mod's two-decimal overlay can't tell which deck the master BPM widget belongs to,
  so it doesn't follow the master colour: digits are dark on Day, white on dark themes,
  for every deck including the master. Use the stock MASTER indicator to see which deck is master.
- **Conflicts with bpm2dp.** Both draw a BPM box over the same stock digits and overlap.
  Turn bpm2dp off (`community-mods.bpm2dp = off` in `MODS/azmods.cfg`, or Disable it under
  Community Mods) while tempo-range is on. The mod logs `bpm2dp=1` at load if it sees it on.
- The printed pitch scale still shows 6/10/16/100. A custom range is numerically correct, but the
  printed ticks don't match it.
- The jog-screen BPM (drawn by the deck's MCU) keeps one decimal.

## What it does

- `claim_hook` on `TempoCtl::recompute` at VA `0x149afe8`.
- Calls the original, then for range enum 0/1/2 rewrites:
  - `TempoControl+0x14` — smooth pitch (meter / % readout)
  - `TempoControl+0x18` — quantised pitch (what playback uses)
- Re-calls `ST_SET_TEMPO` at `0x1525c80` with the new applied value so the
  player engine, Pro DJ Link status and SYNC followers see it.
- Quantiser keeps `N × step = mult`: step 0.0001 (0.01%) for ±4%, 0.0002 (0.02%) for ±8%,
  0.0005 (0.05%) for ±16%. Values are clamped to ±1.0 (the firmware's hard ceiling).
- Two Mod Menu on/off rows (`use4`, `use8`, default on) under the **Custom Tempo Range**
  group; off leaves that slot stock. Settings follow the USB (`MODS/azmods.cfg`).

## What it does not do

- Does **not** change WIDE.
- Does **not** redraw the pitch-scale artwork — ticks stay at the stock
  6/10/16/100 marks, so a custom range is numerically right but the printed
  scale will not match.
- Does **not** add a fifth range (the MYSETTING enum and scale art are fixed).

## BPM with two decimals (0.1.11)

Stock EP147 shows the deck BPM as `125.0`: the readout is four PNG digit glyphs
(`HHH.T`), not text. `BpmWidgetBase`'s update, the function at `0x1f185b8` on 2.00, reads the
int BPM x100 at `[widget+0x90]+0xa8` and sets hundreds/tens/ones and
`(int)((x100 % 100) * 0.1)` (truncated tenths) through vtable slots
`+0x68/+0x70/+0x78/+0x88` (setters `0x1ecf288`.. in `Simple2DeckBpmWidget`).
There is no format string and no fifth glyph, so this is stock behaviour,
not caused by the tempo rescale (the quantiser is 0.01-0.05 % per step, finer
than 0.1 BPM).

0.1.11 draws the BPM to 0.01 (`125.00`, from the core's `deck_bpm()`, rounded
once) in an opaque overlay over the stock digits. No new hook: only
`TempoCtl::recompute` stays hooked.

- Mod Menu **BPM with 2 decimals** (default on).
- A mod may have 4 overlays. In the 2-deck view, overlays 1-2 are the +/-NN
  labels and 3-4 are the BPM for decks 1-2. In the 4-deck view, **4-deck view
  shows** picks `range label` (default) or `BPM 125.00`.
- The text shrinks until `400.00` fits the box. The box defaults come from
  bpm2dp's emulator measurements (2-deck 512/1152,637 115x40 px 34; 4-deck
  537/1177,580/700 90x32 px 26). Override them in the mod's cfg with
  `b2_x1`, `b2_y1`, ... `b2_w`, `b2_h`, `b2_p` (and `b4_*`). **Label outline**
  also frames the BPM box (cyan).
- BPM refreshes every 250 ms. No track or no BPM: the overlay hides and the
  stock digits show.
- The jog screens are drawn by the deck MCU, which gets no BPM text field from
  EP147, so they can't be changed here.

## Decks 3 and 4 (0.1.12)

- EP147 builds four `DjPlayerUnit`s (the function at `0x7f0538` calls the function at `0x7f0068(slot, 0..3)`), each
  with its own TempoControl (owner ctor `0x149a698`, from `0x156a2d8` in the unit ctor
  `0x155d9f0`). The range is per TempoControl (`+0x24`, set by `0x149b240`), so every deck
  keeps its own range and the hook rescales whichever deck it runs for. WIDE (enum 3) is left
  alone on all four.
- Deck identity: the player id (low byte = deck) is read from `[[X+0x38]+0x28]+0x10`
  (X = TempoControl owner, vtable `0x2bdfac0`; PlayerBasisUnit vtable `0x2bf2268`; id holder
  vtable `0x2b04fa0`, ctor `0xe18b08`), vtable-checked at each step. The old BPM-correlation
  and seen-order mapping is only the fallback. Log: `deck map: ... via player-id(pbu+0x28)`.
- Layers: decks 1/3 share the left controls and 2/4 the right. The slider only reaches
  recompute while TempoControl `+0x28` (fader live) is set (`0x149b128`); it is cleared by
  `0x149b2b0` / `0x149b308`. So the deck whose recompute calls advance is that side's active
  layer. In the 2-deck view the label and BPM boxes follow it (`layer: side L -> deck 3`).
- Overlays are filled #DCDCDC on Day and #000000 otherwise (sampled), with white digits on dark.
- Master digits stay white: the master flag is in the BPM widget (`[w+0xc0]+0x620`, read by
  the function at `0x1f185b8`), but that widget has no deck field this mod can confirm, so it is not hooked.

## Build / test

```sh
azmod-pack build tempo-range
azmod-pack validate tempo-range
```

`make test` (the stock host harness) expects `"firmware": ["any"]` and no `claim_hook`, so the
shipped build can't pass it. `make test-harness` runs the same harness (plain, ASan+UBSan,
TSan) on `test/harness_build.c`, a **test-only** build with `-DAZ_HARNESS` (fw_tags `{"any"}`,
no hooks or filters); this build is never packaged. The hook body is covered by `test/sel_test.c`,
the emulator and a real deck.

First instructions at `0x149afe8` are `stp`/`mov`/`str`/`mov` — all movable,
so `claim_hook` should accept the address. Confirm in the mod log after load:
`hooked recompute; pct=…`.

## Test on the emulator or deck

1. Confirm the firmware tag under **Community Mods → Repositories** is
   `ep147-9e9454e357389ece`. Any other tag: the mod stays off.
2. Install / push the mod. Check `/tmp/azmod-community/tempo-range.log` for
   `hooked recompute`. A refused claim shows the reason there.
3. Mod Menu → **Custom Tempo Range**: leave **+/-6% becomes +/-4%** and
   **+/-10% becomes +/-8%** on.
4. On a loaded deck, press TEMPO RANGE: the cycle is 4% (green label) → 8% (orange
   label) → 16% → WIDE. Move the pitch fader to the end in the 4% slot: the % readout
   and the track's BPM should reach about ±4%, not ±6%. Switch a row off and on
   again: the cycle stays 4/8/16.
5. Switch to WIDE: behaviour must be unchanged (±100%).
6. Turn SYNC on: pitch must snap to 0 (the sync flag at `+0x20` still wins).
7. Link: a second player SYNCing to this deck should follow the new
   tempo-adjusted BPM.
8. Power-cycle: the CHOICE rows restore from the USB; move the fader once to
   re-apply (the hook runs on the next `recompute`).

## Risks

- **Wrong address on another firmware** → crash or silent corruption. The
  core blocks the mod on an unlisted tag; do not add a tag you have not
  re-found addresses on.
- **Quantiser mismatch** if `N×step ≠ mult` — this mod keeps them equal.
- **Scale art mismatch** — cosmetic, but easy to misread on stage.
- **Link / SYNC** — followers track the new pitch; that is intended.
- Tested on JC's XDJ-AZ (fw 2.00/2.01, AZ-MODS 1.2.9) for 0.1.20 on 15 Nov 2026 (0.1.21 is the same code without the BETA flag); 0.1.15 was also run on the emulator.

## If playback does not follow

The rewrite of `+0x14`/`+0x18` plus a second `ST_SET_TEMPO` matches the
original tail-call path. If the % readout moves but audio does not:

1. Confirm `ST_SET_TEMPO` is still at `0x1525c80` and that
   `TempoControl+0x08 → parent+0x68` is still the state object.
2. Check whether an intermediate call at `0x152cfc8` (made by `recompute`
   before `ST_SET_TEMPO`) also needs the new value — it was left to the
   original on purpose.
3. Fallback: hook `setTempoRange` (`0x149b240`) only to force a recompute
   after a range change, or patch the `mult` immediates in place (see
   `EP147-TEMPO-RANGE.md` §1.7).
