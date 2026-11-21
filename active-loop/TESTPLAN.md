# Active Loop 0.2.19: AZ-EMU test plan

Firmware 2.00/2.01 only (`ep147-9e9454e357389ece`). Per-mod logging on (for JOG lines).
Do not write to the USB library or set a new loop that would save ANLZ: CALL an
already-saved loop, or arm one that this mod already saved. 0.2.17 combos stay
the same. Saved-loop auto-arm is pre-existing (0.2.18 steps 11/12: the frame
filter can trip off after ~3 s / 200 µs ×8, and a track with no usable saved
loop will not auto-arm); do not treat that as a 0.2.19 regression and do not
change it.

The jog image may be visible on the emulator's jog if it renders the artwork JPEG.
If the jog centre is blank or not drawn, decode the sent JPEG from the UCOM trace
(message 0x33 artwork pieces, 58 B, 3-byte LE length prefix) to a PNG and attach
that instead of a screenshot.

0.2.19 menu: **Active Loop** is the top-level enable TOGGLE (no `settings` GROUP).
All previous rows plus **Jog shows** still sit under that one heading, in the
OPTIONS pane, which can scroll. **Jog shows** is a 4-way bank: Off | Time | Bar |
Time + Bar (lit button is the current value). Tap a button to pick that value;
knob-click cycles Off → Time → Bar → Time + Bar → Off. Key `jog-shows`, default 3.

| # | Action | Expect | Notes |
|---|---|---|---|
| 1 | Boot with Active Loop 0.2.19 enabled, Jog shows = Time + Bar (default). Idle 30 s, nothing loaded. | `loaded 0.2.19`; jog hooks claimed (lambda, encode, hidart, isRemain, widget). The 30 s timing line has `evdrop 0` (session 0). No JOG send lines. Jog looks stock (empty). | fail: crash, hooks refused for lambda+encode without a log that the display is off, or evdrop climbing |
| 2 | Load a track **with artwork** (e.g. Cola) on deck 1. Wait 5 s, do not arm a loop yet. | EP147 artwork appears on the left jog. No JOG send yet. | first loop-image send must wait for this artwork + 2.5 s. A rekordbox Active Loop may already auto-arm (pre-existing; note it, do not "fix") |
| 3 | CALL an already-saved loop (or arm the track's existing Active Loop) on deck 1. Wait until 2.5 s after the artwork send. | Jog centre becomes the static image: red **ACTIVE LOOP** caption, BAR and TIME or REMAIN of the **in-point** (not the playhead). One `JOG d1 send "…"` line. | screenshot or decoded PNG of the jog |
| 4 | Let the track play for 15 s, pause. | Image **does not** update (it is not live). No extra JOG send for playback alone. | |
| 5 | Tap TIME/REMAIN on deck 1 once. | Image updates **once** to the other mode (the bright word and the number). One new JOG send, same bar. | |
| 6 | Jog shows → **Time**. Tap the **Time** bank button (or knob-click from Off, or from Time + Bar click twice: Off then Time). Confirm the lit button is Time and the other three are not. | Image is Time-only (no big bar). One send. Log `row Jog shows -> Time`. cfg `active-loop:jog-shows = 1`. | 0.2.18 could not reach 1 (group checkbox / invisible bank). Must visit this value, not skip to Time + Bar |
| 7 | Jog shows → **Bar**. Tap the **Bar** bank button (or one more knob-click from Time). Confirm the lit button is Bar. | Image is Bar-only (`65.1` style, or Time layout if this track has no usable grid). One send. Log `row Jog shows -> Bar`. cfg `= 2`. | must visit this value |
| 8 | Jog shows → **Time + Bar** (tap that bank, or one more knob-click from Bar). | Combined layout again. Lit button is Time + Bar. One send. cfg `= 3`. | |
| 9 | Jog shows → **Off** (tap Off, or one more knob-click from Time + Bar). | Artwork restored. `JOG d1 restore artwork` (or `blank` only if the cache is empty). Lit button is Off. cfg `= 0`. | |
| 10 | Jog shows → Time + Bar again (tap, or knob-cycle Off → Time → Bar → Time + Bar). | Loop image returns (after the 1 s rate limit). cfg ends at 3 unless later steps change it. | |
| 11 | Clear the Active Loop (SHIFT+DELETE, or SHIFT+the armed pad). | Artwork restored. `JOG d1 restore …`. Combos otherwise as in 0.2.17. | if the frame filter has already tripped off (pre-existing toast “SHIFT combos off until restart”), note it and skip the combo; do not change auto-arm / filter behaviour |
| 12 | Load a **no-artwork** track (Easy, Closer, Good Life or Been A Long Time, artwork_id=0) on deck 2, arm a saved loop, wait 2.5 s after load. | Loop image appears (no artwork send to wait for). Restore on clear is `blank` if +0xf8 is empty. | do not alter the library. If Easy has no usable saved loop and does not auto-arm, that is pre-existing; note it and do not write ANLZ |
| 13 | Utility: Image on jog display → **Custom**. Arm a loop (any deck that has a custom image, or all 4). | The loop image is **not** sent; Custom (or stock empty) stays. No JOG send that would override. | as in jogprobe 0.0.3, Custom may have img=null and no ARTSEND |
| 14 | Switch back to Artwork, wait 2.5 s after EP147's artwork send if one happens, with a loop still armed. | Loop image may return. Custom was left alone while it was on. | |
| 15 | Repeat a load + arm + image on **all four decks** (1 and 3 left bus, 2 and 4 right bus). Switch 1↔3 and 2↔4. | Each deck shows its own loop image when selected. Mapping as jogprobe: collectors deck+0xd4 0/1/2/3, view8 1/2/3/4. | 0.2.18 skipped this (time / filter already off). Do not skip if at all possible; say so if a pre-existing filter trip blocks a deck |
| 16 | Reboot the emulator (full stop and start, not only Reload). Before reboot leave Jog shows at a non-default value if you can (e.g. Time = 1), then confirm after reboot; also confirm a reboot with default 3 still comes back as Time + Bar. Load and arm. | cfg `active-loop:jog-shows` persists (the lit bank matches the saved index). Feature works after reboot. | 0.2.18 skipped this (no reboot proof). Must reboot, not only read the on-disk cfg |
| 17 | Idle 30 s with a loop image up, nothing changing. | No repeated JOG lines. The 30 s timing line shows `evdrop 0` (session stays 0 unless earlier drops were already counted). No flicker, no extra lag on cues/waveform after the 2.5 s wait. | |
| 18 | UCOM art (0x33) count around a send. | One burst of pieces ≈ the logged `pieces=` (ceil((jpeg+3)/58)), then quiet. A restore is a second burst of the artwork size. | compare with the JOG line |

Fail and stop if: a crash or freeze; the 0.2.17 combos misbehave; Custom is overwritten; a loop image is sent while Jog shows is Off; the image ticks with the playhead; ANLZ or the library is written; a send happens immediately on load on top of the artwork (no 2.5 s wait); evdrop climbs while idle; or Jog shows cannot visit all four values (Off, Time, Bar, Time + Bar) by tap or knob.

The azmod.json description must remain under 599 bytes (the deck truncates at 599).
