# active-loop 0.2.17

Menu only: every setting is grouped under one "Active Loop" heading in the Mod Menu
(AZ_ROW_GROUP key `settings`), the same pattern as tempo-range 0.1.17. No playback,
arming, colour, pulse or logging behaviour is changed.

Started from the published 0.2.16 source in johnnycoombes/az-mods (commit
1e08c45682e03ec4c662fbcb76ca1a6c0c9f8adb). `/workspace/mods/active-loop` is 0.2.15.
`/workspace/mods/active-loop-0.2.16-overview` matches the published C, Makefile,
azmod.json, tests, LICENSE and icon; its README does not (the published README is used).

## Before (0.2.16)

Left-list rows (runtime `register_row`; azmod.json has no settings block):

- **Active Loop** (key `enable`, toggle, default on). Right-pane OPTIONS children:
  - **Auto-advance** (key `auto-advance`, default on, parent `enable`)
  - **Re-arm** (key `rearm`, default on, parent `enable`)
  - **Pulse armed pad** (key `pulse`, default on, parent `enable`)
  - **Pop-up messages** (key `popups`, default on, parent `enable`)
- **Pulse overview marker** (key `overview-pulse`, toggle, default off, parent none).
  Top-level because a fifth OPTIONS child overflowed the right pane and shared a tap
  with Pop-up messages. Grayed while Active Loop is off (`available()`).

A help page (register_screen `on_draw`, "Active Loop Picker" combo list) is not a
settings heading. Combo / pad-arm / clear have no rows (hardcoded on since 0.2.1).
There is no logging/debug row.

## After (0.2.17)

Left list: one heading **Active Loop** (key `settings`, group, no value, not saved).

Right pane, in this order:

1. **Active Loop** (`enable`, default on)
2. **Auto-advance** (`auto-advance`, default on, parent `enable`)
3. **Re-arm** (`rearm`, default on, parent `enable`)
4. **Pulse armed pad** (`pulse`, default on, parent `enable`)
5. **Pulse overview marker** (`overview-pulse`, default off, parent `enable`)
6. **Pop-up messages** (`popups`, default on, parent `enable`)

Children stay parented to the enable toggle, so they remain grayed while it is off
and `row_value` still reads 1 only when enable is on. Existing `active-loop:<key>`
lines in `MODS/azmods.cfg` still apply. If the group cannot be registered, the
0.2.16 layout is kept.

The help page is unchanged.

Tests (host, 20 Nov 2026, Europe/London): all tree unit suites (including led_test,
ovp_gate_test, filter_fuzz) passed under gcc -O1, ASan+UBSan and TSan. Harness
`sanitize.sh` on test/harness_build.c passed plain/asan/tsan (gcc 14; two pre-existing
0.2.16 warnings, unused-function and format-truncation, are errors under the harness
-Werror and were left as warnings; clang, the intended compiler, is not on the box).
`make test` on active-loop.c still fails the stock harness fw_tags check, as in 0.2.16;
that is why test-harness exists. azmod-pack manifest/icon/LICENSE checks of azmod.json
passed; full `azmod-pack validate` needs an arm64 .so, which was not built (no cross
compiler on the box).
