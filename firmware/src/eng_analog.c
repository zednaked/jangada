/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ANALOG: two band-limited oscillators (saw / square / tri / sine / PWM),
 * noise, drive and a trapezoidal low-pass.
 *
 * Jangada (EDIT 3 / 4): SUPR up to 6 more copies of oscillator 1, spread by SDTN (a superwave:
 * copy k sits k steps of the spread above or below, so one accumulator of the spread phase drives
 * them all); SUB a square an octave below; DRFT a slow random wander of the pitch per voice;
 * FTYP LP12 (Felucca's), LP24, BP or HP. With more than 4 voices sounding the superwave keeps
 * fewer copies (the CPU, see analog_render_x). With all of them at their defaults the original
 * render runs, sample for sample (analog_render); otherwise analog_render_x. */
static const char *const N_ANALOG_WAVE[] = {"SAW", "SQR", "TRI", "SIN", "PWM"};
static const char *const N_ANALOG_FTYP[] = {"LP12", "LP24", "BP", "HP"};

static void analog_note_on(track_t *t, voice_t *v)
{
    (void)t;
    v->ph[1] = v->ph[0] + 0x40000000u;
    v->ph[2] = 0;                                     /* superwave spread phase */
    v->s[0] = v->s[1] = 0;                            /* filter */
    v->s[3] = v->s[4] = 0;                            /* second stage (LP24) */
    v->s[5] = (int32_t)(v->ph[0] >> 1);               /* sub phase */
    v->s[6] = 0;                                      /* drift */
    if (!v->s[2])
        v->s[2] = 0x1234567 + (int32_t)v->age;        /* noise state */
}

static uint32_t voices_busy(void);                   /* voice.c */
static uint8_t analog_nv;                            /* voices sounding, all parts (analog_block) */
static void analog_block(track_t *t)
{
    (void)t;
    analog_nv = (uint8_t)voices_busy();
}

static inline int32_t analog_osc(uint32_t wave, uint32_t ph, uint32_t inc, uint32_t pw)
{
    switch (wave) {
    case 1:
        return osc_pulse(ph, inc, 0x80000000u);
    case 2:
        return osc_tri(ph);
    case 3:
        return osc_sine(ph);
    case 4:
        return osc_pulse(ph, inc, pw);
    default:
        return osc_saw(ph, inc);
    }
}

/* the Jangada render: superwave, sub, drift, filter types (see the top) */
__attribute__((noinline)) static void analog_render_x(track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                     const vmod_t *m)
{
    static const uint32_t COPY_PH[6] = {0x2B7E1516u, 0x9E3779B9u, 0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au};
    static const int8_t COPY_AT[6] = {1, -1, 2, -2, 3, -3};   /* spread steps of copy k */
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i, k, ncopy = (uint32_t)p[P_E8], ftyp = (uint32_t)p[P_E12];
    int32_t det = p[P_E1], mix = p[P_E2], noise = p[P_E3], sub = p[P_E10], drift = p[P_E11];
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tsvf_t flt;
    int32_t drive = 32768 + p[P_E6] * 512, kq = tsvf_k(p[P_E5]);
    uint32_t inc1 = m->inc, inc2, dinc, subinc;
    int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;
    uint32_t pw = 0x80000000u + (uint32_t)((m->shape - (64 << 8)) << 15);
    int32_t m2 = mix * 258, m1 = 32767 - m2, nz = noise * 200, drv = p[P_E6];
    int32_t cg;
    /* the CPU: fewer copies when many voices sound (all parts share NVOICE): 7 oscillators a voice
     * up to 4 voices, 5 up to 6, 3 above (8 voices of 7 measured 73 % on the FM-1 and lost voices
     * to the shedder; capped: SUPER SAW x 8 voices 55 %). analog_nv: analog_block, once a block */
    if (ncopy > 4u && analog_nv > 4u)
        ncopy = 4;
    if (ncopy > 2u && analog_nv > 6u)
        ncopy = 2;
    cg = 32767 * 10 / (10 + 6 * (int32_t)ncopy);     /* main + copies at 0.6: about the same level */
    int32_t sg = sub * 200;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], spr = v->ph[2], sph = (uint32_t)v->s[5];
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2], jc1 = v->s[3], jc2 = v->s[4];
    if (drift) {                                       /* a random walk per block, cents x 256 */
        int32_t d = v->s[6], lim = drift * 30 * 256 / 127;   /* up to +-30 ct */
        d += ((int32_t)(noise32(&nst) >> 24) - 128) * drift / 8;
        d -= d >> 7;                                  /* drawn back to the pitch */
        d = clamp(d, -lim, lim);
        v->s[6] = d;
        inc1 += (uint32_t)((int32_t)(inc1 >> 12) * ((d >> 8) * 2367 / 1000));
    }
    inc2 = PITCH_INC[clamp(m->pitch16 + d16, 0, 2047)];
    inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    if (drift)
        inc2 += (uint32_t)((int32_t)(inc2 >> 12) * ((v->s[6] >> 8) * 2367 / 1000));
    if (det == 0)
        inc2 = inc1;
    /* spread: SDTN 127 puts the outer copies (3 steps) about 50 ct away */
    dinc = (uint32_t)((int32_t)(inc1 >> 12) * (p[P_E9] * 50 * 2367 / (127 * 3 * 1000)));
    subinc = inc1 >> 1;
    tsvf_coef(&flt, cut, p[P_E5]);
    for (i = 0; i < n; i++) {
        int32_t a = analog_osc(wave, ph0, inc1, pw), b = analog_osc(wave, ph1, inc2, pw), s, y, bp, ab;
        if (ncopy) {
            int32_t sum = 0;
            for (k = 0; k < ncopy; k++)
                sum += analog_osc(wave, ph0 + (uint32_t)(int32_t)COPY_AT[k] * spr + COPY_PH[k],
                                  inc1 + (uint32_t)(int32_t)COPY_AT[k] * dinc, pw);
            sum = ((sum >> 2) * 19661) >> 13;         /* copies at 0.6 (|sum| < 2^18: no overflow) */
            a = mulq15(a, cg) + (((sum >> 2) * cg) >> 13);
        }
        ph0 += inc1;
        ph1 += inc2;
        spr += dinc;
        s = mulq15(a, m1) + mulq15(b, m2);
        if (sg) {
            s += mulq15(osc_pulse(sph, subinc, 0x80000000u), sg);
            sph += subinc;
        }
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)
            s = softclip(((s >> 2) * (drive >> 2)) >> 11);
        y = tsvf_lpbp(&flt, s >> 1, &ic1, &ic2, &bp);
        if (ftyp == 1)
            y = tsvf_lp(&flt, clamp(y, -100000, 100000), &jc1, &jc2);   /* LP24: the low-pass again
                                                                          * (input bounded: no overflow at CUT/RES 127) */
        else if (ftyp == 2)
            y = bp;
        else if (ftyp == 3)
            y = (s >> 1) - ((kq * bp) >> 12) - y;     /* HP = in - k bp - lp */
        ab = y < 0 ? -y : y;                          /* the soft knee of the original */
        if (ab > 16000) {
            ab = 16000 + (softclip((ab - 16000) * 2) >> 1);
            y = y < 0 ? -ab : ab;
        }
        out[i] += mulq15(mulq15(y << 1, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->ph[2] = spr;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
    v->s[3] = jc1;
    v->s[4] = jc2;
    v->s[5] = (int32_t)sph;
}

/* Felucca's render, unchanged: kept in a function of its own (no call in it), so it compiles
 * as it always did */
__attribute__((noinline)) static void analog_render_lp(track_t *t, voice_t *v, int32_t *out, uint32_t n,
                                                      const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t wave = (uint32_t)p[P_E0], i;
    int32_t det = p[P_E1], mix = p[P_E2], noise = p[P_E3];
    int32_t cut = (p[P_E4] << 8) + m->cutoff + (p[P_E7] * (v->pitch16 - 60 * 16) >> 4);
    tsvf_t flt;
    int32_t drive = 32768 + p[P_E6] * 512;                       /* 1x .. 3x */
    uint32_t inc1 = m->inc;
    /* DTN in cents: whole 1/16 semitones from the table, the rest as a fine factor */
    int32_t d16 = det * 16 / 100, rem = det * 16 - d16 * 100;            /* rem: 1/1600 semitone */
    uint32_t inc2 = PITCH_INC[clamp(m->pitch16 + d16, 0, 2047)];
    inc2 += (uint32_t)((int32_t)(inc2 >> 12) * (rem * 2367 / 16000));
    uint32_t pw = 0x80000000u + (uint32_t)((m->shape - (64 << 8)) << 15);
    int32_t m2 = mix * 258, m1 = 32767 - m2;                    /* osc mix Q15 */
    int32_t nz = noise * 200, drv = p[P_E6];
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1];                  /* state in locals: out[] may alias v->s[] */
    int32_t ic1 = v->s[0], ic2 = v->s[1], nst = v->s[2];
    tsvf_coef(&flt, cut, p[P_E5]);
    if (det == 0)
        inc2 = inc1;
    for (i = 0; i < n; i++) {
        int32_t a, b, s;
        switch (wave) {
        case 1:
            a = osc_pulse(ph0, inc1, 0x80000000u);
            b = osc_pulse(ph1, inc2, 0x80000000u);
            break;
        case 2:
            a = osc_tri(ph0);
            b = osc_tri(ph1);
            break;
        case 3:
            a = osc_sine(ph0);
            b = osc_sine(ph1);
            break;
        case 4:
            a = osc_pulse(ph0, inc1, pw);
            b = osc_pulse(ph1, inc2, pw);
            break;
        default:
            a = osc_saw(ph0, inc1);
            b = osc_saw(ph1, inc2);
            break;
        }
        ph0 += inc1;
        ph1 += inc2;
        s = mulq15(a, m1) + mulq15(b, m2);
        if (nz)
            s += mulq15((int32_t)(noise32(&nst) >> 17) - 16384, nz);
        if (drv)
            s = softclip(((s >> 2) * (drive >> 2)) >> 11);   /* pre-shifts: drive is up to 3x, no overflow */
        {   /* filter: linear up to half scale, then a soft knee (only resonance peaks saturate) */
            int32_t y = tsvf_lp(&flt, s >> 1, &ic1, &ic2), a = y < 0 ? -y : y;
            if (a > 16000) {
                a = 16000 + (softclip((a - 16000) * 2) >> 1);
                y = y < 0 ? -a : a;
            }
            s = y << 1;
        }
        out[i] += mulq15(mulq15(s, amp_at(m, i)), VOICE_FS) << 1;
    }
    v->ph[0] = ph0;
    v->ph[1] = ph1;
    v->s[0] = ic1;
    v->s[1] = ic2;
    v->s[2] = nst;
}

static const preset_t ANALOG_PRESETS[] = {
    {"SAW LEAD", {0, 12, 64, 0, 90, 30, 10, 64}, {4, 70, 100, 50}, 20, 1, FX(0, 10, 45, 30), PAT(4)},
    {"SOFT PAD", {0, 20, 64, 4, 60, 10, 0, 32}, {80, 90, 110, 95}, 10, 0, FX(0, 60, 20, 70), PAT(5)},
    {"SQR BASS", {1, 0, 0, 0, 50, 70, 40, 64}, {0, 60, 40, 30}, 30, 1, FX(5, 0, 10, 10), PAT(2)},
    {"PWM STR", {4, 8, 40, 0, 75, 20, 0, 48}, {60, 80, 110, 85}, 8, 0, FX(0, 50, 20, 60), PAT(5)},
    {"ACID", {0, 0, 0, 0, 50, 100, 25, 64}, {0, 55, 20, 30}, 48, 1, FX(20, 0, 45, 15), PAT(1)},
    {"SINE KEY", {3, 6, 50, 0, 127, 0, 0, 0}, {2, 80, 30, 70}, 0, 0, FX(0, 30, 25, 40), PAT(6)},
    {"RAVE", {4, 30, 64, 0, 85, 20, 30, 64}, {20, 80, 110, 60}, 10, 1, FX(30, 40, 30, 30), ARP(1, 2, 2, 50)},
    {"SUB BASS", {3, 0, 0, 0, 40, 0, 20, 0}, {0, 60, 100, 20}, 0, 1, FX(0, 0, 0, 10), PAT(8)},
    {"PLUCK", {0, 8, 50, 0, 30, 40, 0, 64}, {0, 88, 0, 60}, 55, 0, FX(0, 20, 50, 30), PAT(3)},
    {"BRASS", {0, 10, 64, 0, 45, 20, 10, 64}, {35, 70, 90, 45}, 40, 0, FX(0, 20, 20, 40), PAT(6)},
    {"WIND", {0, 0, 0, 90, 30, 90, 0, 0}, {60, 90, 60, 80}, 50, 0, FX(0, 30, 30, 70), PAT(5)},
    {"STRINGS", {0, 25, 64, 0, 70, 10, 0, 32}, {70, 90, 115, 90}, 5, 0, FX(0, 60, 20, 70), PAT(5)},
    /* Jangada: dark / industrial */
    {"RUST BASS", {0, 14, 64, 6, 38, 70, 110, 40}, {0, 60, 90, 25}, 30, 1, FX(70, 0, 0, 12), PAT(2),
     .x = {1, 1, 71, 9, 2}},                        /* + SUB, a little DRFT, LP24 */
    {"HURT PAD", {4, 26, 64, 10, 55, 25, 15, 30}, {95, 90, 115, 105}, 6, 0, FX(0, 70, 25, 95), .x = {1, 1, 1, 41, 2},
     SET({P_LRATE, 18}, {P_LD_FLT, 16}, {P_LD_SHP, 30}, {P_M1SRC, 5}, {P_M1DST, 4}, {P_M1AMT, 14})},
    /* Jangada: drones */
    {"DRONE SAW", {0, 40, 64, 20, 45, 35, 40, 20}, {120, 90, 127, 120}, 0, 0, FX(15, 60, 35, 110), ARP(7, 9, 1, 127),
     .x = {7, 61, 51, 51, 2},                       /* superwave 6, SUB, DRFT, LP24 */
     SET({P_AHOLD, 1}, {P_LRATE, 6}, {P_LD_FLT, 24}, {P_M1SRC, 1}, {P_M1DST, 8}, {P_M1AMT, 18}, {P_M2SRC, 5}, {P_M2DST, 4}, {P_M2AMT, 20})},
    /* Jangada: superwave */
    {"SUPER SAW", {0, 12, 0, 0, 95, 15, 0, 40}, {2, 80, 110, 55}, 10, 0, FX(0, 30, 40, 60), .x = {7, 51, 1, 11, 1}},
    {"SUPER PAD", {0, 20, 64, 0, 60, 20, 0, 30}, {90, 90, 115, 100}, 6, 0, FX(0, 50, 30, 90), .x = {6, 71, 1, 31, 2},
     SET({P_LRATE, 12}, {P_LD_FLT, 14})},
    {"HP SHIMMER", {0, 30, 64, 10, 70, 60, 0, 64}, {60, 90, 110, 100}, 0, 0, FX(0, 60, 50, 100), .x = {5, 81, 1, 21, 4},
     SET({P_LRATE, 9}, {P_LD_FLT, 20})},
    /* Jangada: the Nordeste (with the MANGUE kit; SCL NORD or MIX) */
    {"BAIAO BASS", {1, 8, 64, 0, 36, 40, 40, 50}, {0, 50, 60, 20}, 35, 1, FX(20, 0, 0, 10), PAT(12),
     .x = {1, 1, 51, 11, 2}},                       /* round and plucked, + SUB, LP24 */
    {"RABECA", {0, 8, 64, 12, 70, 60, 30, 40}, {40, 90, 127, 90}, 0, 0, FX(10, 30, 20, 80), ARP(7, 9, 1, 127),
     .x = {1, 1, 1, 31, 3},                         /* the fiddle: nasal (BP), bow noise, its vibrato; a drone */
     SET({P_AHOLD, 1}, {P_LRATE, 89}, {P_LD_PIT, 2})},
    /* Jangada DRONES that evolve (drone.c): rust on a superwave; the walks move it, the tension opens it
     * over 16 bars (RES, DRV, the superwave's spread) */
    {"FERRUGEM", {0, 30, 64, 22, 34, 40, 30, 20}, {120, 90, 127, 120}, 0, 0, FX(25, 50, 30, 115), ARP(7, 9, 1, 127),
     .x = {7, 41, 61, 41, 2},                       /* superwave 6, SUB, DRFT, LP24 */
     SET({P_AHOLD, 1}, {P_LRATE, 5}, {P_LD_FLT, 10}, {P_EVOL, 90}, {P_TENS, 110}, {P_TRAMP, 5})},
};

static void analog_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    if (p[P_E8] || p[P_E10] || p[P_E11] || p[P_E12])     /* Jangada's features: their own render */
        analog_render_x(t, v, out, n, m);
    else
        analog_render_lp(t, v, out, n, m);
}
static const engine_t ENG_ANALOG = {
    "ANALOG", {"OSC", "FLT"},
    {
        {"WAVE", F_ENUM, 0, 4, 0, N_ANALOG_WAVE, 0},
        {"DTN", F_INT, 0, 127, 10, 0, "ct"},
        {"MIX", F_PCT, 0, 127, 64, 0, 0},
        {"NOIS", F_PCT, 0, 127, 0, 0, 0},
        {"CUT", F_CUTOFF, 0, 127, 90, 0, 0},
        {"RES", F_PCT, 0, 127, 30, 0, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"KTR", F_PCT, 0, 127, 64, 0, 0},
        {"SUPR", F_INT, 0, 6, 0, 0, 0},              /* Jangada: EDIT 3 */
        {"SDTN", F_PCT, 0, 127, 40, 0, 0},
        {"SUB", F_PCT, 0, 127, 0, 0, 0},
        {"DRFT", F_PCT, 0, 127, 0, 0, 0},
        {"FTYP", F_ENUM, 0, 3, 0, N_ANALOG_FTYP, 0},  /* EDIT 4 */
    },
    ANALOG_PRESETS, sizeof(ANALOG_PRESETS) / sizeof(ANALOG_PRESETS[0]), 1, analog_note_on, analog_render,
    0xF986, {P_E4, P_E5, P_ATK, P_REL},
    .block = analog_block,                           /* Jangada: the superwave's voice count */
};
