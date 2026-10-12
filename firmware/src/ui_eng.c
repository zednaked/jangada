/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada 0.9.7 (issue #5, after gospodindilly-ops' idea): the engine's own picture in the middle of its EDIT
 * pages, the four knobs as small circles in a row on top, where the columns were (ui_draw.c draw_column, big_one). The algorithm gives the picture: what
 * the engine is made of, read from its state as it plays where it has one (SCAN's masses, GENDY's points, WALSH's
 * wave, GRAIN's grains, FM6's operators, SAMPLE's play position), from its parameters elsewhere. The sound makes
 * it move: fx.c measures the selected part while a picture shows (an), and the picture swells with the level,
 * gets busier with the brightness and kicks on an attack, then settles as the sound dies away.
 *   ANALOG   the oscillators as orbits (SUPR's copies drifting apart with SDTN), the filter an aperture
 *   DIGITAL  FM6   nested rings, an operator each, a modulator's ring rippling with its level
 *   PHASE    a circle whose points bunch up as DCW bends the phase
 *   LOFI     a ring on the pixel grid, coarser with CRSH
 *   SAMPLE   the play position of each voice round the ring
 *   VOICE    a mouth opening with the vowel and the sound
 *   TRIO     three bodies on orbits at the intervals
 *   WHEEL    tonewheels turning, sized by the drawbars, ROTR turning them all
 *   GRAIN    the grains: where they read in the sample, in their windows
 *   NOISE    a cloud of specks, coarser as COLR goes brown
 *   PHYS     a resonator struck or bowed: a bell's ring, a string, a drum head, a string with three beside it
 *   ROBO     VOSIM's pulse trains, GENDY's walking points, WALSH's wave, SCAN's masses on their springs
 * Drawn in the main loop every other frame, from dots: a ring of dots costs its length, an anti-aliased ring
 * its whole square. Coordinates in 1/16 px, as gfx.c's circles */
#define EV_CX (120 * 16)
#define EV_CY (62 * 16)
#define EV_R (52 * 16)                                  /* the picture's reach */
#define EV_HW (104 * 16)                                /* a string's half length: the strip's width (issue #6) */
static struct {
    int32_t lvl, brt, hit, prev;                        /* 0..1000: level (up at once, down slowly), brightness, a strike */
    uint32_t t, rng;
    int16_t gx[GD_MAX], gy[GD_MAX];                     /* GENDY: the last frame's points (its trail) */
    uint8_t gn;
} ev;

static __attribute__((noinline)) int32_t ev_sin(uint32_t a) { return sine_i(a); }
static __attribute__((noinline)) int32_t ev_cos(uint32_t a) { return sine_i(a + 0x40000000u); }
static uint32_t ev_rnd(void)
{
    ev.rng = ev.rng * 1664525u + 1013904223u;
    return ev.rng >> 8;
}
/* |x| mean (Q15 / 4) -> 0..1000 over 48 dB */
static int32_t ev_db(uint32_t a)
{
    int32_t lg = 0;
    if (a < 32u)
        return 0;
    while (a >= 64u) {
        a >>= 1;
        lg += 8;
    }
    lg += (int32_t)(a - 32u) >> 2;
    return clamp(lg * 1000 / 64, 0, 1000);
}

/* once a drawn frame: the measures fx.c left, smoothed */
static void ev_take(void)
{
    uint32_t n = an.n, s = an.sum, h = an.hi;
    int32_t l, b;
    an.n = 0;
    an.sum = an.hi = 0;
    an.part = song.sel;
    an.want = 1;
    l = n ? ev_db(s / n) : 0;
    b = s > 1000u ? clamp((int32_t)(h / (s / 1000u)), 0, 1000) : 0;
    if (l > ev.prev + 90)                               /* an attack: the picture is struck */
        ev.hit = 1000;
    ev.prev = l;
    ev.lvl = l > ev.lvl ? l : ev.lvl - (ev.lvl - l + 7) / 8;
    ev.brt += (b - ev.brt) / 4;
    ev.hit = ev.hit * 13 / 16;
    ev.t++;
}

static __attribute__((noinline)) void ev_dot(int32_t x, int32_t y, int32_t r, uint16_t c) { cv_disc(x, y, r, c); }
/* a polar point: angle a (2^32 a turn), radius r round the centre */
static __attribute__((noinline)) int32_t ev_px(uint32_t a, int32_t r) { return EV_CX + ((r * ev_sin(a)) >> 15); }
static __attribute__((noinline)) int32_t ev_py(uint32_t a, int32_t r) { return EV_CY - ((r * ev_cos(a)) >> 15); }
/* a line of dots about a pixel apart */
static void ev_seg(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t w, uint16_t c)
{
    int32_t dx = x1 - x0, dy = y1 - y0, n = ((dx < 0 ? -dx : dx) > (dy < 0 ? -dy : dy) ? (dx < 0 ? -dx : dx) : (dy < 0 ? -dy : dy)) / 16 + 1, k;
    for (k = 0; k < n; k++)
        ev_dot(x0 + dx * k / n, y0 + dy * k / n, w, c);
}
/* a closed curve through n points */
static void ev_curve(const int32_t *x, const int32_t *y, uint32_t n, int32_t w, uint16_t c)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ev_seg(x[i], y[i], x[(i + 1u) % n], y[(i + 1u) % n], w, c);
}
/* a closed curve r(a) sampled at n points (r: radii in 1/16 px) */
static void ev_polar(const int32_t *r, uint32_t n, uint32_t a0, int32_t w, uint16_t c)
{
    int32_t x[96], y[96];
    uint32_t i;
    if (n > 96u)
        n = 96u;
    for (i = 0; i < n; i++) {
        uint32_t a = a0 + i * (0xFFFFFFFFu / n);
        x[i] = ev_px(a, r[i]);
        y[i] = ev_py(a, r[i]);
    }
    ev_curve(x, y, n, w, c);
}
static void ev_ring(int32_t r, uint32_t n, int32_t w, uint16_t c)   /* a ring of n dots */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        ev_dot(ev_px(i * (0xFFFFFFFFu / n), r), ev_py(i * (0xFFFFFFFFu / n), r), w, c);
}

/* the loudest voice sounding, -1 = none */
static int32_t ev_voice(const track_t *t)
{
    int32_t k, best = -1, bl = 0;
    for (k = 0; k < NVOICE; k++)
        if (t->v[k].active && t->v[k].env_out > bl) {
            bl = t->v[k].env_out;
            best = k;
        }
    return best;
}
/* the engine's state in the part's arena, 0 = another engine's (or none yet) */
static const void *ev_arena(uint32_t eng)
{
    return eng_arena_own[song.sel % NTRK] == eng + 1u ? (const void *)&eng_arena[song.sel % NTRK] : 0;
}
#define EP(k) ((int32_t)t->p[P_E0 + (k)])               /* the engine's parameters, 0..127 mostly */
#define EV_A (ev.lvl > ev.hit ? ev.lvl : ev.hit)        /* how much the picture moves: the sound, or a strike */

/* ANALOG: the oscillator's wave wrapped round a ring (seven cycles), as round as a sine where CUT is low, its
 * copies (SUPR) beside it drifting with SDTN; RES a bright rim, SUB a wide faint ring */
static int32_t ev_wave(uint32_t w, uint32_t ph, int32_t pw)   /* Q15 */
{
    int32_t x = (int32_t)(ph >> 17) - 16384;            /* -16384 .. 16383: the cycle */
    switch (w) {
    case 0: return x * 2;                               /* SAW */
    case 1: return x < 0 ? -32767 : 32767;              /* SQR */
    case 2: return (x < 0 ? -x : x) * 4 - 32767;        /* TRI */
    case 4: return (int32_t)(ph >> 17) < pw ? 32767 : -32767;   /* PWM */
    default: return ev_sin(ph);                         /* SIN */
    }
}
static void ev_analog(const track_t *t, uint16_t c)
{
    uint32_t k, i, nc = 1u + (uint32_t)clamp(EP(8), 0, 5), w = (uint32_t)clamp(EP(0), 0, 4);
    int32_t r[96], cut = 200 + EP(4) * 800 / 127, pw = 16384 + ev_sin(ev.t * 40000000u) / 3;
    for (k = 0; k < nc; k++) {
        uint32_t drift = ev.t * (uint32_t)(EP(9) + EP(1) + 4) * 900000u * k;
        int32_t base = EV_R * (62 - (int32_t)k * 3) / 100, amp = EV_R * (6 + EV_A * 14 / 1000) / 100;
        for (i = 0; i < 96u; i++) {
            uint32_t ph = i * (0xFFFFFFFFu / 96u) * 7u + drift + ev.t * 30000000u;
            int32_t x = (ev_wave(w, ph, pw) * cut + ev_sin(ph) * (1000 - cut)) / 1000;
            r[i] = base + ((amp * x) >> 15);
        }
        ev_polar(r, 96, 0, k ? 12 : 18, k ? mix565(c, C_BG, 35 + (int32_t)k * 8) : c);
    }
    if (EP(5) > 20)                                     /* RES: the rim rings */
        ev_ring(EV_R * 82 / 100 + EV_A * 40 / 1000, 64, 10 + EP(5) / 10, mix565(C_WHITE, C_BG, 100 - EP(5) * 60 / 127));
    if (EP(10))
        ev_ring(EV_R * 97 / 100, 48, 12, mix565(c, C_BG, 70 - EP(10) * 30 / 127));
}

/* operators as nested rings (n of them, the carriers bright), a modulator's ring rippling with its level; the
 * spectrum's brightness adds spokes */
static void ev_ops(uint32_t n, const int32_t *lvl, uint32_t car, uint16_t c)
{
    uint32_t k, i, ns = 2u + (uint32_t)ev.brt * 14u / 1000u;
    int32_t r[48];
    for (k = 0; k < n; k++) {
        int32_t base = EV_R * (int32_t)(k + 2u) / (int32_t)(n + 1u), amp = lvl[k] * EV_R / 7000, lobes = (int32_t)k + 2;
        uint32_t ph = ev.t * (k + 1u) * 50000000u;
        for (i = 0; i < 48u; i++)
            r[i] = base * (88 + EV_A / 100) / 100 + ((amp * ev_sin((uint32_t)i * 89478485u * (uint32_t)lobes + ph)) >> 15);
        ev_polar(r, 48, 0, car >> k & 1u ? 20 : 12,
                 car >> k & 1u ? c : mix565(c, C_BG, 60 - lvl[k] * 40 / 1000));
    }
    for (i = 0; i < ns; i++) {                          /* the brightness: spokes */
        uint32_t a = i * (0xFFFFFFFFu / ns) + ev.t * 20000000u;
        ev_seg(ev_px(a, EV_R / 6), ev_py(a, EV_R / 6), ev_px(a, EV_R * (40 + ev.brt / 25) / 100),
               ev_py(a, EV_R * (40 + ev.brt / 25) / 100), 10, mix565(C_WHITE, C_BG, 80 - EV_A / 25));
    }
}
static void ev_fm6(const track_t *t, uint16_t c)
{
    const fm6_note_t *N = (const fm6_note_t *)ev_arena(ENGI_FM6);
    int32_t lvl[6] = {0}, v = ev_voice(t), k;
    if (N && v >= 0 && v < FM6_POLY)
        for (k = 0; k < 6; k++)                         /* the operator's gain now (Q24), the sixth outermost */
            lvl[5 - k] = clamp(N[v].op[k].gain_out >> 14, 0, 1000);
    {
        uint32_t car = fm6_carriers((uint32_t)EP(0)), cr = 0;
        for (k = 0; k < 6; k++)
            cr |= (car >> k & 1u) << (5 - k);
        ev_ops(6, lvl, cr, c);
    }
}
static void ev_digital(const track_t *t, uint16_t c)
{
    int32_t lvl[4], k, e = EV_A;
    for (k = 0; k < 4; k++)                             /* IDX: how hard the modulators turn, the envelope with it */
        lvl[k] = k == 0 ? e : EP(4) * e / 127 * (4 - k) / 3;
    ev_ops(4, lvl, 1u, c);
}

/* PHASE: the circle's points bunch up as DCW bends the phase, the bend breathing with the sound */
static void ev_phase(const track_t *t, uint16_t c)
{
    int32_t i, d = EP(2) * (300 + EV_A * 7 / 10) / 1000;   /* 0..127 -> how far the points move */
    uint32_t spin = ev.t * 30000000u;
    for (i = 0; i < 56; i++) {
        uint32_t a = (uint32_t)i * 76695844u;
        uint32_t b = a + (uint32_t)((d * ev_sin(a + spin)) << 9);
        int32_t r = EV_R * (70 + EV_A / 40) / 100;
        ev_dot(ev_px(b, r), ev_py(b, r), 16 + (i % 7 == 0) * 10, i % 7 == 0 ? C_WHITE : c);
    }
    if (EP(6))                                          /* SUB: an inner circle */
        ev_ring(EV_R * 35 / 100, 28, 12, mix565(c, C_BG, 50));
}

/* LOFI: a ring on the pixel grid, the grid coarser with CRSH, DUTY squashing it */
static void ev_lofi(const track_t *t, uint16_t c)
{
    int32_t g = (3 + EP(3) * 9 / 127) * 16, i, r = EV_R * (55 + EV_A * 35 / 1000) / 100;
    int32_t sq = 60 + EP(2) * 40 / 127;
    uint32_t spin = ev.t * (uint32_t)(10000000 + EP(5) * 400000);
    for (i = 0; i < 64; i++) {
        uint32_t a = (uint32_t)i * 67108864u + spin;
        int32_t x = EV_CX + ((r * ev_sin(a)) >> 15), y = EV_CY - ((r * sq / 100 * ev_cos(a)) >> 15);
        x = x / g * g + g / 2;
        y = y / g * g + g / 2;
        cv_rect((x - g / 2) >> 4, ((y - g / 2) >> 4), g / 16 - 1, g / 16 - 1, i & 1 ? c : mix565(c, C_WHITE, EV_A / 20));
    }
}

/* SAMPLE: each voice's play position round the ring, its head white */
static void ev_sample(const track_t *t, uint16_t c)
{
    int32_t k, ring = 0;
    ev_ring(EV_R * 92 / 100, 72, 8, mix565(c, C_BG, 75));
    for (k = 0; k < NVOICE && ring < 4; k++) {
        const voice_t *v = &t->v[k];
        const smp_zone_t *z;
        uint32_t i, frac, nd;
        int32_t r;
        if (!v->active || v->s[6])
            continue;
        z = smp_zone((uint32_t)v->s[4]);
        if (!z->n)
            continue;
        frac = (v->ph[0] < z->n ? v->ph[0] : z->n) / (z->n / 1000u + 1u);
        if (frac > 1000u)
            frac = 1000u;
        r = EV_R * (92 - 16 * ring) / 100;
        nd = 2u + frac * 60u / 1000u;
        for (i = 0; i < nd; i++) {
            uint32_t a = 4294967u * frac / (nd - 1u) * i;
            ev_dot(ev_px(a, r), ev_py(a, r), 18, i + 1u == nd ? C_WHITE : c);
        }
        ev_dot(ev_px((uint32_t)(4294967u * frac), r), ev_py((uint32_t)(4294967u * frac), r), 30 + EV_A / 40, C_WHITE);
        ring++;
    }
}

/* VOICE: a mouth, its shape from the vowel (open A .. round U), opened by the sound; TALK makes it chatter */
static void ev_voice_(const track_t *t, uint16_t c)
{
    int32_t x[48], y[48], i, vw = EP(0) * 1000 / 127, op = 15 + EV_A * 85 / 1000;
    int32_t w = EV_R * (95 - vw * 45 / 1000) / 100, h = EV_R * op / 100 * (40 + vw * 30 / 1000) / 100;
    h += (EP(2) * EV_A / 1000) * ev_sin(ev.t * 300000000u) / 32768 * 4;
    for (i = 0; i < 48; i++) {
        uint32_t a = (uint32_t)i * 89478485u;
        x[i] = EV_CX + ((w * ev_sin(a)) >> 15);
        y[i] = EV_CY - ((h * ev_cos(a)) >> 15);
    }
    ev_curve(x, y, 48, 22, c);
    for (i = 1; i <= 3; i++)                            /* the formants: ripples beside it */
        ev_ring(w + i * 70 + EV_A * i / 30, 40, 9, mix565(c, C_BG, 40 + i * 15));
}

/* TRIO: three bodies, their speeds the intervals (INT2, INT3), with trails */
static void ev_trio(const track_t *t, uint16_t c)
{
    static const uint16_t RAT[25] = {1000, 1059, 1122, 1189, 1260, 1335, 1414, 1498, 1587, 1682, 1782, 1888,
                                     2000, 2119, 2245, 2378, 2520, 2670, 2828, 2997, 3175, 3364, 3564, 3775, 4000};
    int32_t b, k;
    for (b = 0; b < 3; b++) {
        int32_t iv = b == 0 ? 0 : clamp(EP(b) * 24 / 127, 0, 24), r = EV_R * (40 + b * 20) / 100 * (85 + EV_A / 66) / 100;
        for (k = 0; k < 10; k++) {
            uint32_t a = (ev.t - (uint32_t)k) * (40000u * RAT[iv]) + (uint32_t)b * 0x55555555u;
            ev_dot(ev_px(a, r), ev_py(a, r), k ? 26 - k * 2 : 40, k ? mix565(c, C_BG, k * 9) : C_WHITE);
        }
        ev_ring(r, 40, 8, mix565(c, C_BG, 80));
    }
}

/* WHEEL: the nine tonewheels round the ring, 16' to 1', each as big as its drawbar (REG and SUB / BODY / TOP),
 * turning at its footage's pitch; ROTR turns them all round */
static void ev_wheel(const track_t *t, uint16_t c)
{
    static const uint8_t HARM2[9] = {1, 3, 2, 4, 6, 8, 10, 12, 16};   /* the footages' pitches, x2 */
    const char *reg = DRW_REG[clamp(EP(0), 0, 15)];
    int32_t k;
    uint32_t rot = ev.t * (uint32_t)(EP(7) * 1200000);
    for (k = 0; k < 9; k++) {
        int32_t bar = clamp(reg[k] - '0' + (k < 2 ? EP(1) : k < 4 ? EP(2) : EP(3)), 0, 8), r = 40 + bar * 16, i;
        uint32_t a = (uint32_t)k * (0xFFFFFFFFu / 9u) + rot, s = ev.t * HARM2[k] * 18000000u;
        int32_t cx = ev_px(a, EV_R * 66 / 100), cy = ev_py(a, EV_R * 66 / 100);
        uint16_t col = bar ? mix565(c, C_WHITE, EV_A * bar / 300) : mix565(c, C_BG, 80);
        for (i = 0; i < 10; i++) {
            uint32_t b = s + (uint32_t)i * 429496729u;
            ev_dot(cx + ((r * ev_sin(b)) >> 15), cy - ((r * ev_cos(b)) >> 15), i ? 10 + bar : 16 + bar * 2, i ? col : C_WHITE);
        }
    }
}

/* GRAIN: each grain where it reads in its sample (round the ring), its window's swell its size and light; POS
 * marked */
static void ev_grain(const track_t *t, uint16_t c)
{
    const gr_part_t *P = (const gr_part_t *)ev_arena(ENGI_GRAIN);
    uint32_t k, a = (uint32_t)(EP(1) * 33818640u);
    ev_seg(ev_px(a, EV_R * 30 / 100), ev_py(a, EV_R * 30 / 100), ev_px(a, EV_R), ev_py(a, EV_R), 10, mix565(c, C_BG, 55));
    if (!P)
        return;
    for (k = 0; k < GR_NG; k++) {
        const gr_grain_t *g = &P->g[k];
        int32_t w, r;
        uint32_t ga;
        if (!g->owner || !g->left || !g->z || !g->z->n)
            continue;
        w = ev_sin(g->wph >> 1);                       /* the window: 0 .. 1 .. 0 (Q15) */
        ga = (g->pos < g->z->n ? g->pos : g->z->n) * (0xFFFFFFFFu / g->z->n);
        r = EV_R * (40 + (int32_t)(k * 53u % 55u)) / 100;
        ev_dot(ev_px(ga, r), ev_py(ga, r), 12 + ((w * (16 + EP(2) / 3)) >> 15) + EV_A / 60,
               mix565(C_BG, g->rev ? C_WHITE : c, 30 + (w >> 9) * 70 / 64));
    }
}

/* NOISE: a cloud of specks, as many as the sound is loud and DENS asks, larger and softer as COLR goes brown */
static void ev_noise(const track_t *t, uint16_t c)
{
    int32_t i, n = 8 + EV_A * (20 + EP(5)) / 2000, sz = 10 + EP(1) / 6;
    ev.rng ^= ev.t * 2654435761u;
    for (i = 0; i < n; i++) {
        uint32_t a = ev_rnd() << 8;
        int32_t r = (int32_t)(ev_rnd() % (uint32_t)EV_R);
        ev_dot(ev_px(a, r), ev_py(a, r), sz + (int32_t)(ev_rnd() % 12u), ev_rnd() & 3u ? c : C_WHITE);
    }
}

/* PHYS: the resonator. A strike rings it out, BOW keeps it moving; DAMP is in the sound's own decay. MODAL: a
 * bell's ring (modes 2, 3, 5); STRNG: a string, plucked at POS; MEMB: a drum head; SYMP: a string and three */
static void ev_string(int32_t y0, int32_t half, int32_t amp, uint32_t ph, int32_t pos, int32_t w, uint16_t c)
{
    int32_t i, py = y0, px = EV_CX - half;
    for (i = 1; i <= 48; i++) {
        int32_t u = i * 1000 / 48, d;                   /* along the string, 0..1000 */
        d = u < pos ? u * 1000 / (pos | 1) : (1000 - u) * 1000 / ((1000 - pos) | 1);   /* the pluck's triangle */
        d = (d * (amp * ev_cos(ph) >> 15) / 1000) + ((amp / 3 * ev_sin((uint32_t)u * 12884902u) >> 15) * ev_sin(ph * 3u) >> 15);
        ev_seg(px, py, EV_CX - half + 2 * half * i / 48, y0 + d, w, c);
        px = EV_CX - half + 2 * half * i / 48;
        py = y0 + d;
    }
}
static void ev_phys(const track_t *t, uint16_t c)
{
    int32_t m = EP(0) % 4, a = EV_A + EP(6) * 3, i;
    uint32_t ph = ev.t * (uint32_t)(60000000 + EP(2) * 1500000);
    if (a > 1000)
        a = 1000;
    if (EP(6))                                          /* BOW: never quite still */
        ph += ev_rnd() << 2;
    if (m == 1 || m == 3) {
        int32_t amp = a * EV_R / 1800, pos = 80 + EP(4) * 840 / 127;
        ev_string(EV_CY - (m == 3 ? 160 : 0), EV_HW, amp, ph, pos, 18, c);
        if (m == 3)                                     /* SYMP: three strings ringing along, fainter */
            for (i = 0; i < 3; i++)
                ev_string(EV_CY + 140 + i * 130, EV_HW * (88 - i * 8) / 100, amp * EP(5) / 200 + amp / 6, ph * (uint32_t)(2 + i),
                          500, 10, mix565(c, C_BG, 40 + i * 10));
        return;
    }
    {
        int32_t r[48], k, base = EV_R * 70 / 100, amp = a * EV_R / 3000;
        for (k = 0; k < (m == 2 ? 3 : 1); k++) {        /* MEMB: the head and its rings */
            for (i = 0; i < 48; i++) {
                uint32_t th = (uint32_t)i * 89478485u;
                int32_t d = m == 2 ? ((amp * ev_cos(ph * (uint32_t)(k + 1))) >> 15)
                                   : (((amp * ev_sin(th * 2u + ph)) >> 15) + ((amp / 2 * ev_sin(th * 3u - ph * 2u)) >> 15) +
                                      ((amp / 3 * ev_sin(th * 5u + ph * 3u)) >> 15));
                r[i] = base * (3 - k) / 3 + d;
            }
            ev_polar(r, 48, 0, k ? 12 : 20, k ? mix565(c, C_BG, 30 + k * 20) : c);
        }
        if (m == 0 && ev.hit > 200)                     /* the strike's point */
            ev_dot(ev_px((uint32_t)EP(4) * 33818640u, base), ev_py((uint32_t)EP(4) * 33818640u, base), 30 * ev.hit / 1000 + 10, C_WHITE);
    }
}

/* ROBO */
static void ev_robo(const track_t *t, uint16_t c)
{
    const robo_part_t *P = (const robo_part_t *)ev_arena(ENGI_ROBO);
    int32_t v = ev_voice(t), mode = EP(0) % RB_COUNT, i;
    const robo_voice_t *R = P && v >= 0 && P->v[v].lay == mode + 1 ? &P->v[v] : 0;
    if (mode == RB_VOSIM) {                             /* PULS pulses a train, each smaller (DCAY), three trains turning */
        int32_t np = 1 + EP(2) * 7 / 127, k;
        for (k = 0; k < 3; k++)
            for (i = 0; i < np; i++) {
                uint32_t a = (uint32_t)k * 0x55555555u + ev.t * 25000000u;
                int32_t r = EV_R * (25 + i * 70 / np) / 100, s = 50 * (EV_A + 200) / 1200;
                int32_t d = 1000;
                int32_t j;
                for (j = 0; j < i; j++)
                    d = d * (40 + EP(3) * 55 / 127) / 100;
                ev_dot(ev_px(a, r), ev_py(a, r), 8 + s * d / 1000, i ? mix565(c, C_BG, 60 - d * 60 / 1000) : C_WHITE);
            }
        return;
    }
    if (mode == RB_GENDY) {                             /* the points as they walk, with last frame's as a trail */
        int32_t x[GD_MAX], y[GD_MAX];
        uint32_t n = 0, acc = 0;
        if (R && R->u.g.n >= 2 && R->u.g.sum) {
            n = R->u.g.n > GD_MAX ? GD_MAX : R->u.g.n;
            for (i = 0; i < (int32_t)n; i++) {
                uint32_t a = acc * (0xFFFFFFFFu / R->u.g.sum);
                int32_t r = EV_R * 55 / 100 + ((R->u.g.amp[i] * (EV_R * 40 / 100)) >> 15) * (300 + EV_A) / 1300;
                x[i] = ev_px(a, r);
                y[i] = ev_py(a, r);
                acc += R->u.g.dur[i];
            }
        }
        if (ev.gn >= 2u) {
            int32_t ox[GD_MAX], oy[GD_MAX];
            for (i = 0; i < (int32_t)ev.gn; i++) {
                ox[i] = ev.gx[i] * 4;
                oy[i] = ev.gy[i] * 4;
            }
            ev_curve(ox, oy, ev.gn, 10, mix565(c, C_BG, 65));
        }
        if (n) {
            ev_curve(x, y, n, 18, c);
            for (i = 0; i < (int32_t)n; i++) {
                ev_dot(x[i], y[i], 30, C_WHITE);
                ev.gx[i] = (int16_t)(x[i] / 4);
                ev.gy[i] = (int16_t)(y[i] / 4);
            }
        }
        ev.gn = (uint8_t)n;
        return;
    }
    {
        int32_t r[64];
        uint32_t n = mode == RB_WALSH ? WL_N : SC_N;
        for (i = 0; i < (int32_t)n; i++) {
            int32_t d = !R ? 0 : mode == RB_WALSH ? R->u.wav[i] : R->u.s.w1[i];
            r[i] = d;
        }
        {                                               /* the shape, filling its room whatever its level */
            int32_t pk = 1;
            for (i = 0; i < (int32_t)n; i++)
                pk = r[i] > pk ? r[i] : -r[i] > pk ? -r[i] : pk;
            for (i = 0; i < (int32_t)n; i++)
                r[i] = EV_R * 62 / 100 + r[i] * (EV_R * 30 / 100) / pk * (300 + EV_A) / 1300;
        }
        if (mode == RB_WALSH) {                         /* steps: each value held over its share of the turn */
            int32_t rr[64];
            for (i = 0; i < 64; i++)
                rr[i] = r[i / 2];
            ev_polar(rr, 64, ev.t * 8000000u, 18, c);
        } else {                                        /* SCAN: the masses on their ring, the springs between */
            ev_polar(r, n, 0, 16, c);
            for (i = 0; i < (int32_t)n; i++) {
                uint32_t a = (uint32_t)i * (0xFFFFFFFFu / n);
                ev_dot(ev_px(a, r[i]), ev_py(a, r[i]), 34, C_WHITE);
            }
        }
    }
}

/* the picture of the selected track's engine; 0 = none (the drum track) */
static int eng_view(void)
{
    const track_t *t = TSEL;
    uint16_t c = ACC;
    if (is_drum(t))
        return 0;
    ev_take();
    switch (t->engine == t->eng_req ? t->engine : t->eng_req) {
    case 0: ev_analog(t, c); break;
    case 1: ev_digital(t, c); break;
    case 2: ev_phase(t, c); break;
    case 3: ev_lofi(t, c); break;
    case 4: ev_sample(t, c); break;
    case 5: ev_voice_(t, c); break;
    case 6: ev_trio(t, c); break;
    case 7: ev_wheel(t, c); break;
    case ENGI_GRAIN: ev_grain(t, c); break;
    case ENGI_FM6: ev_fm6(t, c); break;
    case ENGI_NOISE: ev_noise(t, c); break;
    case ENGI_PHYS: ev_phys(t, c); break;
    case ENGI_ROBO: ev_robo(t, c); break;
    default: ev_ring(EV_R * (60 + EV_A / 30) / 100, 48, 18, c); break;
    }
    return 1;
}
#undef EP
#undef EV_A
