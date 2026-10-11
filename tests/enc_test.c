/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the panel's input decoding on the host (firmware/hal/fm1_input.h, fm1_enc.h).
 *
 * Encoders: the decoder of fm1_enc.h (after SLOOP 2.3 / Felucca 1.0: one rest state, whole cycles)
 * against turns a hand makes, next to the two before it, copied below: Felucca 0.9's (rest states
 * from any 25 ms pause) and Jangada 0.2's (rests counted as evidence, a state and its complement).
 * An FM-1 detent is one full quadrature cycle (4 transitions). One "frame" is one scan of the panel
 * (11 TIMER5 ticks, ~1.1 ms). Every scenario must give exactly the clicks turned with the new one;
 * the old ones are printed for comparison: both count clicks twice once a knob has been held part
 * way through a click, or its contacts bounced (they learn a second rest state).
 * Keys: the asymmetric debounce of fm1_input.h (a press after 2 frames closed, a release after 8
 * open, as each column is read). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#pragma GCC diagnostic ignored "-Wint-to-pointer-cast"
#include "../firmware/hal/fm1_input.h"

/* ---- Felucca 0.9's decoder, as it was (fm1_input.h fm1__frame) */
typedef struct { uint8_t prev, last, rest, still; int8_t sub; } f09_t;
static int f09_frame(f09_t *s, uint32_t cur)
{
    uint32_t idx;
    int step = 0;
    if (cur != s->last) {
        s->last = (uint8_t)cur;
        s->still = 0;
        return 0;
    }
    if (s->prev == 0xFF) {
        s->prev = (uint8_t)cur;
        s->rest = (uint8_t)(1u << cur);
    }
    if (s->still < 255u && ++s->still == 40u) {
        uint32_t r = s->rest, bit = 1u << cur, comp = 1u << (cur ^ 3u);
        if (!(r & bit))
            s->rest = (uint8_t)(r == comp ? (r | bit) : bit);
    }
    if (cur == s->prev)
        return 0;
    idx = (uint32_t)s->prev << 2 | cur;
    if ((0x4182u >> idx) & 1u)
        s->sub++;
    else if ((0x2814u >> idx) & 1u)
        s->sub--;
    s->prev = (uint8_t)cur;
    if ((s->rest >> cur) & 1u) {
        if (s->sub >= 2)
            step = 1;
        else if (s->sub <= -2)
            step = -1;
        s->sub = 0;
    }
    return step;
}

/* ---- Jangada 0.2's decoder, as it was (fm1_enc.h): rests of 240 frames counted as evidence */
typedef struct { uint8_t prev, last, still, rest; int8_t sub; uint8_t seen[4]; } j02_t;
static uint8_t j02_set(const j02_t *s)
{
    uint32_t i, best = 0, c;
    for (i = 1; i < 4u; i++)
        if (s->seen[i] > s->seen[best])
            best = i;
    c = best ^ 3u;
    return (uint8_t)(1u << best | (s->seen[c] && s->seen[c] * 4u >= s->seen[best] ? 1u << c : 0u));
}
static int j02_frame(j02_t *s, uint32_t cur)
{
    uint32_t idx;
    int step = 0;
    if (cur != s->last) {
        s->last = (uint8_t)cur;
        s->still = 0;
        return 0;
    }
    if (s->prev == 0xFF) {
        s->prev = (uint8_t)cur;
        s->seen[cur] = 1;
        s->rest = (uint8_t)(1u << cur);
    }
    if (s->still < 255u && ++s->still == 240u) {
        if (++s->seen[cur] >= 64u)
            for (idx = 0; idx < 4u; idx++)
                s->seen[idx] >>= 1;
        s->rest = j02_set(s);
    }
    if (cur == s->prev)
        return 0;
    idx = (uint32_t)s->prev << 2 | cur;
    if ((0x4182u >> idx) & 1u)
        s->sub++;
    else if ((0x2814u >> idx) & 1u)
        s->sub--;
    s->prev = (uint8_t)cur;
    if ((s->rest >> cur) & 1u) {
        if (s->sub >= 2)
            step = 1;
        else if (s->sub <= -2)
            step = -1;
        s->sub = 0;
    }
    return step;
}

/* ---- a hand on a knob */
static const uint8_t CW[4] = {0, 1, 3, 2};        /* + (clockwise): 00 -> 01 -> 11 -> 10 -> 00 */
static fm1_enc_t nw;
static f09_t f09;
static j02_t j02;
static long g_new, g_f09, g_j02;
static uint32_t pos;                               /* quadrature phase, 0..3 along CW */
static int fails;

static void frames(uint32_t n)
{
    while (n--) {
        g_new += fm1_enc_frame(&nw, CW[pos & 3u]);
        g_f09 += f09_frame(&f09, CW[pos & 3u]);
        g_j02 += j02_frame(&j02, CW[pos & 3u]);
    }
}
static void move(int dir, uint32_t hold)          /* one transition, then hold frames */
{
    pos = (pos + (dir > 0 ? 1u : 3u)) & 3u;
    frames(hold);
}
static void jump(uint32_t hold)                    /* two transitions in one sample */
{
    pos = (pos + 2u) & 3u;
    frames(hold);
}
static void click(int dir, uint32_t hold) { int i; for (i = 0; i < 4; i++) move(dir, hold); }
static void zero(void) { g_new = g_f09 = g_j02 = 0; }
static void start(void)
{
    fm1_enc_init(&nw);
    memset(&f09, 0, sizeof f09);
    memset(&j02, 0, sizeof j02);
    f09.prev = f09.last = j02.prev = j02.last = 0xFF;
    pos = 0;
    frames(400);                                   /* powered on resting on a detent */
    zero();
}
static void check_range(const char *what, long lo, long hi)
{
    int ok = g_new >= lo && g_new <= hi;
    printf("encoder: %-56s new %4ld  0.2 %4ld  0.9 %4ld  want %ld..%ld  %s\n", what, g_new, g_j02, g_f09, lo, hi,
           ok ? "ok" : "FAIL");
    fails += !ok;
    zero();
}
static void check(const char *what, long want) { check_range(what, want, want); }

/* ---- keys */
static uint32_t kid_col, kid_row;
static void key_find(uint32_t id)
{
    uint32_t p, r;
    for (p = 0; p < FM1_NCOL; p++)
        for (r = 1; r < 5u; r++)
            if (FM1_KEYMAP[r][p] == (int8_t)id) {
                kid_col = p;
                kid_row = r;
            }
}
static void key_frame(int closed)                  /* one frame: the key's column read */
{
    fm1_in.raw[kid_col] = (uint8_t)(closed ? 1u << kid_row : 0u);
    fm1__keys(kid_col);
}
static void key_check(const char *what, int ok)
{
    printf("keys:    %-56s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

int main(void)
{
    uint32_t k, i;
    long presses;
    char what[96];

    start();
    for (k = 0; k < 10u; k++) click(1, 6), frames(30);
    check("10 slow clicks clockwise", 10);
    for (k = 0; k < 10u; k++) click(-1, 6), frames(30);
    check("10 slow clicks anticlockwise", -10);
    for (k = 0; k < 20u; k++) click(1, 3), frames(k % 3u ? 300 : 20);
    check("20 clicks, let go between", 20);
    for (k = 0; k < 24u; k++) click(1, 2);
    frames(30);
    check("24 fast clicks (2 frames a transition)", 24);
    for (k = 0; k < 40u; k++) click(1, 2);
    frames(300);
    check("a fast spin of 40", 40);
    for (k = 0; k < 60u; k++) click(1, 1);         /* (Jangada 0.9.7, after Felucca's #126: 1 frame a transition, */
    frames(300);                                   /* ~230 clicks/s; the 2-sample filter lost them all) */
    check("a flick of 60 (1 frame a transition)", 60);
    for (k = 0; k < 30u; k++) click(-1, 1);
    frames(300);
    check("and 30 back", -30);
    for (k = 0; k < 5u; k++) {                     /* a slow turn that stops mid-click (50 ms) */
        move(1, 6); move(1, 45); move(1, 6); move(1, 30);
    }
    check("5 clicks with 50 ms pauses mid-click", 5);
    for (k = 0; k < 20u; k++) {                    /* slow turns: 70-frame pauses half way */
        move(1, 3); move(1, 70); move(1, 3); move(1, 300);
    }
    check("20 slow clicks with 70-frame pauses half way", 20);
    for (k = 0; k < 5u; k++) {
        move(1, 6); move(1, 6); frames(200); move(1, 6); move(1, 30);
    }
    check("5 clicks held half way 0.2 s", 5);
    for (k = 0; k < 20u; k++) {
        move(1, 6); move(1, 6); frames(400); move(1, 6); move(1, 300);
    }
    check("20 clicks held half way 0.4 s each", 20);
    for (k = 0; k < 5u; k++) {                     /* contact bounce on each transition */
        move(1, 1); move(-1, 1); move(1, 4);
        move(1, 4); move(1, 1); move(-1, 1); move(1, 4); move(1, 30);
    }
    check("5 clicks with contact bounce", 5);
    move(1, 6); move(1, 6); move(-1, 6); move(-1, 30);
    check("half a click and back", 0);
    for (k = 0; k < 10u; k++) {                    /* a fast turn: one sample sees two transitions */
        move(1, 2); jump(2); move(1, 30);
    }
    check("10 clicks, each with a two-state jump", 10);
    for (k = 0; k < 6u; k++) {                     /* a flick: only jumps */
        jump(1); jump(1);
    }
    frames(30);
    check_range("6 flicks seen as two-state jumps: no wrong direction", 0, 6);
    click(1, 6); frames(30); zero();
    move(1, 6); move(1, 1000);                     /* parked ~1 s mid-click (held at power-on) */
    for (k = 0; k < 4u; k++) click(1, 6), frames(30);
    check("parked 1 s a quarter off the detent, then 4 clicks", 4);
    for (k = 0; k < 4u; k++) click(1, 6), frames(30);
    check("4 more", 4);
    move(1, 6); move(1, 1000);                     /* (the complement of the detent now) */
    for (k = 0; k < 4u; k++) click(1, 6), frames(30);
    check("parked 1 s half off the detent, then 4 clicks", 4);
    start();                                       /* a long session: 300 clicks, one in 5 held mid-click */
    for (k = 0; k < 300u; k++) {
        if (k % 5u == 0u) {
            move(1, 4); move(1, 4); frames(300); move(1, 4); move(1, 200);
        } else {
            click(1, 4);
            frames(k % 3u ? 300 : 30);
        }
    }
    snprintf(what, sizeof what, "300 clicks, one in 5 held half way 0.3 s");
    check(what, 300);

    /* keys: the debounce of fm1_input.h */
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    key_find(20);                                  /* a note key (id 14.. = note 0..) */
    key_frame(1);
    key_check("one frame closed: not yet a press", !(fm1_in.notes & (1u << 6)));
    key_frame(1);
    key_check("two frames closed: pressed", (fm1_in.notes & (1u << 6)) && (fm1_in.notes_pressed & (1u << 6)));
    fm1_in.notes_pressed = 0;
    for (i = 0; i < 7u; i++)                       /* bounces open for 7 frames on the way */
        key_frame(i & 1u);
    key_frame(1);
    key_check("bouncing open under 8 frames: still held, no second press",
              (fm1_in.notes & (1u << 6)) && !fm1_in.notes_pressed);
    for (i = 0; i < 7u; i++)
        key_frame(0);
    key_check("7 frames open: still held", (fm1_in.notes & (1u << 6)) != 0);
    key_frame(0);
    key_check("8 frames open: released", !(fm1_in.notes & (1u << 6)));
    key_frame(1);
    key_frame(0);
    key_frame(0);
    key_check("one stray closed sample: no note", !(fm1_in.notes & (1u << 6)) && !fm1_in.notes_pressed);
    presses = 0;
    for (k = 0; k < 20u; k++) {                    /* 20 taps with bounce on both edges */
        key_frame(1); key_frame(0); key_frame(1); key_frame(1); key_frame(1);
        if (fm1_in.notes_pressed & (1u << 6))
            presses++;
        fm1_in.notes_pressed = 0;
        for (i = 0; i < 20u; i++)
            key_frame(i < 3u ? (int)(i & 1u) : 0);
        for (i = 0; i < 10u; i++)
            key_frame(0);
    }
    key_check("20 bouncing taps: 20 presses", presses == 20);
    memset((void *)&fm1_in, 0, sizeof fm1_in);
    key_find(FM1_BTN_OCT_UP);
    key_frame(1);
    key_frame(1);
    {
        uint32_t rel = fm1_in.released, pr = fm1_in.pressed;
        key_check("a button: pressed edge after two frames", pr == (1u << FM1_BTN_OCT_UP) && !rel);
    }
    for (i = 0; i < 8u; i++)
        key_frame(0);
    key_check("a button: released edge after eight", (fm1_in.released & (1u << FM1_BTN_OCT_UP)) != 0);

    puts(fails ? "ENCODER TEST FAILED" : "encoders and keys: all ok");
    return fails != 0;
}
