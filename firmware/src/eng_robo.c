/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada 0.9.1: ROBO, engine 12. Four small machine voices from the ideas in issue #4 (gospodindilly-ops):
 * the speech chips' vowels, Xenakis' stochastic waveform, Walsh's square steps and a scanned ring of masses.
 * Written for the FM-1 in fixed point after the published algorithms (Kaegi and Tempelaars' VOSIM, 1978;
 * Xenakis' GENDYN, 1991; Walsh functions in sequency order; Verplank, Mathews and Shaw's scanned synthesis,
 * 2000), no code taken from elsewhere.
 *
 * MODE picks the voice (the values are stored: new ones are appended):
 *   VOSIM  each period of the note starts a burst of PULS sin^2 pulses at the formant FORM, each DCAY smaller
 *          than the last, then silence to the next period: vowels, buzzes, a talking chip. TRK key-tracks FORM
 *          (127 = one semitone a semitone, around C4); the track's FLT (and its envelope) moves FORM
 *   GENDY  dynamic stochastic synthesis: one period is PTS breakpoints joined by lines; every period each
 *          point's level walks by up to STEP and its length by up to TIME, between mirrors. The lengths are
 *          shares of the period, so the pitch holds while the wave crawls. It starts from a sine (STEP and TIME
 *          0: a still PTS-sided sine); SEED 0 walks a new way each note, 1..127 the same way every time
 *   WALSH  a sum of the 31 Walsh functions (+-1 steps, by sequency): SEQ the centre of a bump of them, TERM its
 *          width, BLND from the cal (even, cosine-like) to the sal (odd, sine-like) ones, SWP sweeps SEQ.
 *          The terms past half the sample rate are left out and each step is band-limited (polyBLEP)
 *   SCAN   scanned synthesis, Lite: a ring of 16 masses on springs, moved once a block (1.4 kHz: the shape
 *          moves at haptic rates, not audio ones), read round at the note's pitch, so pitch and timbre are
 *          apart. STIF the springs between the masses (the shape's waves run faster), DAMP their loss (0 rings
 *          on: a frozen shape keeps moving), CNTR the spring to the centre (the shape breathes), HIT the shape
 *          the note starts from (a sine, a narrower and narrower bump, then noise); DRFT pushes a mass of the
 *          ring at random every block (a bowed, living shape) instead of wandering
 * Then for all: DRFT a slow random wander of FORM / STEP / SEQ, CRSH holds samples and drops bits, TONE a
 * one-pole low-pass (the track's FLT moves it in GENDY and WALSH), a DC blocker.
 *
 * State: ph[0] the period, ph[1] VOSIM's pulse / GENDY's segment / WALSH's sweep phase, ph[2] the random
 * generator; s[0] VOSIM's pulse count / GENDY's segment, s[1] VOSIM's pulse level / GENDY's segment rate,
 * s[2] the DC blocker, s[3] TONE, s[4] s[5] CRSH's held sample and count, s[6] s[7] the drift. GENDY's points
 * WALSH's wave and SCAN's ring live in the part's engine arena (engines.c eng_arena_of), shared with GRAIN, FM6
 * and PHYS. */
static const char *const N_ROBO_MODE[] = {"VOSIM", "GENDY", "WALSH", "SCAN"};
enum { RB_VOSIM, RB_GENDY, RB_WALSH, RB_SCAN, RB_COUNT };
#define GD_MAX 16                    /* GENDY: breakpoints at most */
#define WL_N 32                      /* WALSH: points (and functions) */
#define SC_N 16                      /* SCAN: masses */

static const char *const N_ROBO_PULS[] = {"1", "2", "3", "4", "5", "6", "7", "8", 0};
static const char *const N_ROBO_PTS[] = {"4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16", 0};
static const param_desc_t ROBO_D_PULS = {"PULS", F_INT, 0, 127, 30, N_ROBO_PULS, 0};   /* = edit[2] but the names */
static const param_desc_t ROBO_D_STEP = {"STEP", F_PCT, 0, 127, 40, 0, 0};
static const param_desc_t ROBO_D_PTS = {"PTS", F_INT, 0, 127, 40, N_ROBO_PTS, 0};
static const param_desc_t ROBO_D_TIME = {"TIME", F_PCT, 0, 127, 40, 0, 0};
static const param_desc_t ROBO_D_SEED = {"SEED", F_INT, 0, 127, 0, 0, 0};
static const param_desc_t ROBO_D_SEQ = {"SEQ", F_PCT, 0, 127, 40, 0, 0};
static const param_desc_t ROBO_D_TERM = {"TERM", F_PCT, 0, 127, 40, 0, 0};
static const param_desc_t ROBO_D_BLND = {"BLND", F_PCT, 0, 127, 40, 0, 0};
static const param_desc_t ROBO_D_SWP = {"SWP", F_LFOHZ, 0, 127, 0, 0, 0};
static const param_desc_t ROBO_D_STIF = {"STIF", F_PCT, 0, 127, 40, 0, 0};
static const param_desc_t ROBO_D_DAMP = {"DAMP", F_PCT, 0, 127, 40, 0, 0};
static const param_desc_t ROBO_D_CNTR = {"CNTR", F_PCT, 0, 127, 40, 0, 0};
static const param_desc_t ROBO_D_HIT = {"HIT", F_PCT, 0, 127, 64, 0, 0};
static const param_desc_t *const ROBO_DESC[RB_COUNT][4] = {
    [RB_VOSIM] = {0, &ROBO_D_PULS, 0, 0},
    [RB_GENDY] = {&ROBO_D_STEP, &ROBO_D_PTS, &ROBO_D_TIME, &ROBO_D_SEED},
    [RB_WALSH] = {&ROBO_D_SEQ, &ROBO_D_TERM, &ROBO_D_BLND, &ROBO_D_SWP},
    [RB_SCAN] = {&ROBO_D_STIF, &ROBO_D_DAMP, &ROBO_D_CNTR, &ROBO_D_HIT},
};

static const param_desc_t *robo_desc(const track_t *t, uint32_t k)
{
    return k >= 1u && k <= 4u ? ROBO_DESC[(uint32_t)t->p[P_E0] % RB_COUNT][k - 1u] : 0;
}

/* the Walsh functions by sequency (0..31 sign changes a period): bit n set = -1 at point n */
static const uint32_t WALSH[WL_N] = {
    0x00000000u, 0xFFFF0000u, 0x00FFFF00u, 0xFF00FF00u, 0x0FF00FF0u, 0xF00F0FF0u, 0x0F0FF0F0u, 0xF0F0F0F0u,
    0x3C3C3C3Cu, 0xC3C33C3Cu, 0x3CC3C33Cu, 0xC33CC33Cu, 0x33CC33CCu, 0xCC3333CCu, 0x3333CCCCu, 0xCCCCCCCCu,
    0x66666666u, 0x99996666u, 0x66999966u, 0x99669966u, 0x69966996u, 0x96696996u, 0x69699696u, 0x96969696u,
    0x5A5A5A5Au, 0xA5A55A5Au, 0x5AA5A55Au, 0xA55AA55Au, 0x55AA55AAu, 0xAA5555AAu, 0x5555AAAAu, 0xAAAAAAAAu,
};

typedef struct {
    union {
        struct {
            int16_t amp[GD_MAX];     /* GENDY: the points' levels, Q15 */
            uint8_t dur[GD_MAX];     /* their lengths, 1..32 (shares of the period) */
            uint16_t sum;            /* the lengths' sum */
            uint8_t n;               /* the points in use (PTS when they were laid out) */
        } g;
        int16_t wav[WL_N];           /* WALSH: this block's wave */
        struct {
            int32_t x[SC_N], v[SC_N];   /* SCAN: the masses' places and speeds, Q20 */
            int16_t w0[SC_N], w1[SC_N]; /* the shape at the last block and at this one, Q15 (read across) */
        } s;
    } u;
    uint8_t lay;                     /* 1 + the mode whose state is in u, 0 = none */
} robo_voice_t;
typedef struct {
    robo_voice_t v[NVOICE];
} robo_part_t;

static robo_voice_t *robo_of(track_t *t, voice_t *v)
{
    robo_part_t *P;
    if (t < &trk[0] || t >= &trk[NTRK])
        return 0;
    P = (robo_part_t *)eng_arena_of(t, ENGI_ROBO);
    return P ? &P->v[(uint32_t)(v - t->v) % NVOICE] : 0;
}

static inline uint32_t robo_rnd(voice_t *v) { return noise32((int32_t *)&v->ph[2]); }

/* GENDY: PTS points on a sine, equal lengths */
static void gendy_lay(robo_voice_t *R, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) {
        R->u.g.amp[i] = (int16_t)((osc_sine((0xFFFFFFFFu / n) * i) * 29) >> 5);
        R->u.g.dur[i] = 16;
    }
    R->u.g.n = (uint8_t)n;
    R->u.g.sum = (uint16_t)(16u * n);
    R->lay = 1u + RB_GENDY;
}

/* SCAN: the ring at rest in the shape HIT asks for: 0 a sine, up to 96 a raised-cosine bump narrowing from
 * the whole ring to two masses, past it more and more noise; its mean taken out */
static void scan_lay(robo_voice_t *R, voice_t *v, int32_t hit)
{
    uint32_t i;
    int32_t sum = 0, w = hit < 96 ? SC_N * 16 - hit * (SC_N * 16 - 32) / 96 : 32;   /* the bump's width, Q4 */
    int32_t nz = hit > 96 ? (hit - 96) * 1057 : 0;    /* noise, Q15 */
    for (i = 0; i < SC_N; i++) {
        int32_t d = (int32_t)i * 16 - SC_N * 8, x;    /* from the ring's middle, Q4 */
        d = d < 0 ? -d : d;
        if (!hit)
            x = osc_sine(0x10000000u * i) >> 1;       /* (the sine already has no mean) */
        else                                          /* (1 + cos(pi 2d / w)) / 2 */
            x = d * 2 < w ? (32767 + osc_sine(0x40000000u + ((uint32_t)(d * 2 * 32768 / w) << 16))) >> 1 : 0;
        x += mulq15((int32_t)(robo_rnd(v) >> 17) - 16384, nz) * 2;
        R->u.s.x[i] = x << 5;
        R->u.s.v[i] = 0;
        sum += x;
    }
    for (i = 0; i < SC_N; i++) {
        R->u.s.x[i] -= (sum / SC_N) << 5;
        R->u.s.w0[i] = R->u.s.w1[i] = (int16_t)clamp(R->u.s.x[i] >> 5, -32767, 32767);
    }
    R->lay = 1u + RB_SCAN;
}

/* SCAN: one step of the ring (symplectic Euler: the speeds, then the places): the springs to the neighbours
 * (k), to the centre (c), Q18, the loss (d), Q14; DRFT's push on one random mass (with a loss of its own: pushed
 * on, the ring settles at a level instead of growing). The shape it leaves is the
 * one read to the next block (w0 -> w1). Stable while c + 4 k < 4: here at most 0.008, a mode of up to 20 Hz */
static void scan_step(robo_voice_t *R, voice_t *v, int32_t k, int32_t c, int32_t d, int32_t push)
{
    uint32_t i;
    int32_t *x = R->u.s.x, *u = R->u.s.v;
    if (push) {                                       /* a mass one way, the opposite one the other: the ring
                                                       * as a whole stays (with CNTR 0 nothing would bring it back) */
        uint32_t r = robo_rnd(v);
        int32_t f = (((int32_t)(r >> 16) - 32768) * push) >> 8;
        u[r & (SC_N - 1u)] += f;
        u[(r + SC_N / 2u) & (SC_N - 1u)] -= f;
    }
    for (i = 0; i < SC_N; i++) {
        int32_t l = x[(i - 1u) & (SC_N - 1u)] + x[(i + 1u) & (SC_N - 1u)] - 2 * x[i];
        u[i] += ((l >> 2) * k >> 16) - ((x[i] >> 2) * c >> 16) - ((u[i] >> 2) * d >> 12);
    }
    for (i = 0; i < SC_N; i++) {
        x[i] = clamp(x[i] + u[i], -(2 << 20), 2 << 20);
        R->u.s.w0[i] = R->u.s.w1[i];
        R->u.s.w1[i] = (int16_t)clamp(x[i] >> 5, -32767, 32767);
    }
}

/* GENDY: a period's walk: each level by up to +-st (Q15), each length by up to +-tm, mirrored at the walls */
static void gendy_walk(robo_voice_t *R, voice_t *v, int32_t st, int32_t tm)
{
    uint32_t i, n = R->u.g.n, sum = 0;
    for (i = 0; i < n; i++) {
        uint32_t r = robo_rnd(v);
        int32_t a = R->u.g.amp[i] + ((((int32_t)(r >> 16) - 32768) * st) >> 15);   /* (st <= 2^13: 31 bits) */
        int32_t d = R->u.g.dur[i] + (tm ? (int32_t)((r & 0xFFFFu) % (uint32_t)(2 * tm + 1)) - tm : 0);
        if (a > 29000)
            a = 58000 - a;
        if (a < -29000)
            a = -58000 - a;
        if (d > 32)
            d = 64 - d;
        if (d < 1)
            d = 2 - d;
        R->u.g.amp[i] = (int16_t)clamp(a, -29000, 29000);
        R->u.g.dur[i] = (uint8_t)clamp(d, 1, 32);
        sum += R->u.g.dur[i];
    }
    R->u.g.sum = (uint16_t)sum;
}

/* GENDY: segment i's phase rate, Q32 a segment: the period's rate * sum / length */
static uint32_t gendy_inc(const robo_voice_t *R, uint32_t inc, uint32_t i)
{
    uint32_t x = (inc >> 10) * R->u.g.sum / R->u.g.dur[i];   /* (< 2^22 * 512) */
    return x >= 0x400000u ? 0xFFFFFFFFu : x << 10;
}

/* TONE: a one-pole low-pass, topology-preserving (k = G Q16, as fx.c ins_lpk): stable to the top */
static inline int32_t robo_lp(int32_t *z, int32_t x, int32_t k)
{
    int32_t v = mulq16(x - *z, (uint32_t)k), y = v + *z;
    *z = y + v;
    return y;
}

static void robo_note_on(track_t *t, voice_t *v)
{
    robo_voice_t *R = robo_of(t, v);
    uint32_t seed = t->p[P_E0] == RB_GENDY && t->p[P_E4] ? (uint32_t)t->p[P_E4] * 0x9E3779B9u
                                                          : rng() ^ (v->note + 1u) * 0x85EBCA6Bu;
    if (v->env || v->env_out)                       /* a retrigger: the wave goes on where it was */
        return;
    v->ph[0] = v->ph[1] = 0;
    v->ph[2] = seed ? seed : 1u;
    v->s[0] = v->s[2] = v->s[3] = v->s[4] = v->s[5] = v->s[6] = v->s[7] = 0;
    v->s[1] = 32767;
    if (R && t->p[P_E0] == RB_GENDY)
        gendy_lay(R, 4u + (uint32_t)clamp(t->p[P_E2], 0, 127) * 13u / 128u);
    if (R && t->p[P_E0] == RB_SCAN)
        scan_lay(R, v, clamp(t->p[P_E4], 0, 127));
}

/* the drift (DRFT), once a block: a new random target every 0.7 .. 3 s, followed with a 0.7 s lag; Q15 */
static int32_t robo_drift(voice_t *v, int32_t drft)
{
    int32_t tg = v->s[7] >> 16, cnt = v->s[7] & 0xFFFF;
    if (!drft)
        return 0;
    if (--cnt <= 0) {
        uint32_t r = robo_rnd(v);
        tg = (int32_t)(r >> 16) - 32768;
        cnt = 1024 + (int32_t)(r & 3071u);
    }
    v->s[7] = (int32_t)((uint32_t)tg << 16 | (uint32_t)cnt);
    v->s[6] += ((tg << 4) - v->s[6]) >> 10;          /* Q19 */
    return ((v->s[6] >> 4) * drft) / 127;
}

/* WALSH: this block's wave, the bump of terms around SEQ (+ sweep, drift) TERM wide, below kmax */
static void walsh_build(robo_voice_t *R, const int16_t *p, int32_t ctr, uint32_t kmax)
{
    int32_t acc[WL_N] = {0}, wid = 64 + p[P_E2] * 12, blnd = p[P_E3], tot = 0;   /* Q8 sequencies */
    uint32_t k, i;
    for (k = 1; k < WL_N && k <= kmax; k++) {
        int32_t d = ((int32_t)k << 8) - ctr, c;
        d = d < 0 ? -d : d;
        if (d >= wid)
            continue;
        c = ((wid - d) << 12) / wid;                  /* a triangle, Q12 */
        c = (c * ((k & 1u) ? 32 + blnd : 159 - blnd)) >> 7;   /* odd: sal, even: cal */
        if (k == kmax)
            c >>= 1;                                  /* the last one in at half: less of a step as it leaves */
        for (i = 0; i < WL_N; i++)
            acc[i] += (WALSH[k] >> i) & 1u ? -c : c;
        tot += c;
    }
    if (!tot) {
        for (i = 0; i < WL_N; i++)
            R->u.wav[i] = 0;
        return;
    }
    tot = (28000 << 12) / tot;                        /* the peak at most the sum of the terms: +-28000 */
    for (i = 0; i < WL_N; i++)
        R->u.wav[i] = (int16_t)((acc[i] * tot) >> 12);
}

static void robo_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    robo_voice_t *R = robo_of(t, v);
    uint32_t mode = (uint32_t)p[P_E0] % RB_COUNT, inc = m->inc, i, ph0 = v->ph[0], ph1 = v->ph[1];
    int32_t dr = robo_drift(v, p[P_E5]), crsh = p[P_E6], hold = 1 + crsh / 8, sh = crsh / 11;
    int32_t dc = v->s[2], lp = v->s[3], held = v->s[4], cnt = v->s[5], k0 = v->s[0], a0 = v->s[1], lk;
    {                                                 /* TONE: one TPT pole, the FLT in it but in VOSIM */
        int32_t c = (p[P_E7] << 8) + (mode == RB_VOSIM ? 0 : m->cutoff);
        uint32_t g = SVF_G[clamp(c, 0, 127 << 8) >> 8];
        lk = (int32_t)((g << 16) / (4096u + g));
    }
    if (mode != RB_VOSIM && !R)
        return;
    switch (mode) {
    case RB_VOSIM: {
        uint32_t np = 1u + (uint32_t)clamp(p[P_E2], 0, 127) * 8u / 128u, fi;
        int32_t b = 32767 - p[P_E3] * 200, f16;   /* DCAY: each pulse 1.0 .. 0.22 of the last */
        if (a0 < 0 || a0 > 32767)                     /* (from GENDY's state: MODE moved under the note) */
            a0 = 0, k0 = (int32_t)np;
        f16 = (36 + p[P_E1] * 90 / 127) * 16 + (((m->pitch16 - 60 * 16) * p[P_E4]) >> 7) + ((m->cutoff * 14) >> 8) +
              ((dr * 192) >> 15);                     /* FORM, 1/16 st: 65 Hz .. 12 kHz; the drift +-12 st */
        fi = PITCH_INC[clamp(f16, 0, 2047)];
        for (i = 0; i < n; i++) {
            int32_t y = 0;
            uint32_t o = ph0;
            ph0 += inc;
            if (ph0 < o)                              /* a new period: a new burst */
                k0 = 0, a0 = 32767, ph1 = 0;
            if ((uint32_t)k0 < np) {
                o = ph1;
                y = mulq15(32767 - osc_sine(ph1 + 0x40000000u), a0);   /* 2 sin^2 (the DC blocker centres it) */
                ph1 += fi;
                if (ph1 < o)
                    k0++, a0 = mulq15(a0, b);
            }
            y = robo_lp(&lp, y, lk);
            if (crsh) {
                if (--cnt <= 0)
                    cnt = hold, held = y >> sh << sh;
                y = held;
            }
            dc += ((y << 8) - dc) >> 8;
            y -= dc >> 8;
            out[i] += voice_amp(clamp(y, -32767, 32767), m, i);
        }
        break;
    }
    case RB_GENDY: {
        uint32_t np = 4u + (uint32_t)clamp(p[P_E2], 0, 127) * 13u / 128u, seg = (uint32_t)k0, sinc;
        int32_t st = clamp(p[P_E1] * p[P_E1] / 4 + ((dr * 40) >> 8), 0, 8192);   /* STEP: up to 1/4 of the range */
        int32_t tm = p[P_E3] / 16;                    /* TIME: 0 .. 7 */
        if (R->lay != 1u + RB_GENDY || R->u.g.n != np) {   /* PTS moved, or another mode's state is there */
            gendy_lay(R, np);
            seg = 0;
        }
        if (seg >= np)
            seg = 0;
        sinc = gendy_inc(R, inc, seg);                /* (the pitch may have moved) */
        for (i = 0; i < n; i++) {
            uint32_t o = ph1, nx = seg + 1u < np ? seg + 1u : 0u;
            int32_t y = R->u.g.amp[seg] + (((R->u.g.amp[nx] - R->u.g.amp[seg]) * (int32_t)(ph1 >> 17)) >> 15);
            ph1 += sinc;
            if (ph1 < o) {                            /* the next segment */
                uint32_t f = (sinc >> 16) ? (ph1 >> 8) / (sinc >> 16) : 0;   /* the overshoot, Q8 of a sample */
                seg = nx;
                if (!seg)
                    gendy_walk(R, v, st, tm);         /* a period done: everything walks */
                sinc = gendy_inc(R, inc, seg);
                ph1 = (sinc >> 8) * (f < 256u ? f : 255u);   /* ... carried at the new rate: the pitch holds */
            }
            y = robo_lp(&lp, y, lk);
            if (crsh) {
                if (--cnt <= 0)
                    cnt = hold, held = y >> sh << sh;
                y = held;
            }
            dc += ((y << 8) - dc) >> 9;
            y -= dc >> 8;
            out[i] += voice_amp(y, m, i);
        }
        k0 = (int32_t)seg;
        a0 = (int32_t)sinc;
        break;
    }
    case RB_SCAN: {
        /* STIF 0.00002 .. 0.002 (squared), CNTR 0 .. 0.0006 (Q18), DAMP 0 .. 0.1 (Q14); DRFT a push of up to 3 % of the
         * range a block, its own loss up to 0.002 */
        int32_t k = 6 + p[P_E1] * p[P_E1] * 30 / 1000, c = p[P_E3] * 13 / 10, d = p[P_E2] * p[P_E2] / 10;
        int32_t push = p[P_E5] * 2, ia = 0, da = (32768 + (int32_t)n / 2) / (int32_t)(n ? n : 1);
        if (R->lay != 1u + RB_SCAN)
            scan_lay(R, v, clamp(p[P_E4], 0, 127));
        scan_step(R, v, k, c, d + push / 8, push);
        for (i = 0; i < n; i++, ia += da) {
            uint32_t j = ph0 >> 28, j1 = (j + 1u) & (SC_N - 1u), fr = (ph0 >> 13) & 0x7FFFu;
            int32_t a = R->u.s.w0[j] + (((R->u.s.w0[j1] - R->u.s.w0[j]) * (int32_t)fr) >> 15);
            int32_t b = R->u.s.w1[j] + (((R->u.s.w1[j1] - R->u.s.w1[j]) * (int32_t)fr) >> 15);
            int32_t y = clamp((a + (((b - a) * ia) >> 15)) * 2, -32767, 32767);   /* last block's shape into this
                                                                                * one's: no zipper; x2: a bump is thin */
            ph0 += inc;
            y = robo_lp(&lp, y, lk);
            if (crsh) {
                if (--cnt <= 0)
                    cnt = hold, held = y >> sh << sh;
                y = held;
            }
            dc += ((y << 8) - dc) >> 9;
            y -= dc >> 8;
            out[i] += voice_amp(y, m, i);
        }
        break;
    }
    default: {                                        /* WALSH */
        uint32_t kmax = inc ? 0x60000000u / inc : WL_N, si = inc >= 0x08000000u ? 0u : inc << 5;   /* (si 0: no BLEP, the steps
                                                                                              * shorter than a sample) */
        int32_t ctr = p[P_E1] * 31 * 256 / 127 + ((dr * 8) >> 7);   /* SEQ, Q8; the drift +-8 */
        if (p[P_E4]) {                                /* SWP: a triangle of +-8 at its rate */
            uint32_t q = ph1 >> 16;
            ctr += (((int32_t)(q < 32768u ? q : 65535u - q) - 16384) * 8) >> 6;
            ph1 += (LFO_INC[clamp(p[P_E4], 0, 127)] / CTL) * n;
        }
        R->lay = 1u + RB_WALSH;
        walsh_build(R, p, clamp(ctr, 0, (int32_t)(kmax < WL_N ? kmax : WL_N - 1u) << 8), kmax);   /* (high notes: the
                                                                                         * bump slides down to what is left) */
        for (i = 0; i < n; i++) {
            uint32_t j = ph0 >> 27, sp = ph0 << 5;
            int32_t y = R->u.wav[j], r = blep(sp, si);
            if (r) {                                  /* the step at the nearest edge, band-limited */
                int32_t h = sp < si ? y - R->u.wav[(j - 1u) & (WL_N - 1u)] : R->u.wav[(j + 1u) & (WL_N - 1u)] - y;
                y += (h * r) >> 16;
            }
            ph0 += inc;
            y = robo_lp(&lp, y, lk);
            if (crsh) {
                if (--cnt <= 0)
                    cnt = hold, held = y >> sh << sh;
                y = held;
            }
            dc += ((y << 8) - dc) >> 9;
            y -= dc >> 8;
            out[i] += voice_amp(y, m, i);
        }
        break;
    }
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = k0;
    v->s[1] = a0;
    v->s[2] = dc;
    v->s[3] = lp;
    v->s[4] = held;
    v->s[5] = cnt;
}

/* {MODE, A, B, C, D, DRFT, CRSH, TONE}: VOSIM {FORM PULS DCAY TRK}, GENDY {STEP PTS TIME SEED},
 * WALSH {SEQ TERM BLND SWP}. Jangada's machine voices: the dark and industrial side, and the sertão's */
static const preset_t ROBO_PRESETS[] = {
    /* VOSIM. ROBO VOX: a talking chip, the filter envelope opening its mouth; CORAL MORTO: a dead choir, three
     * pulses, the drift moving the vowel, held (HOLD); LATA: a tin can's buzz bass; ABOIO: the cowherd's call
     * of the sertão, a long vowel that bends and wanders */
    {"ROBO VOX", {RB_VOSIM, 60, 30, 50, 64, 0, 0, 120}, {0, 70, 100, 40}, 30, 1, FX(0, 0, 30, 30), CAT(LEAD)},
    {"CORAL MORTO", {RB_VOSIM, 50, 40, 70, 100, 70, 0, 110}, {90, 90, 127, 100}, 0, 0, FX(0, 40, 20, 100),
     ARP(7, 9, 1, 127), SET({P_AHOLD, 1}, {P_EVOL, 50}), CAT(DRONE)},
    {"LATA", {RB_VOSIM, 30, 70, 30, 30, 0, 30, 90}, {0, 60, 90, 20}, 0, 1, FX(40, 0, 0, 10), SET({P_TRANS, -12}),
     CAT(BASS)},
    {"ABOIO", {RB_VOSIM, 72, 20, 60, 90, 50, 0, 120}, {40, 90, 120, 80}, 0, 1, FX(0, 20, 40, 70),
     SET({P_LD_PIT, 4}, {P_LRATE, 30}), CAT(LEAD)},
    /* GENDY. XENAKIS: the wave crawling, a pad that never repeats; ENXAME: a swarm, big steps, many points;
     * GERADOR: a generator's hum, slow and low, fixed seed; ESTILHACO: a shard, the walk crushed */
    {"XENAKIS", {RB_GENDY, 40, 50, 40, 0, 30, 0, 110}, {80, 90, 127, 100}, 0, 0, FX(0, 30, 30, 90), CAT(PAD)},
    {"ENXAME", {RB_GENDY, 90, 127, 80, 0, 0, 0, 100}, {30, 90, 110, 70}, 0, 0, FX(20, 20, 30, 60), CAT(FX)},
    {"GERADOR", {RB_GENDY, 14, 20, 10, 7, 40, 0, 104}, {100, 90, 127, 110}, 0, 0, FX(30, 0, 0, 60),
     ARP(7, 9, 1, 127), SET({P_AHOLD, 1}, {P_TRANS, -12}), CAT(DRONE)},
    {"ESTILHACO", {RB_GENDY, 70, 30, 100, 33, 0, 70, 127}, {0, 50, 0, 30}, 0, 0, FX(30, 0, 30, 30), CAT(PERC)},
    /* WALSH. WALSH BASS: the low terms, sal-heavy, a square's cousin; TELEX: the terms swept, a machine
     * printing; SINAL: a narrow bump high up, a signal through the wall; CHAPA: cal-heavy, wide, hit with
     * the filter envelope, a sheet of steel */
    {"WALSH BASS", {RB_WALSH, 8, 20, 100, 0, 0, 0, 100}, {0, 70, 100, 20}, 20, 1, FX(20, 0, 0, 10),
     SET({P_TRANS, -12}), CAT(BASS)},
    {"TELEX", {RB_WALSH, 50, 30, 64, 50, 20, 20, 110}, {0, 80, 90, 40}, 0, 0, FX(10, 0, 40, 40), CAT(LEAD)},
    {"SINAL", {RB_WALSH, 100, 10, 90, 0, 40, 0, 120}, {10, 90, 110, 70}, 0, 0, FX(0, 20, 50, 60), CAT(FX)},
    {"CHAPA", {RB_WALSH, 64, 100, 10, 0, 0, 0, 70}, {0, 60, 0, 40}, 50, 0, FX(20, 0, 20, 50), CAT(KEYS)},
    /* SCAN. MARE: the tide, a slow ring pushed by the drift, held (HOLD); ONDA PRESA: a trapped wave, a bump
     * running round the ring, never lost; ESTACA: a pile driver, noise struck, stiff and lossy; VIDRO: a sine
     * that the centre spring makes breathe, glass */
    {"MARE", {RB_SCAN, 30, 20, 30, 40, 90, 0, 100}, {100, 90, 127, 110}, 0, 0, FX(0, 30, 30, 100),
     ARP(7, 9, 1, 127), SET({P_AHOLD, 1}, {P_TRANS, -12}), CAT(DRONE)},
    {"ONDA PRESA", {RB_SCAN, 60, 0, 10, 80, 0, 0, 110}, {60, 90, 127, 90}, 0, 0, FX(0, 20, 30, 80), CAT(PAD)},
    {"ESTACA", {RB_SCAN, 110, 90, 60, 127, 0, 20, 110}, {0, 60, 0, 30}, 0, 0, FX(40, 0, 10, 30), SET({P_TRANS, -12}),
     CAT(PERC)},
    {"VIDRO", {RB_SCAN, 20, 10, 120, 0, 20, 0, 120}, {20, 90, 120, 80}, 0, 0, FX(0, 20, 40, 70), CAT(KEYS)},
};

static const engine_t ENG_ROBO = {
    .name = "ROBO",
    .page_title = {"VOICE", "MOVE"},
    .edit = {
        {"MODE", F_ENUM, 0, RB_COUNT - 1, 0, N_ROBO_MODE, 0},
        {"FORM", F_PCT, 0, 127, 60, 0, 0},           /* GENDY STEP, WALSH SEQ (robo_desc) */
        {"PULS", F_INT, 0, 127, 30, 0, 0},           /* (1 .. 8: robo_desc) GENDY PTS, WALSH TERM */
        {"DCAY", F_PCT, 0, 127, 50, 0, 0},           /* GENDY TIME, WALSH BLND */
        {"TRK", F_PCT, 0, 127, 64, 0, 0},            /* GENDY SEED, WALSH SWP */
        {"DRFT", F_PCT, 0, 127, 0, 0, 0},
        {"CRSH", F_PCT, 0, 127, 0, 0, 0},
        {"TONE", F_CUTOFF, 0, 127, 110, 0, 0},
    },
    .presets = ROBO_PRESETS,
    .npresets = sizeof ROBO_PRESETS / sizeof ROBO_PRESETS[0],
    .note_on = robo_note_on,
    .render = robo_render,
    .fil_page = -1,
    .color = 0x9EDD,
    .macro = {P_E1, P_E3, P_E5, P_E7},
    .desc = robo_desc,
};
