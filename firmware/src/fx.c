/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Effects: per-track DIST insert, then sends into three
 * shared buses (chorus, tempo delay, reverb). Mono buses (the PLATE reverb: stereo), stereo dry mix.
 * Jangada: three reverb models (G_RTYPE) in the same buffers: ROOM (Felucca's, the default), SPRING (from
 * Felucca 1.0) and PLATE (the feedback delay network of SLOOP 2.2, in ROOM's buffers). */
#define DLY_LEN 65536u           /* 1.49 s: 1/4 at 40 BPM fits */
#define CHO_LEN 2048u
static int16_t dly_buf[DLY_LEN] __attribute__((section(".pool")));
static int16_t cho_buf[CHO_LEN] __attribute__((section(".pool")));
static const uint16_t REV_COMB[4] = {1116, 1188, 1277, 1356};
static const uint16_t REV_AP[2] = {556, 441};
static int16_t rev_comb[1116 + 1188 + 1277 + 1356] __attribute__((section(".pool")));
static union {                          /* ROOM's and PLATE's allpasses; SPRING's allpass chain (int32: no clamps) */
    int16_t ap[556 + 441];
    int32_t sp[(556 + 441) / 2];
} rev_u __attribute__((section(".pool")));
#define rev_ap (rev_u.ap)
static struct {
    uint32_t dly_w, cho_w, cho_ph;
    int32_t dly_lp;
    uint16_t comb_i[4], ap_i[2];
    int32_t comb_lp[4];
    uint8_t rtype;                       /* the reverb model running (G_RTYPE: 0 ROOM, 1 SPRING, 2 PLATE) */
    uint16_t sp_w;                       /* SPRING: the loop's write index (SP_MASK) */
    int32_t sp_lp, sp_hp, sp_he, sp_size;   /* .. its loop low-pass, low cut (and its remainder), the loop
                                             * length (Q8, glides) */
    uint32_t sp_ph;                      /* .. the output tap's wobble */
    uint16_t pl_i[4];                    /* PLATE: the four lines' indices, their loop low-passes, the LFO */
    int32_t pl_lp[4];
    uint32_t pl_ph;
} fx;

/* SPRING (G_RTYPE 1, Felucca 1.0): one spring of a spring tank, mono like the other buses, in the ROOM's own
 * buffers (no RAM of its own): the input and the loop's return -> a low cut (~110 Hz: a spring carries little
 * bass) -> SP_N stretched first-order allpasses, (a + z^-4) / (1 + a z^-4) (after Valimaki, Parker and Abel:
 * below fs / 8 = 5.5 kHz the group delay rises with frequency, the chirp; each pass round the loop adds more
 * of it: the "boing", the drips) -> the loop's delay line (rev_comb, SP_LEN) -> back through a one-pole
 * low-pass (DAMP) and the decay gain (SIZE). The output: the spring's far end, half way along the loop, plus a
 * second, quieter pickup at three quarters. SIZE sets the loop's length (30 .. 60 ms) and its decay. */
#define SP_LEN 4096u                     /* the loop's line in rev_comb (4937 samples) */
#define SP_MASK (SP_LEN - 1u)
#define SP_N 10u                         /* allpass stages */
#define SP_A 2867                        /* their coefficient, Q12 */
_Static_assert(sizeof rev_comb / 2u >= SP_LEN && sizeof rev_u.sp / 4u >= 4u * (SP_N + 1u), "SPRING in ROOM's buffers");

/* PLATE (G_RTYPE 2, after SLOOP 2.2): two input diffusers (rev_ap), then four delay lines (in rev_comb)
 * mixed by a Hadamard matrix: every echo feeds all four, so it thickens instead of ringing like a comb;
 * damped in the loop, one line slowly modulated (no metallic tone on long tails); left and right take
 * different lines. SLOOP's lines are 35..63 ms; here 25..30 ms to fit ROOM's buffers: denser (a plate),
 * the loop gain raised to keep the decay times (SIZE: RT60 ~0.4 .. 4 s). */
#define PL_MOD 12                        /* samples the modulated line moves (+-) */
static const uint16_t PL_LINE[4] = {1109, 1193, 1277, 1327};   /* coprime */
_Static_assert(1109 + PL_MOD + 2 + 1193 + 1277 + 1327 <= sizeof rev_comb / 2u, "PLATE in ROOM's buffers");

/* DIST SOFT (P_DTYPE 0; the others: grit.c): low cut -> drive (1x..8x, exponential) -> asymmetric soft clip
 * (a little bias = even harmonics) -> tone low-pass that closes with drive ->
 * make-up gain. State per part (track_t dist_*). */
static void dist_soft(track_t *t, int32_t *b, uint32_t n, int32_t d)
{
    int32_t i, g, k, mk, bias = 2400, b0;
    if (!d)
        return;                                         /* states kept: switching on does not click */
    g = 4096 + d * d * 2;                                /* Q12: 1x .. ~9x, gentle at first */
    k = 32000 - d * 95;                                  /* tone: transparent at low drive .. ~3 kHz, Q15 */
    mk = 30000 - d * 120;                                /* make-up */
    b0 = softclip(bias);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = b[i], y;
        t->dist_hp += (x - t->dist_hp + 64) >> 7;           /* ~55 Hz low cut: keep the bass out of the clipper */
        x = clamp(x - t->dist_hp, -230000, 230000);         /* (x >> 2) * g fits 32 bits; the clip is flat out there */
        y = softclip((((x >> 2) * g) >> 10) + bias) - b0;   /* >> 2 first: no overflow for loud poly */
        t->dist_lp1 += mulq15(y - t->dist_lp1, k);         /* two poles: tames the fizz */
        t->dist_lp2 += mulq15(t->dist_lp1 - t->dist_lp2, k);
        b[i] = mulq15(t->dist_lp2, mk);
    }
}

#include "grit.c"             /* Jangada GRIT: the other DIST types, TAPE and HUM on the master */

/* the DIST insert of a track: SOFT (above, the default) or a GRIT type (P_DTYPE) */
static void track_dist(track_t *t, int32_t *b, uint32_t n)
{
    int32_t d = t->p[P_DIST];
    uint32_t mode = d ? 1u + (uint32_t)clamp(t->p[P_DTYPE], DT_SOFT, DT_RING) : 0u, old = t->dist_mode;
    if (mode != old) {
        t->dist_mode = (uint8_t)mode;
        if (old > 1u || mode > 1u) {                    /* a GRIT type comes or goes: a crossfade */
            dist_xfade(t, b, n, old, mode, d);
            return;
        }
    }
    if (mode == 1u)
        dist_soft(t, b, n, d);
    else if (mode)
        dist_grit(t, b, n, mode - 1u, d);
}

/* master: peak limiter in front of the soft clipper. Fast attack (~0.1 ms),
 * ~150 ms release, threshold where tanh is still nearly linear, so chords
 * get quieter instead of crushed. */
#define LIM_T 18000
static int32_t lim_env = LIM_T;
static volatile uint8_t fx_lowcut;     /* settings: 12 dB/oct ~110 Hz for the small speaker */
static int32_t lc_l1, lc_l2, lc_r1, lc_r2, dc_l, dc_r, dce_l, dce_r;

/* DC blocker (~2 Hz), always on: a leaky integrator of the input (Q6 state) subtracted from it.
 * The >> 12 step keeps its remainder (error feedback, 0..4095) and adds it to the next one, so no
 * part of the step is lost: the state follows the input exactly, down to 0 after the sound stops.
 * (It was rounded, and a rounded step of (x - dc) / 4096 stops moving at |x - dc| < 2048: a
 * constant offset of up to +-31 stayed at the output after silence.) */
static inline int32_t dc_block(int32_t x, int32_t *dc, int32_t *err)
{
    int32_t e = (x << 6) - *dc + *err, d = e >> 12;
    *err = e - (d << 12);
    *dc += d;
    return x - ((*dc + 32) >> 6);
}

static int32_t lce[4];
static inline int32_t lowcut1(int32_t x, int32_t *lc, int32_t *err)   /* x minus its one-pole low-pass */
{
    int32_t e = x - *lc + *err, d = e >> 6;
    *err = e - (d << 6);
    *lc += d;
    return x - *lc;
}

/* Jangada (after SLOOP 2.3): the USB audio input at full level (menu USB AUDIO = FULL): the same output
 * stage with its own state, fed as if MASTER were all the way up, so the recording level does not follow
 * the knob. The DAC path (master_out) is untouched. audio.c sets usb_full_now for a render while the
 * computer records; usb_full_block runs after the block, out of line (mix_block's loops stay as they were). */
static uint8_t usb_full;                 /* menu USB AUDIO: 0 MASTER (follows the knob), 1 FULL (panel.c lights_word) */
#if FELUCCA_UAC
static uint8_t usb_full_now;             /* this render fills usb_out (audio.c) */
static int32_t usb_out[2u * CTL];
static struct { int32_t lim, dc[2], dce[2], lc[4], lce[4]; } uo = {LIM_T, {0, 0}, {0, 0}, {0, 0, 0, 0}, {0, 0, 0, 0}};
#endif

static inline void master_out(int32_t *l, int32_t *r)
{
    int32_t al, ar, a;
    *l = dc_block(*l, &dc_l, &dce_l);
    *r = dc_block(*r, &dc_r, &dce_r);
    if (fx_lowcut) {                  /* two one-pole high-passes, error feedback as dc_block (the */
        *l = lowcut1(*l, &lc_l1, &lce[0]);          /* rounded step stopped at |x - lc| < 32: an offset) */
        *l = lowcut1(*l, &lc_l2, &lce[1]);
        *r = lowcut1(*r, &lc_r1, &lce[2]);
        *r = lowcut1(*r, &lc_r2, &lce[3]);
    }
    al = *l < 0 ? -*l : *l;
    ar = *r < 0 ? -*r : *r;
    a = al > ar ? al : ar;
    if (a > lim_env)
        lim_env += (a - lim_env) >> 2;
    else if (lim_env > LIM_T)
        lim_env -= ((lim_env - LIM_T) >> 12) + 1;
    if (lim_env > LIM_T) {
        int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)lim_env);   /* < 32768 */
        *l = ((*l >> 4) * g) >> 11;                      /* >> 4 first: |l| may be far above Q15 */
        *r = ((*r >> 4) * g) >> 11;
    }
    *l = softclip(*l);
    *r = softclip(*r);
}

/* length of one division (N_DIVL order; N_DIV is its prefix) in samples at the song tempo */
static const uint16_t DIV_Q24[10] = {24, 12, 6, 3, 8, 4, 48, 96, 192, 384};   /* in 1/24 beat */
static uint32_t div_samples(uint32_t div)
{
    return (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM] * DIV_Q24[div % 10u] / 24u;
}

static uint32_t delay_samples(void)
{
    static const uint8_t DLY_Q24[8] = {24, 12, 6, 3, 8, 4, 18, 9};   /* N_DLY in 1/24 beat: + 1/8 and 1/16 dotted */
    uint32_t s = (uint32_t)FS * 60u / (uint32_t)song.g[G_BPM] * DLY_Q24[(uint32_t)song.g[G_DTIME] % 8u] / 24u;
    return s < 16u ? 16u : s >= DLY_LEN ? DLY_LEN - 1u : s;
}

/* ROOM (G_RTYPE 0): 4 damped combs + 2 allpasses (Freeverb-like, mono), added to out */
static __attribute__((noinline)) void rev_room(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k;
    int32_t size = 25000 + song.g[G_RSIZE] * 50, damp = 32767 - song.g[G_RDAMP] * 200;
    for (i = 0; i < n; i++) {
        int32_t a = 0;
        int16_t *c = rev_comb;
        int32_t in = mulq15(rev_in[i], 2580);           /* 1/8 at -4 dB */
        for (k = 0; k < 4u; k++) {
            int32_t o = c[fx.comb_i[k]];
            fx.comb_lp[k] = o + mulq15(fx.comb_lp[k] - o, 32767 - damp);
            c[fx.comb_i[k]] = (int16_t)clamp(in + mulq15(fx.comb_lp[k], size), -32768, 32767);
            if (++fx.comb_i[k] >= REV_COMB[k])
                fx.comb_i[k] = 0;
            a += o;
            c += REV_COMB[k];
        }
        c = rev_ap;
        for (k = 0; k < 2u; k++) {
            int32_t o = c[fx.ap_i[k]];
            int32_t v = a + (o >> 1);
            c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
            a = o - a;                                  /* Freeverb: out = buf - in (o - v would be a notch comb) */
            if (++fx.ap_i[k] >= REV_AP[k])
                fx.ap_i[k] = 0;
            c += REV_AP[k];
        }
        out[i] += a;
    }
}

/* SPRING (see the top), added to out */
static __attribute__((noinline)) void rev_spring(const int32_t *rev_in, int32_t *out, uint32_t n)
{
    uint32_t i, k, s = (uint32_t)song.g[G_RSIZE];
    int32_t g = 19661 + (int32_t)s * 85;                /* the loop's gain: 0.6 .. 0.93 */
    int32_t kl = 26000 - song.g[G_RDAMP] * 160;         /* its low-pass: ~9 kHz .. ~1.3 kHz */
    int32_t len = (int32_t)(1323u + ((s * 1323u) >> 7)) << 8, L, L2, L3, f, w;
    int16_t *ln = rev_comb;
    int32_t *ap = rev_u.sp;
    if (!fx.sp_size)
        fx.sp_size = len;
    fx.sp_size += clamp(len - fx.sp_size, -256, 256);   /* SIZE glides (a sample a block at most) */
    L = fx.sp_size >> 8;
    fx.sp_ph += 2u * LFO_INC[24];                       /* the wobble: a slow sine, 1.5 samples deep */
    w = (fx.sp_size >> 1) + ((osc_sine(fx.sp_ph) * 3) >> 8);   /* the far end, Q8 */
    L2 = w >> 8;
    f = w & 255;
    L3 = (L * 3) >> 2;
    for (i = 0; i < n; i++) {
        uint32_t wp = fx.sp_w, j = (wp & 3u) * (SP_N + 1u);
        int32_t x = mulq15(rev_in[i], 2580), r = ln[(wp - (uint32_t)L) & SP_MASK], p, o;
        int32_t t0 = ln[(wp - (uint32_t)L2) & SP_MASK], t1 = ln[(wp - (uint32_t)L2 - 1u) & SP_MASK];
        fx.sp_lp += mulq15(r - fx.sp_lp, kl);
        o = fx.sp_lp * g;
        x += (o + ((o >> 31) & 32767)) >> 15;           /* towards 0: a loop of floors would hold an offset */
        o = x - fx.sp_hp + fx.sp_he;                    /* the low cut, its step's remainder kept */
        fx.sp_he = o & 63;
        fx.sp_hp += o >> 6;
        x -= fx.sp_hp;
        p = ap[j];                                      /* the chain: ap[j + k], stage k's output 4 samples ago */
        ap[j] = x;
        for (k = 1; k <= SP_N; k++) {                   /* (lossless: bounded by the loop's input, no clamp) */
            int32_t v = (x - ap[j + k]) * SP_A;
            o = ap[j + k];
            x = ((v + ((v >> 31) & 4095)) >> 12) + p;
            p = o;
            ap[j + k] = x;
        }
        ln[wp & SP_MASK] = (int16_t)clamp(x, -32768, 32767);
        fx.sp_w = (uint16_t)(wp + 1u);
        out[i] += (t0 + (((t1 - t0) * f) >> 8)) * 4 + ln[(wp - (uint32_t)L3) & SP_MASK] * 2;
    }
}

/* a * b >> 15 rounded towards 0: a loop of floors would keep a small offset and noise going for ever */
static inline int32_t mulq15z(int32_t a, int32_t b)
{
    int32_t v = a * b;
    return (v + ((v >> 31) & 32767)) >> 15;
}

/* PLATE (see the top), added to out_l / out_r */
static __attribute__((noinline)) void rev_plate(const int32_t *rev_in, int32_t *out_l, int32_t *out_r, uint32_t n)
{
    uint32_t i, k;
    const uint32_t L0 = PL_LINE[0] + PL_MOD + 2u, B1 = L0, B2 = B1 + PL_LINE[1], B3 = B2 + PL_LINE[2];
    int32_t g = 22540 + song.g[G_RSIZE] * 69, lpk = 32767 - song.g[G_RDAMP] * 200;   /* loop gain, damping */
    int32_t m0 = osc_sine(fx.pl_ph), m1, ma, mb;
    fx.pl_ph += LFO_INC[30];
    m1 = osc_sine(fx.pl_ph);
    ma = (PL_MOD << 8) + ((m0 * PL_MOD) >> 7);
    mb = (PL_MOD << 8) + ((m1 * PL_MOD) >> 7);
    for (i = 0; i < n; i++) {
        int32_t a = mulq15(rev_in[i], 13000), o0, o1, o2, o3;
        {
            int16_t *c = rev_ap;
            for (k = 0; k < 2u; k++) {
                int32_t b = c[fx.ap_i[k]], v = a + (b >> 1);
                c[fx.ap_i[k]] = (int16_t)clamp(v, -32768, 32767);
                a = b - (v >> 1);
                if (++fx.ap_i[k] >= REV_AP[k])
                    fx.ap_i[k] = 0;
                c += REV_AP[k];
            }
        }
        {
            int16_t *c = rev_comb;
            int32_t s0, s1, d0, d1, r = ma + (((mb - ma) * (int32_t)i) >> CTL_LOG2);
            uint32_t ri = fx.pl_i[0] + ((uint32_t)r >> 8), rj;
            if (ri >= L0)
                ri -= L0;
            rj = ri + 1u >= L0 ? 0u : ri + 1u;
            o0 = c[ri] + (((c[rj] - c[ri]) * (r & 255)) >> 8);
            o1 = c[B1 + fx.pl_i[1]];
            o2 = c[B2 + fx.pl_i[2]];
            o3 = c[B3 + fx.pl_i[3]];
            s0 = o0 + o1, d0 = o0 - o1, s1 = o2 + o3, d1 = o2 - o3;   /* Hadamard / 2: each feeds all four */
            /* (the steps clamped: x 32767 stays in 32 bits) */
            fx.pl_lp[0] += mulq15z(clamp(((s0 + s1) >> 1) - fx.pl_lp[0], -65535, 65535), lpk);
            fx.pl_lp[1] += mulq15z(clamp(((d0 + d1) >> 1) - fx.pl_lp[1], -65535, 65535), lpk);
            fx.pl_lp[2] += mulq15z(clamp(((s0 - s1) >> 1) - fx.pl_lp[2], -65535, 65535), lpk);
            fx.pl_lp[3] += mulq15z(clamp(((d0 - d1) >> 1) - fx.pl_lp[3], -65535, 65535), lpk);
            c[fx.pl_i[0]] = (int16_t)clamp(mulq15z(fx.pl_lp[0], g) + a, -32768, 32767);
            c[B1 + fx.pl_i[1]] = (int16_t)clamp(mulq15z(fx.pl_lp[1], g) - a, -32768, 32767);
            c[B2 + fx.pl_i[2]] = (int16_t)clamp(mulq15z(fx.pl_lp[2], g) + a, -32768, 32767);
            c[B3 + fx.pl_i[3]] = (int16_t)clamp(mulq15z(fx.pl_lp[3], g) - a, -32768, 32767);
            if (++fx.pl_i[0] >= L0) fx.pl_i[0] = 0;
            if (++fx.pl_i[1] >= PL_LINE[1]) fx.pl_i[1] = 0;
            if (++fx.pl_i[2] >= PL_LINE[2]) fx.pl_i[2] = 0;
            if (++fx.pl_i[3] >= PL_LINE[3]) fx.pl_i[3] = 0;
        }
        out_l[i] += o0 + o2;
        out_r[i] += o1 - o3;
    }
}

/* the reverb's buffers and states to silence (the model changed) */
static void rev_clear(void)
{
    uint32_t i;
    for (i = 0; i < sizeof rev_comb / 2u; i++)
        rev_comb[i] = 0;
    for (i = 0; i < sizeof rev_u.ap / 2u; i++)          /* (int16: 997 of them, an odd count the int32 view misses one of) */
        rev_u.ap[i] = 0;
    for (i = 0; i < 4u; i++) {
        fx.comb_lp[i] = fx.pl_lp[i] = 0;
        fx.comb_i[i] = fx.pl_i[i] = 0;
    }
    fx.ap_i[0] = fx.ap_i[1] = 0;
    fx.sp_lp = fx.sp_hp = fx.sp_he = 0;
}

static void rev_run(uint32_t type, const int32_t *rev_in, int32_t *l, int32_t *r, uint32_t n)
{
    if (type == 2u)
        rev_plate(rev_in, l, r, n);
    else if (type == 1u)
        rev_spring(rev_in, l, n);
    else
        rev_room(rev_in, l, n);
}

/* process the three buses for one block; sends in, wet out (stereo: the mono buses in both) */
static int32_t rev_fade_l[CTL], rev_fade_r[CTL];
static void fx_buses(const int32_t *cho_in, const int32_t *dly_in, const int32_t *rev_in, int32_t *wet_l,
                     int32_t *wet_r, uint32_t n)
{
    uint32_t i, rt = (uint32_t)clamp(song.g[G_RTYPE], 0, 2), dl = delay_samples();
    int32_t fb = song.g[G_DFDBK] * 230, col = 2000 + song.g[G_DCOLOR] * 240;
    int32_t dmix = song.g[G_DMIX] * 258;
    int32_t cdepth = song.g[G_CDEPTH] * 6;
    uint32_t cinc = LFO_INC[song.g[G_CRATE] & 127] / CTL;
    for (i = 0; i < n; i++) {
        int32_t y = 0, x, r;
        /* chorus: modulated short delay, 5..15 ms */
        cho_buf[fx.cho_w & (CHO_LEN - 1u)] = (int16_t)clamp(cho_in[i] >> 1, -32768, 32767);
        fx.cho_ph += cinc;
        r = (400 << 8) + ((osc_sine(fx.cho_ph) + 32768) * cdepth >> 8);   /* Q8 delay: read between samples */
        {
            uint32_t ri = (uint32_t)r >> 8;
            int32_t f = r & 255, c0 = cho_buf[(fx.cho_w - ri) & (CHO_LEN - 1u)];
            int32_t c1 = cho_buf[(fx.cho_w - ri - 1u) & (CHO_LEN - 1u)];
            y += (c0 + (((c1 - c0) * f) >> 8)) << 1;
        }
        fx.cho_w++;
        /* delay with a low-passed feedback */
        x = dly_buf[(fx.dly_w - dl) & (DLY_LEN - 1u)];
        fx.dly_lp += mulq15(x - fx.dly_lp, col);
        dly_buf[fx.dly_w & (DLY_LEN - 1u)] =
            (int16_t)clamp((dly_in[i] >> 1) + mulq15(fx.dly_lp, fb), -32768, 32767);
        fx.dly_w++;
        y += mulq15(x << 1, dmix);
        wet_l[i] = y;
    }
    if (rt != fx.rtype) {                               /* the model changed: the old one's block fades out, */
        int32_t g = 65536, d = 65536 / (int32_t)n;      /* its buffers are cleared, the new one starts from silence */
        for (i = 0; i < n; i++)
            rev_fade_l[i] = rev_fade_r[i] = 0;
        rev_run(fx.rtype, rev_in, rev_fade_l, rev_fade_r, n);
        if (fx.rtype != 2u)
            for (i = 0; i < n; i++)
                rev_fade_r[i] = rev_fade_l[i];
        for (i = 0; i < n; i++, g -= d) {
            wet_r[i] = wet_l[i] + mulq16(rev_fade_r[i], (uint32_t)g);
            wet_l[i] += mulq16(rev_fade_l[i], (uint32_t)g);
        }
        rev_clear();
        fx.rtype = (uint8_t)rt;
        return;
    }
    if (rt == 2u) {                                     /* PLATE: stereo */
        for (i = 0; i < n; i++)
            wet_r[i] = wet_l[i];
        rev_plate(rev_in, wet_l, wet_r, n);
        return;
    }
    rev_run(rt, rev_in, wet_l, wet_r, n);               /* ROOM / SPRING: mono, as before */
    for (i = 0; i < n; i++)
        wet_r[i] = wet_l[i];
}

/* one block of the whole mix (shared with tests/hostsim.c): events -> each part
 * -> dist -> SLICER -> level / pan / sends -> drums (-> SLICER) -> buses -> master; out: stereo Q15 */
static void events_block(uint32_t n);                    /* seq.c */
static uint32_t clk_adv;                                 /* seq.c: units the beat clock moved this block */
static int32_t send_c[CTL], send_d[CTL], send_r[CTL], wet[CTL], wet_r[CTL], mix_l[CTL], mix_r[CTL], part_buf[CTL];

/* ---- Jangada: the beat clock (after SLOOP), in units of a sample at 1 BPM: a beat is BEAT_U at any
 * tempo, so a tempo change keeps the place in the beat. seq_start zeroes it with step 0; seq.c
 * events_block advances it while playing. The punch-in loops and gate and the DUCK curve read it. */
#define BEAT_U ((uint32_t)FS * 60u)
static uint32_t clk_pos;
static uint32_t clk_beat;                               /* beats since play (the TRACKS screen: bar.beat) */

/* ---- DUCK: every kick (GM 35 / 36 on the drum track: drums.kick) dips the synth parts, which come
 * back over an eighth note: depth G_DUCK, the curve (1 - t / T)^2. At 0 nothing changes. */
static struct {
    uint32_t t;                                         /* units since the kick */
    int32_t g0, g1;                                     /* the parts' gain at the block start, end (Q15) */
} duck = {0xFFFFFFFFu, 32767, 32767};

static void duck_block(uint32_t adv)
{
    int32_t depth = song.g[G_DUCK] * 258, x;
    uint32_t len = BEAT_U / 2u;
    duck.g0 = duck.g1;
    if (drums.kick) {
        drums.kick = 0;
        duck.t = 0;
    }
    if (!depth || duck.t >= len) {
        duck.g1 = 32767;
        return;
    }
    x = 32767 - (int32_t)((duck.t << 10) / (len >> 5));      /* 1 - t / T, Q15 (t < len < 2^22) */
    if (x < 0)
        x = 0;
    duck.g1 = 32767 - mulq15(depth, mulq15(x, x));
    duck.t = duck.t + adv < duck.t ? 0xFFFFFFFFu : duck.t + adv;
}

/* ---- the DJ filter: v < 0 a low-pass closing, > 0 a high-pass opening, 0 off. The cutoff glides to the
 * knob (no zipper); at 0 it opens fully, then the filter is bypassed. On the master (G_FILT) and, Jangada 0.7
 * after SLOOP 2.4, on each track (P_TFLT: a synth part's mono signal, the drum track's left, right, reverb) */
typedef struct {
    int32_t cut;                                        /* now, 0..127 << 8 (CUTOFF_HZ index) */
    int8_t mode;                                        /* -1 LP, 1 HP, 0 off */
    int32_t z[3][2];                                    /* the SVF states, per channel */
} djf_t;
static djf_t djf, tflt[NTRK];

/* the knob v -> this block's coefficients in *c; 0 = bypassed (nothing to do) */
static int djf_block(djf_t *f, int32_t v, tsvf_t *c)
{
    int32_t to;
    if (v < 0 && f->mode >= 0) {                        /* (switching side: from open) */
        f->mode = -1;
        f->cut = 127 << 8;
        memset(f->z, 0, sizeof f->z);
    } else if (v > 0 && f->mode <= 0) {
        f->mode = 1;
        f->cut = 0;
        memset(f->z, 0, sizeof f->z);
    }
    if (!f->mode)
        return 0;
    to = f->mode < 0 ? (v < 0 ? (127 << 8) + v * 90 * 4 : 127 << 8) : (v > 0 ? v * 90 * 4 : 0);
    f->cut += clamp(to - f->cut, -384, 384);            /* ~1.5 index a block */
    if (!v && f->cut == to) {
        f->mode = 0;                                    /* fully open again: off */
        return 0;
    }
    tsvf_coef(c, f->cut, 40);
    return 1;
}
/* n samples of channel ch through the filter, the signal at `sh` bits below its level (x within +-140000) */
static void djf_run(djf_t *f, const tsvf_t *c, int32_t *b, uint32_t n, uint32_t ch, uint32_t sh)
{
    uint32_t i;
    int32_t *z = f->z[ch];
    for (i = 0; i < n; i++) {
        int32_t x = clamp(b[i] >> sh, -140000, 140000), y = tsvf_lp(c, x, &z[0], &z[1]);
        b[i] = (f->mode < 0 ? y : x - y) << sh;
    }
}

/* ---- INSERT (Jangada 0.9, after Felucca 1.5, Discussions #78 and #177; GPL-3.0): one effect per synth track, after
 * DIST and before the SLICER, with a dry / wet MIX. TYPE (P_ITYPE, N_ITYPE; stored, append-only) and its three values
 * A B C (P_IA .. P_IC, what they mean: params.c ins_desc):
 *   SOFT HARD FOLD FUZZ  DRIVE (1x .. ~13x, squared), TONE (two one-pole low-passes at F_CUTOFF), LEVEL (F_DB, as the
 *                        track's LEVEL: 112 is 0 dB). A slow low cut in front (~27 Hz), a DC blocker after;
 *   CRUSH                BITS (1 .. 16), RATE (a held sample, 689 Hz .. 44.1 kHz: the old samplers' aliasing), LPF;
 *   PHASR                RATE, DEPTH (the sweep around ~850 Hz), FDBK (0 .. 0.7): four all-pass stages, mono;
 *   FLANG CHOR           RATE, DEPTH, FDBK: a 256-sample line per track; FLANG swept 0.25 .. 5 ms (feedback
 *                        0 .. 0.85), CHOR a sine around 3.2 ms (+-2.3 ms, feedback 0 .. 0.4);
 *   DISP                 AMNT, STRCH (1 .. 16), FDBK: dispersion (Jangada 0.10, an idea from issue #4): eight
 *                        stretched all-passes, (a + z^-M) / (1 + a z^-M) as the SPRING's, a = -0.85 .. 0 with AMNT,
 *                        M = STRCH samples; the phases of the partials pull apart, the spectrum stays: a stiff
 *                        string's chirp at M 1, a metal sheet's at 8 and up. FDBK (0 .. 0.8) around the chain rings
 *                        it at inharmonic frequencies. Fully wet the track's level stays.
 * After DIST, so the GRIT types stack: a FUZZ into a FOLD, a CRUSH into a FLANG. The swept ones are the pedals': their
 * wet is (dry + swept) / 2. MIX 0 or TYPE OFF: the track bit for bit as before (nothing runs). MIX glides; a TYPE
 * change fades the old one out, clears the state, fades the new one in. State in the pool (ins[], ~580 B a track) */
enum { IT_OFF, IT_SOFT, IT_HARD, IT_FOLD, IT_FUZZ, IT_CRUSH, IT_PHASER, IT_FLANGER, IT_CHORUS, IT_DISP, IT_N };
#define IL_LEN 256u                     /* FLANG / CHOR: the line, samples (a power of 2: masked) */
#define PH_ST 4u                        /* PHASR: all-pass stages (two notches, the classic pedal's) */
#define DS_ST 8u                        /* DISP: all-pass stages, */
#define DS_LEN 16u                      /* each one's line (a power of 2: masked; STRCH up to it) */
#define INS_SLOPE (32768 / 128)         /* MIX glides over 128 samples (2.9 ms) */
static const uint8_t INS_HOLD[16] = {64, 48, 40, 32, 24, 20, 16, 12, 10, 8, 6, 5, 4, 3, 2, 1};   /* CRUSH RATE */
/* PHASR: the stages' coefficient over the sweep (Q14), 150 Hz .. 4.8 kHz (Felucca perform.c) */
static const int16_t PH_A[33] = {
    -16038, -15998, -15955, -15906, -15853, -15793, -15727, -15653, -15572, -15481, -15381, -15270, -15147, -15012,
    -14862, -14696, -14514, -14313, -14092, -13849, -13582, -13290, -12970, -12621, -12240, -11825, -11375, -10886,
    -10356, -9784, -9166, -8500, -7783,
};
typedef struct {
    int32_t w;                          /* the wet share now, Q15 */
    uint8_t type;                       /* the type running */
    uint8_t cnt;                        /* CRUSH: samples left of the held one */
    uint16_t wp;                        /* FLANG / CHOR: the line's next write */
    uint32_t ph;                        /* the sweep's phase */
    int32_t hp, lp1, lp2, dc, hold;     /* low cut (Q8), the two low-passes, DC blocker (Q8), CRUSH's held sample */
    int32_t px[PH_ST], py[PH_ST], fb;   /* PHASR: each stage's last input and output, the fed back */
    union {
        int16_t line[IL_LEN];           /* FLANG / CHOR, at a quarter (headroom) */
        int32_t dw[DS_ST * DS_LEN];     /* DISP: each stage's w[n] (direct form II) */
    } u;
} ins_t;
_Static_assert(DS_ST * DS_LEN * 4u <= IL_LEN * 2u, "DISP's lines share the FLANG's");
static ins_t ins[NTRK] __attribute__((section(".pool")));

static inline int32_t ins_mix(int32_t x, int32_t y, int32_t w) { return w == 32768 ? y : x + mulq16(y - x, (uint32_t)w << 1); }
static inline uint32_t ins_tri(uint32_t q) { uint32_t t = q >> 16; return t < 32768u ? t << 1 : (65535u - t) << 1; }
static inline int32_t ins_mixq(int32_t m) { return m >= 127 ? 32768 : m <= 0 ? 0 : m * 258; }   /* MIX -> Q15 */
/* the one-pole low-pass (topology-preserving) coefficient of cutoff index c, Q16: G = g / (1 + g) */
static int32_t ins_lpk(int32_t c)
{
    uint32_t g = SVF_G[clamp(c, 0, 127)];
    return (int32_t)((g << 16) / (4096u + g));
}
static inline int32_t ins_shape(uint32_t ty, int32_t v, uint32_t sh)
{
    switch (ty) {
    case IT_SOFT:
        return softclip(v);
    case IT_HARD:
        return clamp(v, -24000, 24000);
    case IT_FOLD: {
        int32_t u = (int32_t)((uint32_t)(v + 16384) & 0xFFFFu) - 32768;   /* a triangle of period 65536: +-16384 */
        u = 16384 - (u < 0 ? -u : u);
        return u + (u >> 1);
    }
    case IT_FUZZ:
        return v > 0 ? (v > 20000 ? 20000 : v) : softclip(v) >> 1;
    case IT_CRUSH:                                      /* sh: 16 - BITS; to the nearest step */
        return sh ? ((v + (1 << (sh - 1u))) >> sh) << sh : v;
    default:
        return v;
    }
}

/* SOFT .. FUZZ: low cut -> DRIVE -> shape -> TONE -> LEVEL -> DC blocker; CRUSH: held sample -> bits -> LPF */
static __attribute__((noinline)) void ins_drive(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, ty = s->type, crush = ty == IT_CRUSH, sh = 15u - (uint32_t)(clamp(t->p[P_IA], 0, 127) >> 3);
    uint32_t hold = INS_HOLD[clamp(t->p[P_IB], 0, 127) >> 3];
    int32_t a = t->p[P_IA], g = 4096 + a * a * 3;       /* DRIVE, Q12: 1x .. 12.8x, gentle at first */
    int32_t k = ins_lpk(crush ? t->p[P_IC] : t->p[P_IB]), lvl = crush ? 4096 : LEVEL_Q12[t->p[P_IC] & 127];
    for (i = 0; i < n; i++) {
        int32_t x = b[i], y, v;
        s->w += clamp(tw - s->w, -INS_SLOPE, INS_SLOPE);
        if (crush) {
            if (!s->cnt) {
                s->hold = ins_shape(IT_CRUSH, clamp(x, -262144, 262143), sh);
                s->cnt = (uint8_t)hold;
            }
            s->cnt--;
            y = s->hold;
        } else {
            s->hp += ((x << 8) - s->hp) >> 8;            /* ~27 Hz low cut */
            v = clamp(x - (s->hp >> 8), -230000, 230000);
            y = ins_shape(ty, ((v >> 3) * g) >> 9, 0);
        }
        y <<= 4;
        v = mulq16(y - s->lp1, (uint32_t)k);             /* TONE / LPF: two one-pole low-passes */
        y = v + s->lp1;
        s->lp1 = y + v;
        v = mulq16(y - s->lp2, (uint32_t)k);
        y = v + s->lp2;
        s->lp2 = y + v;
        y = (y + 8) >> 4;
        if (!crush) {
            y = (y * lvl) >> 12;                        /* LEVEL */
            s->dc += ((y << 8) - s->dc) >> 9;           /* DC blocker, ~14 Hz (FUZZ's lopsided halves) */
            y -= s->dc >> 8;
        }
        b[i] = ins_mix(x, y, s->w);
    }
}

/* PHASR, FLANG, CHOR: the sweep at RATE (one LFO per track), DEPTH, FDBK */
static __attribute__((noinline)) void ins_swept(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, inc = LFO_INC[clamp(t->p[P_IA], 0, 127)] / CTL, dep = (uint32_t)clamp(t->p[P_IB], 0, 127);
    int32_t c = clamp(t->p[P_IC], 0, 127);
    if (s->type == IT_PHASER) {                         /* the coefficients a block at a time (at its middle) */
        uint32_t tr = ins_tri(s->ph + (n >> 1) * inc), q = 32768u + (uint32_t)(((int32_t)tr - 32768) * (int32_t)dep / 127);
        int32_t a = PH_A[q >> 11] + (((PH_A[(q >> 11) + 1u] - PH_A[q >> 11]) * (int32_t)(q & 2047u)) >> 11);
        int32_t fbk = c * 180;                          /* 0 .. 0.7, Q15 */
        for (i = 0; i < n; i++) {
            int32_t x = b[i], xs = clamp((x + 4) >> 3, -16383, 16383), u = xs + ((s->fb * fbk + 16384) >> 15), y = u;
            uint32_t st;
            s->w += clamp(tw - s->w, -INS_SLOPE, INS_SLOPE);
            for (st = 0; st < PH_ST; st++) {            /* y = a (u - y1) + u1 */
                y = clamp(s->px[st] + ((a * (u - s->py[st]) + 8192) >> 14), -65535, 65535);
                s->px[st] = u;
                s->py[st] = y;
                u = y;
            }
            s->fb = clamp(y, -32767, 32767);
            y <<= 2;
            s->dc += ((y << 8) - s->dc) >> 9;
            b[i] = ins_mix(x, (x >> 1) + y - (s->dc >> 8), s->w);   /* (dry + all-passed) / 2 */
        }
        s->ph += n * inc;
        return;
    }
    {
        int32_t chorus = s->type == IT_CHORUS, fbk = c * (chorus ? 103 : 219);   /* 0 .. 0.4 / 0 .. 0.85, Q15 */
        uint32_t dq = dep * 516u;                       /* DEPTH, Q16 */
        for (i = 0; i < n; i++, s->ph += inc) {
            int32_t x = b[i], r0, r1, v, fr;
            uint32_t d;                                 /* the delay, Q16 samples */
            s->w += clamp(tw - s->w, -INS_SLOPE, INS_SLOPE);
            if (chorus) {
                d = (uint32_t)((140 << 16) + ((osc_sine(s->ph) * (int32_t)dep * 50) >> 5));   /* 3.2 +- 2.3 ms */
            } else {
                uint32_t tr = ins_tri(s->ph), sw = (tr * tr) >> 16;   /* longer near the short end */
                d = (11u << 16) + 209u * (((sw >> 1) * dq) >> 15);  /* 0.25 .. 5 ms */
            }
            r0 = (int32_t)(s->wp - (d >> 16)) & (int32_t)(IL_LEN - 1u);
            r1 = (r0 - 1) & (int32_t)(IL_LEN - 1u);
            fr = (int32_t)((d >> 1) & 0x7FFFu);
            v = s->u.line[r0] + (((s->u.line[r1] - s->u.line[r0]) * fr) >> 15);
            s->u.line[s->wp] = (int16_t)clamp((x >> 2) + softclip(mulq15(v, fbk)), -32767, 32767);
            s->wp = (uint16_t)((s->wp + 1u) & (IL_LEN - 1u));
            v <<= 1;
            s->dc += ((v << 8) - s->dc) >> 9;
            b[i] = ins_mix(x, (x >> 1) + v - (s->dc >> 8), s->w);   /* (dry + delayed) / 2 */
        }
    }
}

/* DISP: AMNT the stages' coefficient, STRCH their delay, FDBK around the chain. Each stage
 * w[n] = u - a w[n-M], y = a w[n] + w[n-M] (Q15 a); its lines hold the track at an eighth (+-65535: no
 * product past 31 bits) */
static __attribute__((noinline)) void ins_disp(ins_t *s, const track_t *t, int32_t *b, uint32_t n, int32_t tw)
{
    uint32_t i, st, m = (uint32_t)clamp(t->p[P_IB], 0, 127) >> 3;   /* STRCH - 1 */
    int32_t a = -clamp(t->p[P_IA], 0, 127) * 219, fbk = clamp(t->p[P_IC], 0, 127) * 206;   /* -0.85 .. 0, 0 .. 0.8 */
    for (i = 0; i < n; i++) {
        int32_t x = b[i], u = clamp((x + 4) >> 3, -32767, 32767) + mulq15(s->fb, fbk), y;
        uint32_t wr = s->wp, rd = (wr - m - 1u) & (DS_LEN - 1u);
        s->w += clamp(tw - s->w, -INS_SLOPE, INS_SLOPE);
        for (st = 0; st < DS_ST; st++) {
            int32_t *w = &s->u.dw[st * DS_LEN], wm = w[rd], wn = clamp(u - mulq15(wm, a), -65535, 65535);
            w[wr] = wn;
            u = mulq15(wn, a) + wm;
        }
        s->wp = (uint16_t)((wr + 1u) & (DS_LEN - 1u));
        s->fb = softclip(clamp(u, -65535, 65535));
        y = u << 3;
        s->dc += ((y << 8) - s->dc) >> 9;
        b[i] = ins_mix(x, y - (s->dc >> 8), s->w);
    }
}

/* the INSERT at rest at once (mix_part: TYPE turned OFF while the part was silent) */
static __attribute__((noinline)) void ins_rest(track_t *t)
{
    memset(&ins[(uint32_t)(t - trk) % NTRK], 0, sizeof(ins_t));
    t->ins_run = 0;
}

/* the track's INSERT on its block b (mix_part): nothing while TYPE is OFF or MIX is 0 and faded */
static __attribute__((noinline)) void track_insert(track_t *t, int32_t *b, uint32_t n)
{
    ins_t *s = &ins[(uint32_t)(t - trk) % NTRK];
    int32_t ty = clamp(t->p[P_ITYPE], 0, IT_N - 1), tw;
    if (s->type != ty && !s->w)                         /* faded out: the new type from rest */
        s->type = (uint8_t)ty;
    tw = s->type == ty && ty ? ins_mixq(t->p[P_IMIX]) : 0;
    if (!s->w) {
        if (!tw) {
            t->ins_run = 0;
            return;                                     /* dry: bit for bit */
        }
        memset(s, 0, sizeof *s);                        /* starting: no stale line or filter */
        s->type = (uint8_t)ty;
    }
    if (s->type == IT_DISP)
        ins_disp(s, t, b, n, tw);
    else if (s->type >= IT_PHASER)
        ins_swept(s, t, b, n, tw);
    else
        ins_drive(s, t, b, n, tw);
    t->ins_run = s->w != 0;                             /* (mix_part calls again while it fades) */
}

/* one synth part into the dry mix and the sends; a part with no voice sounding costs
 * the LFO tick and a cleared buffer only (after the DIST tail has run out) */
static void mix_part(track_t *t, uint32_t n)
{
    int32_t *b = part_buf;
    uint32_t i;
    if (track_render(t, b, n))
        t->tail = t->p[P_ITYPE] ? 64 : 16;              /* blocks of DIST / INSERT state to run out after the last
                                                         * voice (the INSERT's: 46 ms, a flanger's feedback) */
    else if ((!t->tail || !(t->p[P_DIST] | t->p[P_ITYPE]) || !--t->tail) && !slicer_busy(t)) {
        slicer_track(t, 0, n);                          /* (the SLICER's step clock runs on) */
        if (t->ins_run && !t->p[P_ITYPE])
            ins_rest(t);                                /* TYPE OFF while silent: no fade-out left for the next note */
        return;
    }
    {
        int32_t lvl = LEVEL_Q12[t->p[P_LEVEL] & 127], pan = t->p[P_PAN];
        int32_t gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
        int32_t c = t->p[P_CHOR] * 258, d = t->p[P_DLY] * 258, r = t->p[P_REV] * 258, pk = t->peak;
        int32_t xmax = c > d ? c : d, ga = duck.g0, gb = duck.g1;   /* DUCK, ramped over the block */
        xmax = 0x7FFFFFFF / ((xmax > r ? xmax : r) | 1);   /* sends: loud chords at a high LEVEL */
        {                                               /* Jangada DRONES: the tension pushes the DIST */
            int16_t dk = t->p[P_DIST];
            t->p[P_DIST] = (int16_t)clamp(dk + drone_dist(t), 0, 127);
            track_dist(t, b, n);
            t->p[P_DIST] = dk;
        }
        if (t->p[P_ITYPE] | t->ins_run)
            track_insert(t, b, n);                      /* the INSERT (Jangada 0.9): OFF, nothing */
        slicer_track(t, b, n);                          /* slicer.c: before the level, pan and sends */
        {
            tsvf_t fc;                                  /* the track's FILT (P_TFLT), after the SLICER (4x level) */
            djf_t *f = &tflt[(uint32_t)(t - trk) % NTRK];
            if (djf_block(f, t->p[P_TFLT], &fc))
                djf_run(f, &fc, b, n, 0, 2);
        }
        for (i = 0; i < n; i++) {
            int32_t x = ((b[i] >> 2) * lvl) >> 10, a, xs;   /* pre-shift: 8 loud voices */
            if (ga < 32767 || gb < 32767) {
                int32_t g = ga + (((gb - ga) * (int32_t)i) >> CTL_LOG2);
                x = (x >> 4) * (g >> 3) >> 8;           /* (Q15 in two halves: no 32-bit overflow) */
            }
            a = x < 0 ? -x : x;
            xs = clamp(x, -xmax, xmax);                 /* sends: mulq15 would overflow */
            if (a > pk)
                pk = a;
            if (c)
                send_c[i] += mulq15(xs, c);
            if (d)
                send_d[i] += mulq15(xs, d);
            if (r)
                send_r[i] += mulq15(xs, r);
            mix_l[i] += (int32_t)(((int64_t)x * gl) >> 12);   /* 64-bit: x * 4096 overflowed for loud parts */
            mix_r[i] += (int32_t)(((int64_t)x * gr) >> 12);
        }
        t->peak = pk;
    }
}

/* ---- Jangada: the master bus, after SLOOP (isod89/sloop-fm1, GPL-3.0): DUST, the DJ filter and the
 * punch-in effects run on the whole mix, before the volume (Jangada GRIT: HUM and TAPE after DUST,
 * grit.c). All are off (and the mix bit-identical) until used. */
/* ---- DUST: the master through an old sampler and a record. G_DUST 0..127 turns up together: drive
 * into a soft clip, a lower sample rate (held samples, 44.1 -> 11 kHz), fewer bits (15 -> 8), a
 * one-pole low-pass (open -> ~3 kHz), a little hiss and crackle. The hiss and the crackle are the
 * record turning: they fade in with PLAY and out (~0.1 s) at STOP, so a stopped Jangada is silent.
 * Stereo, ~25 ops a sample. */
static struct {
    int32_t hl, hr, hn;                                 /* held samples, samples left to hold */
    int32_t ll, lr;                                     /* low-pass states */
    int32_t rnd, click;                                 /* noise state, a crackle decaying */
    int32_t bed;                                        /* hiss / crackle level, Q15: 0 stopped, 32767 playing */
} dust = {0, 0, 0, 0, 0, 0x2545F491, 0, 0};

static int32_t crush_bits(int32_t v, int32_t shift)    /* fewer bits, rounded toward 0: no DC from tails */
{
    return v >= 0 ? (v >> shift) << shift : -((-v >> shift) << shift);
}

static void dust_process(int32_t *l, int32_t *r, uint32_t n)
{
    int32_t d = song.g[G_DUST], hold, shift, a, drive, hiss, i, bed0, bed1;
    uint32_t pc;
    if (!d) {
        dust.bed = 0;
        return;
    }
    hold = 1 + d * 3 / 127;
    shift = d / 18;
    a = 32767 - d * 165;                                /* one-pole coefficient, Q15 */
    drive = 4096 + d * 24;                              /* Q12: 1x .. 1.75x */
    hiss = d * 2;
    pc = (uint32_t)d * 7u;                              /* crackle: chance per sample, x 2^-22 */
    bed0 = dust.bed;                                    /* ~0.1 s from 0 to full (238 a block of 32) */
    bed1 = dust.bed = clamp(dust.bed + (song.playing ? 238 : -238), 0, 32767);
    for (i = 0; i < (int32_t)n; i++) {
        int32_t x = l[i], y = r[i];
        int32_t bed = bed0 + (((bed1 - bed0) * i) >> CTL_LOG2);
        uint32_t nz = noise32(&dust.rnd);
        if (--dust.hn <= 0) {                           /* sample and hold, then the bits */
            dust.hn = hold;
            dust.hl = softclip(((x >> 2) * drive) >> 10);
            dust.hr = softclip(((y >> 2) * drive) >> 10);
            if (shift) {
                dust.hl = crush_bits(dust.hl, shift);
                dust.hr = crush_bits(dust.hr, shift);
            }
        }
        dust.ll += mulq15(dust.hl - dust.ll, a);
        dust.lr += mulq15(dust.hr - dust.lr, a);
        if ((nz >> 10) < pc)                            /* a speck of dust */
            dust.click = mulq15(((int32_t)(nz & 0x3FFu) - 512) * d / 8, bed);
        x = dust.ll + dust.click + mulq15(((int32_t)(nz >> 16) - 32768) * hiss >> 15, bed);
        y = dust.lr + dust.click + mulq15(((int32_t)(nz & 0xFFFFu) - 32768) * hiss >> 15, bed);
        dust.click -= dust.click >> 2;
        l[i] = x;
        r[i] = y;
    }
}

static void djf_process(int32_t *l, int32_t *r, uint32_t n)
{
    tsvf_t c;
    if (!djf_block(&djf, song.g[G_FILT], &c))
        return;
    djf_run(&djf, &c, l, n, 0, 0);
    djf_run(&djf, &c, r, n, 1, 0);
}

#include "punch.c"            /* PUNCH-IN FX on the whole mix (FX held + a white key) */

#if FELUCCA_UAC
static __attribute__((noinline)) void usb_full_block(uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n && i < CTL; i++) {
        int32_t l = (mix_l[i] >> 2) << 2, r = (mix_r[i] >> 2) << 2, al, ar, a;   /* MASTER 4096 */
        l = dc_block(l, &uo.dc[0], &uo.dce[0]);
        r = dc_block(r, &uo.dc[1], &uo.dce[1]);
        if (fx_lowcut) {
            l = lowcut1(l, &uo.lc[0], &uo.lce[0]);
            l = lowcut1(l, &uo.lc[1], &uo.lce[1]);
            r = lowcut1(r, &uo.lc[2], &uo.lce[2]);
            r = lowcut1(r, &uo.lc[3], &uo.lce[3]);
        }
        al = l < 0 ? -l : l;
        ar = r < 0 ? -r : r;
        a = al > ar ? al : ar;
        if (a > uo.lim)
            uo.lim += (a - uo.lim) >> 2;
        else if (uo.lim > LIM_T)
            uo.lim -= ((uo.lim - LIM_T) >> 12) + 1;
        if (uo.lim > LIM_T) {
            int32_t g = (int32_t)(((uint32_t)LIM_T << 15) / (uint32_t)uo.lim);
            l = ((l >> 4) * g) >> 11;
            r = ((r >> 4) * g) >> 11;
        }
        usb_out[2u * i] = softclip(l);
        usb_out[2u * i + 1u] = softclip(r);
    }
}
#endif

static void mix_block(int32_t *out, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        send_c[i] = send_d[i] = send_r[i] = mix_l[i] = mix_r[i] = 0;
    events_block(n);
    duck_block(clk_adv);                                /* (n x BPM, or the MIDI clock's units: seq.c) */
    for (i = 0; i < NTRK; i++)
        if (trk_synth(i))
            mix_part(&trk[i], n);
    if (is_drum(TDRUM)) {
        static int32_t dl[CTL], dr[CTL], dv[CTL];
        tsvf_t fc;
        djf_t *f = &tflt[TRK_DRUM];
        if (n <= CTL && djf_block(f, TDRUM->p[P_TFLT], &fc)) {   /* the drum track's FILT: left, right, reverb */
            for (i = 0; i < n; i++)
                dl[i] = dr[i] = dv[i] = 0;
            slicer_drums(dl, dr, dv, n);
            djf_run(f, &fc, dl, n, 0, 2);
            djf_run(f, &fc, dr, n, 1, 2);
            djf_run(f, &fc, dv, n, 2, 2);
            for (i = 0; i < n; i++) {
                mix_l[i] += dl[i];
                mix_r[i] += dr[i];
                send_r[i] += dv[i];
            }
        } else {
            slicer_drums(mix_l, mix_r, send_r, n);      /* drums_render, through the SLICER when on */
        }
    } else
        drums_render(mix_l, mix_r, send_r, n);          /* track 4 is a synth: only the drums' tails */
    fx_buses(send_c, send_d, send_r, wet, wet_r, n);
    for (i = 0; i < n; i++) {
        mix_l[i] += wet[i];
        mix_r[i] += wet_r[i];
    }
    dust_process(mix_l, mix_r, n);
    hum_process(mix_l, mix_r, n);                       /* Jangada GRIT: the hum under it, all onto the tape */
    tape_process(mix_l, mix_r, n);
    punch_process(mix_l, mix_r, n);
    djf_process(mix_l, mix_r, n);
    for (i = 0; i < n; i++) {
        int32_t l = ((mix_l[i] >> 2) * (int32_t)song.master_q12) >> 10;
        int32_t r = ((mix_r[i] >> 2) * (int32_t)song.master_q12) >> 10;
        master_out(&l, &r);
        out[2u * i] = l;
        out[2u * i + 1u] = r;
    }
#if FELUCCA_UAC
    if (usb_full_now)                                   /* USB AUDIO = FULL, while the computer records */
        usb_full_block(n);
#endif
}
