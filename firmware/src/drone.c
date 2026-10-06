/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada DRONES: a drone that evolves on its own, and a tension that opens slowly. Per part, at the
 * control rate (once per block of CTL samples), from track_render (voice.c). Included by mod.c.
 *
 * EVOL (P_EVOL): while the part sounds, DR_NW slow random walks move its sound: each walk takes a new
 *   target every 4 to 8 seconds (5/8 of the last one and a random step: it wanders and comes back,
 *   reflected at +-1) and follows it through two one-pole lowpasses, so it never has a corner. Cycles
 *   of tens of seconds to minutes, never the same twice. Walk 0 moves the filter (vmod cutoff) and is the
 *   matrix's DRIFT source, walk 1 the shape (vmod shape), walk 2 the engine's own "natural" parameter
 *   (DR_ENG: the superwave's MIX, the FM index, the grain size, ..), and every voice gets its own
 *   mix of the walks for a slow detune (a few cents) and a breath in its level: a held chord
 *   beats and breathes like a choir of old machines.
 * TENS (P_TENS): a macro, 0 = the preset as it is. It opens the filter, pushes the engine's
 *   resonance / drive / brightness (DR_ENG) and the track's DIST (when it is on), spreads the voices
 *   apart in pitch (up to +-12 cents, each its own way) and makes the walks deeper and faster (x3).
 * RAMP (P_TRAMP): TENS is a target the sound travels to in that many bars (at the tempo); OFF: at
 *   once (smoothed). A drone that starts from silence starts at tension 0 and builds; turning TENS down
 *   goes back as slowly.
 *
 * The walks only move while the part sounds; when it falls silent they stop, and a drone that starts
 * again starts from the preset's own sound. Their random numbers are their own (not rng()): turning
 * EVOL on changes nothing else. EVOL 0, TENS 0 and no DRIFT in the matrix: nothing here runs and the
 * sound is the same to the sample. */
#define DR_NW 4                                         /* walks per part */
#define DR_PER (6 * FS / CTL)                           /* ticks between two targets (x 0.6 .. 1.4) */
#define DR_K 20                                         /* lowpass, Q16 per tick at speed 1: tau ~2.4 s */
#define DR_SUB 4u                                       /* the walks move every DR_SUB ticks (2.9 ms) */
#define DR_T1 (32767u << 16)                            /* tension 1.0 (Q31) */
static const uint8_t DR_RAMP_BARS[7] = {0, 1, 2, 4, 8, 16, 32};   /* P_TRAMP (params.c N_TRAMP) */

typedef struct {
    uint32_t seed;               /* the walks' own random numbers */
    int32_t cnt[DR_NW];          /* 1/256 ticks to the next target */
    int32_t tgt[DR_NW], s1[DR_NW], s2[DR_NW];          /* Q27 (Q15 << 12) */
    int32_t w[DR_NW];            /* the walks, Q15 (-1 .. 1) */
    int32_t tens;                /* the tension now, Q31 (Q15 << 16: a 32-bar ramp keeps its time) */
    int32_t depth;               /* how far the walks reach, Q15 */
    int32_t cut, shp;            /* this block's filter and shape offsets (vmod units) */
    uint8_t ne, ej[4];           /* the engine parameters the drone moves (one walk, three tensions), */
    int16_t eoff[4];             /* by how much (worked out every DR_SUB ticks), */
    int16_t keep[4];             /* their own values while this block renders */
    int16_t vg[NVOICE], vf[NVOICE];   /* each voice's breath (Q15 gain) and tune (1/4096) */
    int16_t dist;                /* what the tension adds to the track's DIST (fx.c mix_part) */
    uint8_t nk;
    uint8_t sub, eng;            /* ticks to the next walk step; the engine the targets are for */
    uint8_t on;                  /* the part sounded last block */
} drone_t;
static drone_t drn[NTRK] = {{.seed = 0x6A09E667u}, {.seed = 0xBB67AE85u}, {.seed = 0x3C6EF372u}, {.seed = 0xA54FF53Au}};

/* the engines' natural targets: EV the parameter walk 2 moves (EVP % of its range each way at full
 * depth), T[k] = {parameter, % of its range at full tension}; -1 none. Only continuous parameters (an
 * ENUM, a table select or the WHEEL's bars would step). By engine number (engines.c) */
typedef struct {
    int8_t ev, evp;
    int8_t t[3][2];
    int8_t dist;                 /* % of the track's DIST range the tension adds, when DIST is on */
    int8_t fs;                   /* % of the filter / shape offsets (engines that read them as an FM index) */
} dr_eng_t;
static const dr_eng_t DR_ENG[] = {
    {2, 25, {{5, 35}, {6, 40}, {9, 30}}, 15, 100},  /* ANALOG: MIX; RES, DRV, SDTN (the superwave opens) */
    {4, 18, {{4, 15}, {6, 12}, {-1, 0}}, 10, 35},   /* DIGITAL: IDX; IDX, FB (a little: feedback hisses) */
    {2, 20, {{2, 25}, {4, 20}, {-1, 0}}, 20, 60},   /* PHASE: DCW; DCW, DTN */
    {-1, 0, {{3, 30}, {-1, 0}, {-1, 0}}, 20, 100},  /* LOFI: (DUTY picks tables); CRSH */
    {-1, 0, {{6, 45}, {-1, 0}, {-1, 0}}, 15, 100},  /* SAMPLE: DRV */
    {5, 20, {{4, 30}, {6, 30}, {-1, 0}}, 20, 100},  /* VOICE: BRTH; BUZZ, Q */
    {3, 30, {{6, 35}, {3, 25}, {-1, 0}}, 20, 100},  /* TRIO: DTN; RES, DTN */
    {6, 20, {{6, 70}, {-1, 0}, {-1, 0}}, 40, 100},  /* WHEEL: DRV; DRV (its bars step: left alone; the
                                                     * reeds' walk, their spread and the DIST do the rest) */
    {2, 20, {{3, 25}, {6, 35}, {-1, 0}}, 25, 100},  /* GRAIN: SIZE; DENS, RAND */
    {6, 20, {{2, 25}, {6, 20}, {-1, 0}}, 15, 60},   /* FM6: DTUN; MLVL, DTUN */
    {-1, 0, {{-1, 0}, {-1, 0}, {-1, 0}}, 0, 0},     /* SLICE */
};
/* each voice's place in the tension's detune spread (-1 .. 1, Q7): fixed per voice, so a chord struck
 * again (an arp RPT) keeps its tuning */
static const int8_t DR_SPREAD[NVOICE] = {127, -55, 90, -109, 37, -127, 72, -18};

static uint32_t dr_rand(drone_t *d)
{
    d->seed = d->seed * 1664525u + 1013904223u;
    return d->seed;
}

/* the walk's per-tick change of the tension towards TENS: RAMP bars at the tempo, Q31 */
static int32_t dr_ramp_step(const track_t *t)
{
    uint32_t bars = DR_RAMP_BARS[(uint32_t)t->p[P_TRAMP] % 7u], bpm = (uint32_t)clamp(song.g[G_BPM], 20, 300);
    uint32_t ticks = (uint32_t)FS * 240u / CTL * bars / bpm;   /* ticks in the ramp (32 bars at 20 BPM: 2.6 M) */
    return (int32_t)(DR_T1 / (ticks ? ticks : 1u));
}

/* every DR_SUB ticks: the walks, how far they reach, the offsets for the voices and the engine */
static void drone_walk(track_t *t, drone_t *d)
{
    const engine_t *e = ENGINES[t->engine];
    const dr_eng_t *de = &DR_ENG[t->engine % (sizeof DR_ENG / sizeof DR_ENG[0])];
    int32_t T = d->tens >> 16, evol = t->p[P_EVOL] * 258, spd = 256 + (T >> 6), k, c;   /* faster: x1 .. x3 */
    k = DR_K * DR_SUB * spd >> 8;
    for (c = 0; c < DR_NW; c++) {
        if ((d->cnt[c] -= spd * DR_SUB) <= 0) {         /* a new target: a step of the walk */
            uint32_t r = dr_rand(d);
            /* 5/8 of the last one and a random step of up to +-0.75: it wanders, and comes back */
            int32_t x = (d->tgt[c] >> 12) * 5 / 8 + (((int32_t)(r >> 16) - 32768) * 3) / 4;
            if (x > 32767)
                x = 65534 - x;                          /* reflected at the ends */
            if (x < -32767)
                x = -65534 - x;
            d->tgt[c] = x << 12;
            d->cnt[c] += (int32_t)(DR_PER * (154u + ((r >> 4) & 255u) * 205u / 255u));   /* 0.6 .. 1.4 */
        }
        d->s1[c] += ((d->tgt[c] - d->s1[c]) >> 8) * k >> 8;
        d->s2[c] += ((d->s1[c] - d->s2[c]) >> 8) * k >> 8;
        d->w[c] = d->s2[c] >> 12;
    }
    /* how far they reach: EVOL, deeper with the tension, and some of it with the tension alone */
    d->depth = clamp(evol + mulq15(evol, T) + (T * 10 >> 5), 0, 32767);
    d->cut = (((mulq15(d->w[0], d->depth) * 22) >> 7) + ((T * 34) >> 7)) * de->fs / 100;   /* +-22 steps; +34 */
    d->shp = (((mulq15(d->w[1], d->depth) * 18) >> 7) + ((T * 12) >> 7)) * de->fs / 100;
    /* every voice its own mix of the walks: a slow detune and a breath; the tension pulls them apart */
    for (c = 0; c < NVOICE; c++) {
        int32_t wp = d->w[(c + 1) & 3], wa = d->w[(c + 2) & 3];
        if (c & 4)
            wp = -wp;                                   /* eight voices, eight curves from four walks */
        if (c & 1)
            wa = -wa;
        d->vf[c] = (int16_t)(((mulq15(wp, d->depth) * 14) >> 15) +   /* +-6 cents walking (2.367 per cent) */
                             ((mulq15(T, DR_SPREAD[c] * 258) * 28) >> 15));   /* +-12 cents apart at full TENS */
        d->vg[c] = (int16_t)(32767 - mulq15(d->depth, (wa + 32768) >> 1) / 5);   /* breath: down to -2 dB */
    }
    d->dist = (int16_t)((T * de->dist / 100 * 127) >> 15);
    /* the engine's own parameters: the walk on one, the tension on up to three */
    d->ne = 0;
    for (c = -1; c < 3; c++) {
        int32_t j = c < 0 ? de->ev : de->t[c][0], off;
        const param_desc_t *pd;
        if (j < 0 || (uint32_t)j >= NEDIT)
            continue;
        pd = &e->edit[j];
        if (!pd->label || pd->max <= pd->min || pd->fmt == F_ENUM)
            continue;
        off = c < 0 ? mulq15(d->w[2], d->depth) * de->evp : T * de->t[c][1];   /* Q15 x % */
        d->ej[d->ne] = (uint8_t)j;
        d->eoff[d->ne++] = (int16_t)((off / 100) * (pd->max - pd->min) >> 15);
    }
}

/* once per block, before the part's voices. Returns 1 when the voices take the drone's offsets
 * (drone_voice), and then the engine parameters it moved are in t->p until drone_restore. */
static __attribute__((noinline)) uint32_t drone_tick(track_t *t, uint32_t drift)
{
    drone_t *d = &drn[(uint32_t)(t - trk) % NTRK];
    const engine_t *e = ENGINES[t->engine];
    int32_t goal = (int32_t)t->p[P_TENS] * 258 << 16, c;
    uint32_t i, act = 0;
    if (!(t->p[P_EVOL] | t->p[P_TENS] | d->tens | (int32_t)drift)) {
        d->on = 0;
        return 0;                                       /* off: nothing runs */
    }
    for (i = 0; i < NVOICE; i++)
        act |= t->v[i].active;
    if (!act) {                                         /* silent: the walks stop, the tension waits */
        d->on = 0;
        d->tens = t->p[P_TRAMP] ? 0 : goal;             /* (a RAMP builds again from 0) */
        return 0;
    }
    if (!d->on) {                                       /* a drone starts: from the preset's own sound */
        d->on = 1;
        d->sub = 0;
        for (c = 0; c < DR_NW; c++) {
            d->tgt[c] = d->s1[c] = d->s2[c] = d->w[c] = 0;
            d->cnt[c] = (int32_t)((dr_rand(d) >> 8) % (DR_PER * 256u));   /* staggered first targets */
        }
        if (t->p[P_TRAMP])
            d->tens = 0;
    }
    if (d->tens != goal) {                              /* the tension towards TENS: RAMP, or smoothed */
        int32_t df = goal - d->tens, st;
        if (t->p[P_TRAMP]) {
            st = dr_ramp_step(t);
        } else {
            st = (df < 0 ? -df : df) >> 5;
            st = st > 1 << 22 ? st : 1 << 22;
        }
        d->tens = df > 0 ? (df < st ? goal : d->tens + st) : (-df < st ? goal : d->tens - st);
    }
    if (!d->sub-- || d->eng != t->engine) {           /* (another engine: its own targets now) */
        d->sub = DR_SUB - 1u;
        d->eng = t->engine;
        drone_walk(t, d);
    }
    /* this block's engine values (its block() and every voice see them; drone_restore puts them back) */
    for (i = 0; i < d->ne; i++) {
        const param_desc_t *pd = &e->edit[d->ej[i]];
        d->keep[i] = t->p[P_E0 + d->ej[i]];
        t->p[P_E0 + d->ej[i]] = (int16_t)clamp(d->keep[i] + d->eoff[i], pd->min, pd->max);
    }
    d->nk = d->ne;
    return 1;
}

/* after the part's voices: the engine parameters back to their own values */
static __attribute__((noinline)) void drone_restore(track_t *t)
{
    drone_t *d = &drn[(uint32_t)(t - trk) % NTRK];
    uint32_t i = d->nk;
    while (i--)                                         /* (in reverse: a parameter moved twice) */
        t->p[P_E0 + d->ej[i]] = d->keep[i];
    d->nk = 0;
}

/* voice i of the part, before it renders: its breath on the amplitude ramp, its own detune, the part's
 * filter and shape offsets (m as track_render worked it out) */
static __attribute__((noinline)) void drone_voice(const track_t *t, uint32_t i, vmod_t *m)
{
    const drone_t *d = &drn[(uint32_t)(t - trk) % NTRK];
    int32_t f = d->vf[i % NVOICE];
    if (d->depth) {                                     /* (the breath moves slowly: the ramp stays whole) */
        m->amp0 = mulq15(m->amp0, d->vg[i % NVOICE]);
        m->amp1 = mulq15(m->amp1, d->vg[i % NVOICE]);
    }
    if (f) {
        m->fine += f;
        m->inc += (uint32_t)((int32_t)(PITCH_INC[m->pitch16] >> 12) * f);
    }
    m->cutoff += d->cut;
    m->shape += d->shp;
}

/* the track's DIST insert with the tension (fx.c mix_part): pushed only when the sound has it on (from
 * 0 the insert would come in at once); the drone silent: the DIST as it is */
static int32_t drone_dist(const track_t *t)
{
    const drone_t *d = &drn[(uint32_t)(t - trk) % NTRK];
    return t->p[P_DIST] && d->on ? (int32_t)d->dist : 0;
}
