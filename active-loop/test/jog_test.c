/* TEST-ONLY: host unit tests for 0.2.19 jog format, bar.beat (incl. pickups), throttle, restore and Jog shows cycling. */
#include "../jog_display.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static void test_time(void)
{
    char b[16], s[8];
    assert(jog_fmt_time(192640, b, sizeof b, s, sizeof s) == 0);
    assert(strcmp(b, "03:12") == 0 && strcmp(s, ".640") == 0);
    assert(jog_value_ms(0, 192640, 415720) == 192640);
    assert(jog_value_ms(1, 192640, 415000) == 222360);
    assert(jog_fmt_time(jog_value_ms(1, 192640, 415000), b, sizeof b, s, sizeof s) == 0);
    assert(strcmp(b, "03:42") == 0 && strcmp(s, ".360") == 0);
    assert(jog_fmt_time(jog_value_ms(1, 192640, 415720), b, sizeof b, s, sizeof s) == 0);
    assert(strcmp(b, "03:43") == 0 && strcmp(s, ".080") == 0);
    assert(jog_fmt_time(-1, b, sizeof b, s, sizeof s) == -1);
    assert(strcmp(b, "--:--") == 0 && strcmp(s, ".---") == 0);
    assert(jog_value_ms(1, 192640, -1) == -1);
    assert(jog_value_ms(1, 500, 400) == -1);
    assert(jog_fmt_time(0, b, sizeof b, s, sizeof s) == 0);
    assert(strcmp(b, "00:00") == 0 && strcmp(s, ".000") == 0);
    assert(jog_fmt_time(100 * 60000, b, sizeof b, s, sizeof s) == 0);
    assert(strcmp(b, "100:00") == 0);
}

static void test_bar(void)
{
    /* 10 beats, downbeats at 2 and 6: first downbeat is beat index 2 → 1.1 */
    struct jog_gcell c[10];
    int32_t db[3] = { 2, 6, 10 };
    int bar, beat;
    for (int i = 0; i < 10; i++) { c[i].pos = i * 1000; c[i].bpm = 120.0; }
    assert(jog_bar_beat(db, 2, 10, 2, &bar, &beat) == 0 && bar == 1 && beat == 1);
    assert(jog_bar_beat(db, 2, 10, 3, &bar, &beat) == 0 && bar == 1 && beat == 2);
    assert(jog_bar_beat(db, 2, 10, 6, &bar, &beat) == 0 && bar == 2 && beat == 1);
    /* pickups before the first downbeat: 0.4 0.3 0.2 0.1, then -1.4 */
    assert(jog_bar_beat(db, 2, 10, 1, &bar, &beat) == 0 && bar == 0 && beat == 4);
    assert(jog_bar_beat(db, 2, 10, 0, &bar, &beat) == 0 && bar == 0 && beat == 3);
    /* a grid whose first downbeat is beat 4: indices 3,2,1,0 → 0.4,0.3,0.2,0.1 */
    int32_t db4[1] = { 4 };
    assert(jog_bar_beat(db4, 1, 10, 3, &bar, &beat) == 0 && bar == 0 && beat == 4);
    assert(jog_bar_beat(db4, 1, 10, 2, &bar, &beat) == 0 && bar == 0 && beat == 3);
    assert(jog_bar_beat(db4, 1, 10, 1, &bar, &beat) == 0 && bar == 0 && beat == 2);
    assert(jog_bar_beat(db4, 1, 10, 0, &bar, &beat) == 0 && bar == 0 && beat == 1);
    /* two pickup beats (first downbeat #2): i=0 is 0.3? wait db0=2: i=1 → 0.4, i=0 → 0.3 */
    int32_t db2[1] = { 2 };
    assert(jog_bar_beat(db2, 1, 10, 1, &bar, &beat) == 0 && bar == 0 && beat == 4);
    assert(jog_bar_beat(db2, 1, 10, 0, &bar, &beat) == 0 && bar == 0 && beat == 3);
    char t[16];
    assert(jog_fmt_bar(0, 65, 1, t, sizeof t) == 0 && strcmp(t, "65.1") == 0);
    assert(jog_fmt_bar(0, 0, 3, t, sizeof t) == 0 && strcmp(t, "0.3") == 0);
    assert(jog_fmt_bar(-1, 0, 0, t, sizeof t) == -1 && strcmp(t, "--.-") == 0);
    /* nearest: a point a few samples before beat 2 must not fall to beat 1 */
    assert(jog_beat_near(c, 10, 2000) == 2);
    assert(jog_beat_near(c, 10, 2000 - 3) == 2);   /* 3 samples before, span 1000 */
    assert(jog_beat_near(c, 10, 2000 - 600) == 1); /* closer to beat 1 */
    assert(jog_bar_beat(NULL, 0, 10, 2, &bar, &beat) == -1);
}

static void test_compose(void)
{
    char t[80];
    jog_compose_text(JOGSHOW_TIME, 0, 192640, 415720, 0, 0, 0, t, sizeof t);
    assert(strcmp(t, "TIME 03:12.640") == 0);
    jog_compose_text(JOGSHOW_TIME, 1, 192640, 415000, 0, 0, 0, t, sizeof t);
    assert(strcmp(t, "REMAIN 03:42.360") == 0);
    jog_compose_text(JOGSHOW_BAR, 0, 192640, 415720, 1, 65, 1, t, sizeof t);
    assert(strcmp(t, "BAR 65.1") == 0);
    jog_compose_text(JOGSHOW_BOTH, 0, 192640, 415720, 1, 65, 1, t, sizeof t);
    assert(strcmp(t, "BAR 65.1 TIME 03:12.640") == 0);
    jog_compose_text(JOGSHOW_BOTH, 1, 192640, 415000, 1, 65, 1, t, sizeof t);
    assert(strcmp(t, "BAR 65.1 REMAIN 03:42.360") == 0);
    /* no grid: Bar and Time+Bar fall back to Time */
    jog_compose_text(JOGSHOW_BAR, 0, 192640, 415720, 0, 0, 0, t, sizeof t);
    assert(strcmp(t, "TIME 03:12.640") == 0);
    jog_compose_text(JOGSHOW_BOTH, 0, 192640, 415720, 0, 0, 0, t, sizeof t);
    assert(strcmp(t, "TIME 03:12.640") == 0);
    /* pickup */
    jog_compose_text(JOGSHOW_BAR, 0, 100, 415720, 1, 0, 3, t, sizeof t);
    assert(strcmp(t, "BAR 0.3") == 0);
}

static void test_plan(void)
{
    jog_st st; memset(&st, 0, sizeof st);
    jog_want w; memset(&w, 0, sizeof w);
    w.enabled = 1; w.show_mode = JOGSHOW_BOTH; w.have_loop = 1;
    snprintf(w.text, sizeof w.text, "BAR 65.1 TIME 03:12.640");
    /* no load yet: wait */
    assert(jog_decide(&st, &w, 1000) == JOG_ACT_WAIT);
    jog_note_load(&st, 0);
    assert(jog_decide(&st, &w, 1000000000ull) == JOG_ACT_WAIT);          /* 1 s after load, no art */
    assert(jog_decide(&st, &w, 2500000000ull) == JOG_ACT_SEND);           /* load + 2.5 s, no art (no-artwork track) */
    /* art path: wait art+2.5 even if load was earlier */
    memset(&st, 0, sizeof st);
    jog_note_load(&st, 0);
    jog_note_art(&st, 1000000000ull);
    assert(jog_decide(&st, &w, 3000000000ull) == JOG_ACT_WAIT);          /* 2 s after art */
    assert(jog_decide(&st, &w, 3500000000ull) == JOG_ACT_SEND);
    jog_apply_send(&st, w.text, 3500000000ull);
    assert(jog_decide(&st, &w, 6000000000ull) == JOG_ACT_NONE);          /* already showing this text */
    snprintf(w.text, sizeof w.text, "BAR 65.1 TIME 03:12.640"); /* same */
    /* TIME/REMAIN flip = new text */
    snprintf(w.text, sizeof w.text, "BAR 65.1 REMAIN 03:42.360");
    assert(jog_decide(&st, &w, 4000000000ull) == JOG_ACT_WAIT);          /* 0.5 s after last send */
    assert(jog_decide(&st, &w, 4500000000ull) == JOG_ACT_SEND);          /* 1 s */
    jog_apply_send(&st, w.text, 4500000000ull);
    /* clear loop: restore, also 1/s */
    w.have_loop = 0; w.text[0] = 0;
    assert(jog_decide(&st, &w, 5000000000ull) == JOG_ACT_WAIT);
    assert(jog_decide(&st, &w, 5500000000ull) == JOG_ACT_RESTORE);
    jog_apply_restore(&st, 5500000000ull);
    assert(jog_decide(&st, &w, 8000000000ull) == JOG_ACT_NONE);
    /* Off while showing */
    memset(&st, 0, sizeof st);
    jog_note_load(&st, 0); jog_note_art(&st, 0);
    w.have_loop = 1; w.show_mode = JOGSHOW_BOTH;
    snprintf(w.text, sizeof w.text, "TIME 03:12.640");
    assert(jog_decide(&st, &w, 2500000000ull) == JOG_ACT_SEND);
    jog_apply_send(&st, w.text, 2500000000ull);
    w.show_mode = JOGSHOW_OFF;
    assert(jog_decide(&st, &w, 3500000000ull) == JOG_ACT_RESTORE);
    /* Custom: never override, even with a loop */
    w.show_mode = JOGSHOW_BOTH; w.custom = 1;
    assert(jog_decide(&st, &w, 5000000000ull) == JOG_ACT_NONE);
    /* enable off */
    w.custom = 0; w.enabled = 0;
    jog_st st2 = st; st2.showing = 1;
    assert(jog_decide(&st2, &w, 6000000000ull) == JOG_ACT_RESTORE);
}

static void test_jpeg(void)
{
    uint8_t jpg[JOG_JPEG_CAP];
    int n = jog_render_jpeg(JOGSHOW_BOTH, 0, 192640, 415720, 1, 65, 1, jpg, sizeof jpg);
    assert(n > 1000 && n < 0x2b401);
    assert(jpg[0] == 0xff && jpg[1] == 0xd8);
    assert(jpg[n-2] == 0xff && jpg[n-1] == 0xd9);
    assert(jog_pieces(n) == (n + 3 + 57) / 58);
    /* 58-byte pieces include the 3-byte length prefix */
    assert(jog_pieces(0) == 1 || jog_pieces(0) == 0 || jog_pieces(55) == 1);
    assert(jog_pieces(55) == 1);
    assert(jog_pieces(56) == 2);   /* 56+3=59 → 2 pieces */
    /* TIME and Bar-only also encode, and a pickup 0.3 */
    int n2 = jog_render_jpeg(JOGSHOW_TIME, 1, 192640, 415000, 0, 0, 0, jpg, sizeof jpg);
    assert(n2 > 1000);
    int n3 = jog_render_jpeg(JOGSHOW_BAR, 0, 100, 415720, 1, 0, 3, jpg, sizeof jpg);
    assert(n3 > 1000);
}


static void test_jog_show_cycle(void)
{
    /* Off -> Time -> Bar -> Time + Bar -> Off */
    assert(jog_show_cycle(JOGSHOW_OFF) == JOGSHOW_TIME);
    assert(jog_show_cycle(JOGSHOW_TIME) == JOGSHOW_BAR);
    assert(jog_show_cycle(JOGSHOW_BAR) == JOGSHOW_BOTH);
    assert(jog_show_cycle(JOGSHOW_BOTH) == JOGSHOW_OFF);
    int v = JOGSHOW_BOTH; /* default 3 */
    const char *order[4] = { "Off", "Time", "Bar", "Time + Bar" };
    for (int i = 0; i < 4; i++) {
        v = jog_show_cycle(v);
        assert(v >= JOGSHOW_OFF && v <= JOGSHOW_BOTH);
        assert(strcmp(jog_show_label(v), order[i]) == 0);
    }
    assert(v == JOGSHOW_BOTH); /* full lap back to the default */
    assert(jog_show_cycle(-1) == JOGSHOW_OFF);   /* out of range treated as default 3, then Off */
    assert(jog_show_cycle(99) == JOGSHOW_OFF);
    assert(strcmp(jog_show_label(JOGSHOW_OFF), "Off") == 0);
    assert(strcmp(jog_show_label(JOGSHOW_TIME), "Time") == 0);
    assert(strcmp(jog_show_label(JOGSHOW_BAR), "Bar") == 0);
    assert(strcmp(jog_show_label(JOGSHOW_BOTH), "Time + Bar") == 0);
    assert(strcmp(jog_show_label(-1), "?") == 0);
    assert(strcmp(jog_show_label(4), "?") == 0);
}

int main(void)
{
    test_time();
    test_bar();
    test_compose();
    test_plan();
    test_jpeg();
    test_jog_show_cycle();
    printf("jog_test: ok\n");
    return 0;
}
