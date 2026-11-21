# active-loop 0.2.18

Jog display: when a deck has a current Active Loop, its jog centre can show a
static 181×181 image (red ACTIVE LOOP caption + in-point Time and/or Bar) in
place of the artwork. A new Mod Menu row **Jog shows** (Off / Time / Bar /
Time + Bar, default Time + Bar) sits last in the existing single **Active Loop**
group. Off is stock behaviour. 0.2.17 combos, arming, pad pulse, overview
marker and pop-ups are unchanged; the mod id stays `active-loop`.

The image is never live. It is sent only when the shown text changes (loop set,
cleared or advanced; TIME/REMAIN; track or grid). Time follows TimeRemainState
+0xb0 (isRemain 0x212f1a0). REMAIN uses the time-widget length at +0xe8. Bar is
bar.beat from the grid via LoadState at time widget +0x130 then +0x180
(vtables + 'XOCR', as validated by jogprobe 0.0.3 on AZ-EMU). Bars start at 1.1.

Sending uses EP147's own path: the ArtWorkCollect worker (0x21672f8) drops the
queue (0x22f58e8) and calls setArtwork (0x22f7b08); the encode hook (0x22f8e88)
substitutes the mod JPEG. The encode entry is a top-level asm stub that saves
the MemoryBlock sret pointer in x8 before any C prologue (same pattern as
jogprobe 0.0.3). The first send waits for EP147's own artwork send plus 2.5 s
(or 2.5 s after load if there is no artwork), then at most one send per second
per deck. Custom mode (+0x118) is not overridden. Clearing the loop or switching
Off restores the cached image at +0xf8. Unknown pointers fall back to stock.
No USB or ANLZ writes.

Logging is quiet: one JOG line per send or restore (deck, text, bytes, pieces, µs).

Tests (host, 21 Nov 2026, Europe/London): `test/jog_test.c` (format, pickups,
throttle, restore, JPEG) plus the 0.2.17 unit suites passed under gcc -O1,
ASan+UBSan and TSan. SDK harness `sanitize.sh` on `test/harness_build.c`
passed plain/asan/tsan (gcc 14; the same pre-existing format-truncation
warnings in overview log lines as 0.2.17). aarch64-linux-gnu-gcc -Wall -Werror
(plus -Wno-error=format-truncation for those known lines) with the SDK flags
built cleanly; objdump of `my_jog_encode` shows `str x8` before any call, then
x0/x1/x8 restored for the original encode.
