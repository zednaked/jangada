/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the encoders' quadrature decoder with detent counting, no hardware here, so the host
 * can test it (tests/enc_test.c). fm1_input.h runs it once a scan frame (~1.1 ms) per encoder.
 *
 * Jangada (after SLOOP 2.3 / Felucca 1.0, its #23 "knobs skipping or jumping"): an FM-1 detent is
 * one full quadrature cycle (4 transitions) and the knob rests in one state, the one seen at
 * power-on (relearned only after FM1_ENC_REST frames, ~1 s, parked elsewhere). Steps are emitted
 * on arriving back at it, the net transitions rounded to whole cycles (>= 2 counts one: a lost
 * transition or two is forgiven; a two-state jump counts on in the direction of travel). One
 * click = one step at any speed; bounce and back-and-forth cancel out.
 * Jangada 0.9.7 (after Felucca 1.1.5, its #126): every frame's state counts. The 2-sample filter took a state
 * only when two frames in a row saw it, and a turn faster than ~110 clicks/s (~60 with the transitions bunched)
 * lost whole clicks: a flick counted next to nothing. A two-state jump on the detent goes the way of a click in
 * the last FM1_ENC_GO frames (else nowhere). Counted up to ~300 clicks/s.
 * Never a second rest state. Felucca 0.9 learned rest states from any 25 ms pause, and Jangada
 * 0.2 from ~260 ms rests counted as evidence (a state and its complement): a knob held mid-click
 * that long, often enough, taught the complement as a rest and every click counted twice from
 * then on; short mid-click pauses of a slow turn taught the mid states and the knob went dead.
 * tests/enc_test.c plays both against this one. */
#pragma once
#include <stdint.h>

#define FM1_ENC_REST 900u         /* frames still off the detent state (~1 s): that is the detent */
#define FM1_ENC_GO 20u            /* a 2-state jump from the detent goes on the way of a click this recent (~22 ms) */

typedef struct {
    uint8_t prev, last;           /* the last decoded state; the last raw sample */
    uint8_t rest;                 /* the detent state (0..3) */
    int8_t sub;                   /* net transitions since the last time on the detent */
    uint16_t still;               /* frames in the same state */
    int8_t dir;                   /* the last click's direction, and the frames since it (to FM1_ENC_GO + 1) */
    uint8_t since;
} fm1_enc_t;

static void fm1_enc_init(fm1_enc_t *s)
{
    s->prev = s->last = 0xFF;     /* seeded by the first frame */
    s->rest = 0;
    s->sub = 0;
    s->still = 0;
    s->dir = 0;
    s->since = 0xFF;
}

/* one frame: cur = this frame's A (bit 1) and B (bit 0); returns the steps it emits (+ = clockwise) */
static int fm1_enc_frame(fm1_enc_t *s, uint32_t cur)
{
    uint32_t idx;
    int32_t n;
    if (s->since <= FM1_ENC_GO)
        s->since++;
    if (cur != s->last) {                          /* (no 2-sample filter: every frame's state counts) */
        s->last = (uint8_t)cur;
        s->still = 0;
    }
    if (s->prev == 0xFF) {                         /* first frame: the knob rests here */
        s->prev = (uint8_t)cur;
        s->rest = (uint8_t)cur;
    }
    if (s->still < 0xFFFFu && ++s->still == FM1_ENC_REST && cur != s->rest) {
        s->rest = (uint8_t)cur;                    /* parked ~1 s off the detent (held at power-on): */
        s->sub = 0;                                /* that is the detent. Never a second state (see top) */
    }
    if (cur == s->prev)
        return 0;
    idx = (uint32_t)s->prev << 2 | cur;
    if ((0x4182u >> idx) & 1u)
        s->sub++;
    else if ((0x2814u >> idx) & 1u)
        s->sub--;
    else                                           /* two states in one sample: a fast turn, the way it was */
        s->sub = (int8_t)(s->sub + 2 * (s->sub > 0 ? 1 : s->sub < 0 ? -1 : s->since <= FM1_ENC_GO ? s->dir : 0));
                                                   /* going (on the detent: a click of the last FM1_ENC_GO frames) */
    s->prev = (uint8_t)cur;
    if (s->sub > 100 || s->sub < -100)
        s->sub = 0;                                /* (never off the detent that long) */
    if (cur != s->rest)
        return 0;
    n = s->sub < 0 ? -s->sub : s->sub;             /* back on the detent: whole cycles, a lost transition */
    n = n >= 2 ? (n + 2) / 4 : 0;                  /* or two forgiven (one click = 4 transitions) */
    n = s->sub < 0 ? -n : n;
    s->sub = 0;
    if (n) {
        s->dir = (int8_t)(n < 0 ? -1 : 1);
        s->since = 0;
    }
    return (int)n;
}
