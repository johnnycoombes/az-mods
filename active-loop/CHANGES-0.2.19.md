# active-loop 0.2.19

Fix: the **Jog shows** row now reaches all four values (Off, Time, Bar,
Time + Bar). On AZ-MODS 1.2.5 a 4-way `AZ_ROW_CHOICE` inside an `AZ_ROW_GROUP`
pane is drawn as a checkbox (`c_draw_group`) while hits still use the invisible
bank-button rects. A knob click from the default (3) went to Off (one cycle
step); a tap on the checkbox landed on the rightmost bank (Time + Bar). Time
only and Bar only could not be chosen, and the current value was not shown.

0.2.19 drops the unused `settings` GROUP (it had no saved value). **Active Loop**
is again the top-level enable TOGGLE, as in 0.2.16 and miniwave. Its OPTIONS pane
draws the four banks (lit button is the current value). A tap picks a value; the
knob cycles Off → Time → Bar → Time + Bar → Off. Key `jog-shows` and default 3
(Time + Bar) are unchanged; children stay parented to `enable`. 1.2.5 OPTIONS
panes scroll, so the 0.2.17 overflow reason no longer applies.

Also: `azmod.json` description shortened to 596 bytes (the deck truncates at
599; 0.2.18 was 733). The 30 s timing / summary line now always includes
`evdrop` / session (the hook-event ring already existed; 0.2.18 only logged
drops when the count was non-zero, so AZ-EMU saw no evdrop at all).

Saved-loop auto-arm and the frame-filter trip (0.2.18 steps 11/12) are
pre-existing and were not changed.

Tests (host, 21 Nov 2026, Europe/London): `test/jog_test.c` (format, pickups,
throttle, restore, JPEG, and Jog shows cycle Off→Time→Bar→Time+Bar→Off) plus
the 0.2.17 unit suites under gcc -O1, ASan+UBSan and TSan. SDK harness
`sanitize.sh` on `test/harness_build.c` plain/asan/tsan. aarch64-linux-gnu-gcc
-Wall -Werror (plus -Wno-error=format-truncation for the known overview lines).
