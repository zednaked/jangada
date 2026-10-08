/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Projects: four slots in .noinit RAM, so they survive resets and UBOOT
 * entry. With FELUCCA_FLASH every save also goes to flash through storage.c,
 * and an empty RAM slot is filled from flash on load.
 *
 * Formats: 3 ("FUN3", written) = format 2 with today's P_COUNT per track (the SLICER parameters);
 * 2 ("FUN2") and 1 ("FUN1") are read and converted: they hold PROJ_NP_V2 parameters per track,
 * mapped by count as user presets are (the first PROJ_NP_V2 - 8 are P_LEVEL.. in order, the last 8
 * P_E0..P_E7; the parameters added since take their defaults, so the SLICER is OFF). Their engine
 * bytes are kept: formats 1 and 2 had engines 0..7 (ANALOG .. WHEEL), and the engines added since
 * (SLICE 8, ..) were appended, no index moved; the drum track's byte (it has no engine) becomes 0.
 *
 * Built on the host too (tests/project_test.c, -DPROJ_HOST): the part above the #ifndef
 * PROJ_HOST needs core.h, params.c (TP), the engines and trk_def_engine (ui.c). */
/* Jangada: a RAM slot is today's project_t ("JNGR"); flash holds "JNG1", self-describing: the
 * stable keys of the values it stores (keys.h), then the values in that order, so a project
 * survives parameters being added or moved. FUN3 / FUN2 / FUN1 (Felucca) are read and converted.
 * After the tracks, "JNG1" carries tagged sections (byte 11 = their count; tag, length, data), skipped
 * when unknown: section 1 = each track's FM6 patch (eng_fm6.c, NTRK x the 128-byte packed record, as
 * Felucca 1.0's FUN8 keeps it). A project without it (Jangada 0.4 and before, Felucca's) loads with
 * the patch of each track's PTCH, as it did. Section 2 (Jangada 0.7, after SLOOP 2.4) = the nudges, the step
 * conditions and the parameter locks, only when a track has any: per track a byte (bit 7: its 64 step bytes
 * follow, core.h seqx_t sx; bits 0..4: its locks), then the step bytes, then each lock as step, key, value.
 * A project without it has none, as before. */
#define PROJ_MAGIC 0x52474E4Au                 /* "JNGR": a RAM slot, today's layout */
#define PROJ_MAGIC_JNG 0x31474E4Au             /* "JNG1": stored, keyed (proj_to_jng / proj_from_jng) */
#define PROJ_MAGIC_V3 0x46554E33u              /* "FUN3": Felucca 0.9, 57 values a track = keys 0..56 */
#define PROJ_NP_V3 57u
#define PROJ_DEF ((int16_t)-32768)             /* a value the stored data has not: its default (proj_fill) */
#define PROJ_MAGIC_V2 0x46554E32u              /* "FUN2": four tracks, PROJ_NP_V2 parameters; read only */
#define PROJ_MAGIC_V1 0x46554E31u              /* "FUN1": one instrument; loads into track 1 */
#define PROJ_NP_V2 53u                         /* P_COUNT of formats 1 and 2 (P_E0 was 45) */
#define PROJ_NG_V2 27u                         /* G_COUNT of formats 1 and 2 */
typedef struct {                               /* one track; the drum track ignores engine / preset */
    int16_t p[P_COUNT];
    uint8_t engine, preset;
    step_t step[NSTEP];
    seqx_t x;                                  /* Jangada: nudges, conditions, locks (param: P_* in RAM) */
} proj_trk_t;
typedef struct {
    uint32_t magic, size;
    uint32_t layout;                           /* Jangada: proj_layout() of the build that wrote it */
    int16_t g[G_COUNT];
    uint8_t sel, rsv[3];                       /* the selected track */
    proj_trk_t t[NTRK];
    uint8_t fm6[NTRK][FM6_PACKED];             /* Jangada: each track's FM6 patch, packed (eng_fm6.c) */
    uint8_t has_fm6, rsv2[3];                  /* 0: the project had none (each track: its PTCH's patch) */
    uint32_t sum;
} project_t;
typedef struct {                               /* a track of format 3 (Felucca 0.9), read only */
    int16_t p[PROJ_NP_V3];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_v3_t;
typedef struct {                               /* format 3, read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v3_t t[NTRK];
    uint32_t sum;
} project_v3_t;
typedef struct {                               /* a track of formats 1 and 2, read only */
    int16_t p[PROJ_NP_V2];
    uint8_t engine, preset;
    step_t step[NSTEP];
} proj_trk_v2_t;
typedef struct {                               /* format 2 (until 0.9), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    uint8_t sel, rsv[3];
    proj_trk_v2_t t[NTRK];
    uint32_t sum;
} project_v2_t;
typedef struct {                               /* format 1 (until 0.5 beta), read only */
    uint32_t magic, size;
    int16_t g[PROJ_NG_V2];
    proj_trk_v2_t t;
    uint32_t sum;
} project_v1_t;
_Static_assert(sizeof(project_v2_t) == 2552u && sizeof(project_v1_t) == 688u, "formats 1 / 2 as they were stored");
_Static_assert(sizeof(project_v3_t) == 2584u, "format 3 as Felucca 0.9 stored it");
project_t proj_slot[4] __attribute__((section(".noinit")));

static uint32_t proj_hash(const void *p, uint32_t n)   /* FNV-1a over n bytes */
{
    const uint8_t *b = (const uint8_t *)p;
    uint32_t i, s = 0x811C9DC5u;
    for (i = 0; i < n; i++)
        s = (s ^ b[i]) * 16777619u;
    return s;
}
static uint32_t proj_sum(const project_t *p) { return proj_hash(p, sizeof *p - 4u); }
/* Jangada: a RAM slot survives a firmware update (.noinit): one written by a build with another
 * parameter layout must not be read as this one's (it would be the same size if parameters only
 * moved); then the keyed copy in flash is used */
static uint32_t proj_layout(void)
{
    uint32_t v[5] = {G_COUNT, NENGINES, sizeof(step_t), NSTEP, sizeof(seqx_t)};
    return proj_hash(P_KEY, sizeof P_KEY) ^ proj_hash(v, sizeof v);
}
static int proj_ok(const project_t *q)
{
    return q->magic == PROJ_MAGIC && q->size == sizeof *q && q->layout == proj_layout() && q->sum == proj_sum(q);
}

/* the globals of formats 1 and 2 (G_* unchanged since; any added later: their defaults) */
static void proj_g_from_v2(int16_t *g, const int16_t *g2)
{
    uint32_t i;
    for (i = 0; i < G_COUNT; i++)
        g[i] = i < PROJ_NG_V2 ? g2[i] : GP[i].def;
}

/* a track of formats 1 and 2 -> today's, mapped by count (see the top); drum: the drum track */
static void proj_trk_from_v2(proj_trk_t *d, const proj_trk_v2_t *s, int drum)
{
    uint32_t k, nc = PROJ_NP_V2 - 8u;
    for (k = 0; k < P_COUNT; k++)
        d->p[k] = PROJ_DEF;
    for (k = 0; k < nc; k++)                    /* P_LEVEL..: keys 0.. as they were */
        d->p[key_param(k)] = s->p[k];
    for (k = 0; k < 8u; k++)
        d->p[P_E0 + k] = s->p[nc + k];
    d->engine = drum ? 0u : s->engine;          /* (indices 0..7 as they were) */
    d->preset = drum ? 0u : s->preset;
    memcpy(d->step, s->step, sizeof d->step);
}

/* a format 2 project (n bytes in *v2) -> slot q as format 3 */
static int proj_from_v2(project_t *q, const project_v2_t *v2, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v2 || v2->magic != PROJ_MAGIC_V2 || v2->size != sizeof *v2 ||
        v2->sum != proj_hash(v2, sizeof *v2 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v2->g);
    q->sel = v2->sel;
    for (i = 0; i < NTRK; i++)
        proj_trk_from_v2(&q->t[i], &v2->t[i], i == TRK_DRUM);
    q->sum = proj_sum(q);
    return 1;
}

/* a format 1 project (n bytes in *v1) -> slot q as format 3: the instrument becomes track 1,
 * tracks 2..4 start empty (their sounds as at power-on) */
static int proj_from_v1(project_t *q, const project_v1_t *v1, int n)
{
    uint32_t i;
    if (n != (int)sizeof *v1 || v1->magic != PROJ_MAGIC_V1 || v1->size != sizeof *v1 ||
        v1->sum != proj_hash(v1, sizeof *v1 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v1->g);
    proj_trk_from_v2(&q->t[0], &v1->t, 0);
    for (i = 1; i < NTRK; i++) {               /* the other tracks: their defaults, no steps */
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)
            q->t[i].p[k] = k >= P_E0 && k < P_E0 + NEDIT ? ENGINES[trk_def_engine(i)]->edit[k - P_E0].def : TP[k].def;
        q->t[i].engine = (uint8_t)trk_def_engine(i);
        q->t[i].preset = 0xFF;                 /* 0xFF: its default preset (project_load) */
        for (k = 0; k < NSTEP; k++)
            q->t[i].step[k].time = ST_REST;
    }
    q->sum = proj_sum(q);
    return 1;
}

/* values a stored format did not have (PROJ_DEF) -> their defaults, for the track's engine */
static void proj_fill(project_t *q)
{
    uint32_t i, k;
    for (i = 0; i < NTRK; i++) {
        uint32_t e = q->t[i].engine % NENGINES;     /* (the drum track's byte is 0 unless T4 was SYNTH) */
        for (k = 0; k < P_COUNT; k++)
            if (q->t[i].p[k] == PROJ_DEF)
                q->t[i].p[k] = k >= P_E0 && k < P_E0 + NEDIT ? ENGINES[e]->edit[k - P_E0].def : TP[k].def;
    }
}

/* a format 3 project (Felucca 0.9) -> slot q: its 57 values are keys 0..56 */
static int proj_from_v3(project_t *q, const project_v3_t *v3, int n)
{
    static const uint8_t K3[PROJ_NP_V3] = {
        0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28,
        29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56};
    uint32_t i;
    if (n != (int)sizeof *v3 || v3->magic != PROJ_MAGIC_V3 || v3->size != sizeof *v3 ||
        v3->sum != proj_hash(v3, sizeof *v3 - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    proj_g_from_v2(q->g, v3->g);
    q->sel = v3->sel;
    for (i = 0; i < NTRK; i++) {
        key_map(K3, v3->t[i].p, PROJ_NP_V3, q->t[i].p, PROJ_DEF);
        q->t[i].engine = i == TRK_DRUM ? 0u : v3->t[i].engine;
        q->t[i].preset = i == TRK_DRUM ? 0u : v3->t[i].preset;
        memcpy(q->t[i].step, v3->t[i].step, sizeof q->t[i].step);
    }
    return 1;
}

/* "JNG1": magic, size, np, ng, sel, nsec, key[np] (+ a pad byte to even), g[ng],
 * NTRK x (p[np], engine, preset, step[NSTEP]), nsec x (tag, length u16, data[length]), FNV-1a of all
 * before. Little-endian, unaligned. nsec was 0 (Jangada 0.4 wrote no sections) */
#define JNG_HDR 12u
#define JNG_SIZE(np, ng) (JNG_HDR + (((np) + 1u) & ~1u) + 2u * (ng) + NTRK * (2u * (np) + 2u + sizeof(step_t) * NSTEP) + 4u)
#define JNG_SEC_FM6 1u                         /* section 1: NTRK x FM6_PACKED, the tracks' FM6 patches */
#define JNG_FM6_SIZE (3u + NTRK * FM6_PACKED)
#define JNG_SEC_SEQX 2u                        /* section 2: nudges, conditions, locks (see the top) */
#define JNG_SEQX_MAX (3u + NTRK * (1u + NSTEP + 3u * NLOCK))
static int seqx_sx_any(const seqx_t *x)
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++)
        if (x->sx[k])
            return 1;
    return 0;
}
static uint32_t seqx_nlock(const seqx_t *x)
{
    uint32_t k, n = 0;
    for (k = 0; k < NLOCK; k++)
        n += x->lock[k].step != LOCK_FREE;
    return n;
}
static uint32_t jng_seqx_len(const project_t *q)   /* section 2's data length, 0 = no section */
{
    uint32_t i, n = 0, any = 0;
    for (i = 0; i < NTRK; i++) {
        uint32_t sx = (uint32_t)seqx_sx_any(&q->t[i].x), nl = seqx_nlock(&q->t[i].x);
        any |= sx | nl;
        n += 1u + (sx ? NSTEP : 0u) + 3u * nl;
    }
    return any ? n : 0u;
}
static uint32_t jng_size(uint32_t np, uint32_t ng) { return JNG_SIZE(np, ng); }

static uint32_t proj_to_jng(const project_t *q, uint8_t *b)   /* -> bytes written */
{
    uint32_t xl = jng_seqx_len(q);
    uint32_t n = jng_size(P_COUNT, G_STORED) + (q->has_fm6 ? JNG_FM6_SIZE : 0u) + (xl ? 3u + xl : 0u), o = JNG_HDR, i, k, sum;
    uint32_t m = PROJ_MAGIC_JNG;
    memset(b, 0, n);
    memcpy(b, &m, 4);
    memcpy(b + 4, &n, 4);
    b[8] = P_COUNT;
    b[9] = G_STORED;                                   /* (not the macros: core.h) */
    b[10] = q->sel;
    b[11] = (uint8_t)((q->has_fm6 ? 1u : 0u) + (xl ? 1u : 0u));   /* sections */
    for (k = 0; k < P_COUNT; k++)
        b[o + k] = P_KEY[k];
    o += (P_COUNT + 1u) & ~1u;
    memcpy(b + o, q->g, 2u * G_STORED);
    o += 2u * G_STORED;
    for (i = 0; i < NTRK; i++) {
        memcpy(b + o, q->t[i].p, 2u * P_COUNT);
        o += 2u * P_COUNT;
        b[o++] = q->t[i].engine;
        b[o++] = q->t[i].preset;
        memcpy(b + o, q->t[i].step, sizeof q->t[i].step);
        o += sizeof q->t[i].step;
    }
    if (q->has_fm6) {                              /* section 1: the FM6 patches */
        b[o++] = JNG_SEC_FM6;
        b[o++] = (uint8_t)(NTRK * FM6_PACKED);
        b[o++] = (uint8_t)((NTRK * FM6_PACKED) >> 8);
        memcpy(b + o, q->fm6, sizeof q->fm6);
        o += sizeof q->fm6;
    }
    if (xl) {                                      /* section 2: nudges, conditions, locks */
        b[o++] = JNG_SEC_SEQX;
        b[o++] = (uint8_t)xl;
        b[o++] = (uint8_t)(xl >> 8);
        for (i = 0; i < NTRK; i++) {
            const seqx_t *x = &q->t[i].x;
            uint32_t sx = (uint32_t)seqx_sx_any(x);
            b[o++] = (uint8_t)((sx ? 0x80u : 0u) | seqx_nlock(x));
            if (sx) {
                memcpy(b + o, x->sx, NSTEP);
                o += NSTEP;
            }
            for (k = 0; k < NLOCK; k++)
                if (x->lock[k].step != LOCK_FREE) {
                    b[o++] = (uint8_t)(x->lock[k].step - 1u);
                    b[o++] = P_KEY[x->lock[k].param % P_COUNT];
                    b[o++] = (uint8_t)x->lock[k].val;
                }
        }
    }
    sum = proj_hash(b, o);
    memcpy(b + o, &sum, 4);
    return n;
}

static int proj_from_jng(project_t *q, const uint8_t *b, int n)
{
    uint32_t m, size, np, ng, o = JNG_HDR, i, sum, nsec, end;
    int16_t vals[KEY_MAX];
    if (n < (int)JNG_HDR + 4)
        return 0;
    memcpy(&m, b, 4);
    memcpy(&size, b + 4, 4);
    np = b[8];
    ng = b[9];
    nsec = b[11];
    if (m != PROJ_MAGIC_JNG || size != (uint32_t)n || np > KEY_MAX || !np || jng_size(np, ng) > size ||
        (!nsec && jng_size(np, ng) != size))
        return 0;
    {   /* the sections fill the rest exactly */
        uint32_t k, at = jng_size(np, ng) - 4u;
        for (k = 0; k < nsec; k++) {
            if (at + 3u > size - 4u)
                return 0;
            at += 3u + ((uint32_t)b[at + 1] | (uint32_t)b[at + 2] << 8);
        }
        if (at != size - 4u)
            return 0;
    }
    memcpy(&sum, b + size - 4u, 4);
    if (sum != proj_hash(b, size - 4u))
        return 0;
    memset(q, 0, sizeof *q);
    q->magic = PROJ_MAGIC;
    q->size = sizeof *q;
    q->sel = b[10];
    {
        const uint8_t *keys = b + o;
        o += (np + 1u) & ~1u;
        for (i = 0; i < G_COUNT; i++)          /* globals by position, as G_* has kept them */
            if (i < ng)
                memcpy(&q->g[i], b + o + 2u * i, 2);
            else
                q->g[i] = GP[i].def;
        o += 2u * ng;
        for (i = 0; i < NTRK; i++) {
            memcpy(vals, b + o, 2u * np);
            o += 2u * np;
            key_map(keys, vals, np, q->t[i].p, PROJ_DEF);
            q->t[i].engine = b[o++];
            q->t[i].preset = b[o++];
            memcpy(q->t[i].step, b + o, sizeof q->t[i].step);
            o += sizeof q->t[i].step;
        }
    }
    for (end = size - 4u; o + 3u <= end;) {         /* sections: the known ones, the others skipped */
        uint32_t tag = b[o], len = (uint32_t)b[o + 1] | (uint32_t)b[o + 2] << 8, k;
        o += 3u;
        if (tag == JNG_SEC_FM6 && len == sizeof q->fm6) {
            for (k = 0; k < sizeof q->fm6; k++)
                q->fm6[k / FM6_PACKED][k % FM6_PACKED] = b[o + k] & 0x7Fu;
            q->has_fm6 = 1;
        } else if (tag == JNG_SEC_SEQX) {
            uint32_t at = o, i2;
            for (i2 = 0; i2 < NTRK && at < o + len; i2++) {
                seqx_t *x = &q->t[i2].x;
                uint32_t f = b[at++], nl = f & 0x1Fu, j, w = 0;
                if ((f & 0x80u) && at + NSTEP <= o + len) {
                    memcpy(x->sx, b + at, NSTEP);
                    at += NSTEP;
                }
                for (j = 0; j < nl && at + 3u <= o + len; j++, at += 3u) {
                    uint32_t pid = key_param(b[at + 1]);
                    if (b[at] < NSTEP && pid < P_COUNT && w < NLOCK) {   /* (an unknown parameter: dropped) */
                        x->lock[w].step = (uint8_t)(b[at] + 1u);
                        x->lock[w].param = (uint8_t)pid;
                        x->lock[w].val = (int8_t)b[at + 2];
                        w++;
                    }
                }
            }
        }
        o += len;
    }
    return 1;
}

/* n bytes of a stored project (any format) -> slot q in today's layout; 0 = not a project */
static int proj_import(project_t *q, const void *b, int n)
{
    if (!proj_from_jng(q, (const uint8_t *)b, n) && !proj_from_v3(q, (const project_v3_t *)b, n) &&
        !proj_from_v2(q, (const project_v2_t *)b, n) && !proj_from_v1(q, (const project_v1_t *)b, n))
        return 0;
    proj_fill(q);
    q->layout = proj_layout();
    q->sum = proj_sum(q);
    return 1;
}

#ifndef PROJ_HOST
#if FELUCCA_FLASH
/* the stored form of a project, both ways (any format in, "JNG1" out) */
static uint8_t proj_io[ST_PAYLOAD_MAX] __attribute__((aligned(4)));
_Static_assert(JNG_SIZE(P_COUNT, G_STORED) + JNG_FM6_SIZE + JNG_SEQX_MAX <= ST_PAYLOAD_MAX && sizeof(project_v3_t) <= ST_PAYLOAD_MAX,
               "a stored project fits one flash object");

/* slot from flash into RAM (format 3, or format 2 / 1 converted) */
static void proj_fetch(uint32_t slot)
{
    project_t *q = &proj_slot[slot & 3u];
    int n = st_load(OBJ_PROJECT0 + (slot & 3u), proj_io, sizeof proj_io);
    if (!proj_import(q, proj_io, n))
        q->magic = 0;
}
#endif

/* the working project -> p (also the autosave) */
static void proj_capture(project_t *p)
{
    uint8_t pk[FM6_PACKED];
    uint32_t i;
    memset(p, 0, sizeof *p);
    p->magic = PROJ_MAGIC;
    p->size = sizeof *p;
    p->layout = proj_layout();
    fm1_irq_off();                                     /* Jangada: live recording writes steps in the ISR */
    for (i = 0; i < G_COUNT; i++)
        p->g[i] = song.g[i];
    p->sel = song.sel;
    for (i = 0; i < NTRK; i++) {
        uint32_t k;
        for (k = 0; k < P_COUNT; k++)                  /* (a lock in force: the value under it) */
            p->t[i].p[k] = p_unlocked(&trk[i], k);
        p->t[i].engine = trk[i].eng_req;
        p->t[i].preset = trk[i].preset;
        memcpy(p->t[i].step, trk[i].step, sizeof trk[i].step);
        p->t[i].x = trk[i].x;
    }
    fm1_irq_on();
    for (i = 0; i < NTRK; i++) {                       /* the tracks' FM6 patches (main loop owns fm6_patch) */
        memset(pk, 0, sizeof pk);
        fm6_pack(fm6_patch[i], pk);
        memcpy(p->fm6[i], pk, sizeof pk);
    }
    p->has_fm6 = 1;
    p->sum = proj_sum(p);
}

static void project_save(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
    proj_capture(p);
#if FELUCCA_FLASH
    if (flash_ok) {
        uint32_t n = proj_to_jng(p, proj_io);           /* stored keyed: "JNG1" */
        ui_message(st_save(OBJ_PROJECT0 + (slot & 3u), proj_io, n) ? "SAVE ERROR" : "SAVED");
        return;
    }
#endif
    ui_message("SAVED (RAM)");
}

static void proj_apply(const project_t *p);

static void project_load(uint32_t slot)
{
    project_t *p = &proj_slot[slot & 3u];
#if FELUCCA_FLASH
    if (flash_ok && !proj_ok(p))
        proj_fetch(slot);
#endif
    if (!proj_ok(p)) {
        ui_message("EMPTY SLOT");
        return;
    }
    proj_apply(p);
    ui_message("LOADED");
}

/* a project into the working one: the transport stops, everything sounding is released, every value
 * back inside its range */
static void proj_apply(const project_t *p)
{
    uint32_t i, k;
    transport_req = 2;
    panic_req = (1u << NTRK) - 1u;
    fm1_irq_off();                                      /* the audio ISR must not see half a project */
    for (i = 0; i < G_COUNT; i++)
        if (i != G_SLOT && i != G_LOAD && i != G_SAVE && i < G_STORED)   /* (the macros stay where they are) */
            song.g[i] = (int16_t)clamp(p->g[i], GP[i].min, GP[i].max);
    if (song.t4 != (song.g[G_T4] != 0)) {               /* Jangada: the project's track 4 type, before */
        t4_reset(TDRUM);                                /* the tracks below are read with it */
        song.t4 = (uint8_t)(song.g[G_T4] != 0);
    }
    for (k = 0; k < NTRK; k++) {
        track_t *t = &trk[k];
        const proj_trk_t *s = &p->t[k];
        uint32_t e = trk_synth(k) ? s->engine % NENGINES : 0u;   /* (globals, G_T4 too, are loaded above) */
        t->eng_req = (uint8_t)e;
        t->user = 0;                                    /* (no user preset slot is saved) */
        t->lk_n = 0;                                    /* (the locks in force: the values come from the project) */
        for (i = 0; i < P_COUNT; i++) {                 /* every value back inside its range */
            const param_desc_t *d = i >= P_E0 && i < P_E0 + NEDIT ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
            t->p[i] = (int16_t)clamp(s->p[i], d->min, d->max);
        }
        t->preset = (uint8_t)(ENGINES[e]->npresets ? (s->preset == 0xFFu ? 0u : s->preset) % ENGINES[e]->npresets : 0u);
        memcpy(t->step, s->step, sizeof t->step);
        for (i = 0; i < NSTEP; i++) {
            step_t *st = &t->step[i];
            uint32_t j;
            if (st->n > 4u)
                st->n = 4;
            if (st->time > ST_REST)
                st->time = ST_REST;
            for (j = 0; j < 4u; j++)
                st->note[j] &= 127u;
            st->vel &= 127u;                            /* Jangada: > 127 overflowed the voice amplitude */
            st->flags &= SF_STEP;
        }
        t->x = s->x;
        for (i = 0; i < NSTEP; i++)                     /* (condition 3: as normal) */
            if ((t->x.sx[i] >> SX_COND_SH) > FC_NOFILL)
                t->x.sx[i] &= SX_MICRO;
        for (i = 0; i < NLOCK; i++) {                   /* a lock on no step, of no lockable parameter: free */
            plock_t *l = &t->x.lock[i];
            if (l->step > NSTEP || (l->step && (l->param >= P_COUNT || !p_lockable(t, l->param))))
                l->step = LOCK_FREE;
            if (l->step == LOCK_FREE)
                l->param = 0, l->val = 0;
            else {
                const param_desc_t *d = lock_desc(t, l->param);
                l->val = (int8_t)clamp(l->val, d->min, d->max);
            }
        }
    }
    song.sel = (uint8_t)(p->sel < NTRK ? p->sel : 0u);
    fm1_irq_on();
    for (k = 0; k < NTRK; k++) {                        /* Jangada: the project's own FM6 patches, PTCH as saved
                                                         * (fm6_poll keeps them); a project without: PTCH's patch */
        if (p->has_fm6) {
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(p->fm6[k], v);
            fm6_set_patch(k, v);
            fm6_slot[k] = (uint8_t)trk[k].p[P_E7];
        } else {
            fm6_slot[k] = 0xFFu;
        }
    }
    for (k = 0; k < NPART; k++)                         /* a format 1 project: the default sounds of tracks 2, 3 */
        if (p->t[k].preset == 0xFFu) {
            apply_preset_to(&trk[k], TRK_DEF[k][1]);
            track_defaults_steps(&trk[k]);
        }
    sync_reload = 1;
    ui.force = 1;
}

#if FELUCCA_FLASH
/* Jangada (after SLOOP): the working project, kept in flash by itself: saved when it changed, the
 * transport is stopped, nothing sounds and the panel was not touched for AUTOSAVE_IDLE (a flash erase
 * stops the audio for ~50 ms: never while something plays); loaded at power-on (autosave_resume) */
#define AUTOSAVE_IDLE 2500u                    /* ms without input */
#define AUTOSAVE_GAP 20000u                    /* ms between two saves at least */
static project_t autosave_buf;
static uint32_t autosave_sum, autosave_ms, autosave_checked;
static uint32_t autosave_hold;                 /* fm1_ms until which it waits: an editor backup uses proj_io
                                                * (editor_backup.c), or a staged FM6 bank half (fm6_bank.c) */
static uint32_t proj_io_bk;                    /* fm1_ms until which the editor's backup owns proj_io (its snapshot
                                                * or its staging, editor_backup.c): a bank write (fm6_bank.c)
                                                * is refused meanwhile, so it cannot corrupt a restore */

static int audio_quiet(void)                   /* no voice of any track, no drum */
{
    uint32_t i, k;
    for (i = 0; i < NTRK; i++)
        for (k = 0; k < NVOICE; k++)
            if (trk[i].v[k].active)
                return 0;
    for (k = 0; k < NDRUM; k++)
        if (drums.v[k].active)
            return 0;
    return 1;
}

static void autosave_tick(void)                /* main loop */
{
    uint32_t now = fm1_ms, n;
    if (settings_later && !song.playing) {     /* (a flash write stops the audio ~50 ms: not while playing) */
        settings_later = 0;
        settings_save();
    }
    if (!flash_ok || song.playing || ui.menu || now - ui_input_ms < AUTOSAVE_IDLE ||
        now - autosave_ms < AUTOSAVE_GAP || now - autosave_checked < 1000u || (int32_t)(autosave_hold - now) > 0)
        return;
    autosave_checked = now;
    proj_capture(&autosave_buf);
    if (autosave_buf.sum == autosave_sum || !audio_quiet())
        return;
    n = proj_to_jng(&autosave_buf, proj_io);
    if (st_save(OBJ_AUTOSAVE, proj_io, n) == 0) {
        autosave_sum = autosave_buf.sum;
        autosave_ms = now;
    }
}

/* power-on: the project as it was left. Not after a failed boot (a project that crashed it stays
 * out), nor with OCT+ held alone (a new project) */
static void autosave_resume(void)
{
    int n;
    if (!flash_ok || bootguard.failed || (fm1_in.buttons & 3u) == 2u) {
        autosave_sum = 0;
        return;
    }
    n = st_load(OBJ_AUTOSAVE, proj_io, sizeof proj_io);
    if (n <= 0 || !proj_import(&autosave_buf, proj_io, n) || !proj_ok(&autosave_buf))
        return;
    proj_apply(&autosave_buf);
    autosave_sum = autosave_buf.sum;
}
#endif

/* settings + learned panel table: one flash object. The flash copy wins at
 * boot (the .noinit copies are garbage after a power-off). */
typedef struct {
    uint32_t magic, palette, lowcut, zoom;         /* zoom: reserved (Jangada: ZOOM left the menu; the layout
                                                    * stays for the flash copy and the backups: written as 0) */
    panel_t panel;
    uint32_t lights;                               /* Jangada: menu LIGHTS / KEYS / NOTES / USB AUDIO (panel.c
                                                    * lights_word); appended, so 0.2 still reads its part */
} persist_t;
#define PERSIST_MAGIC 0x50455232u                  /* "PER2" */
#define PERSIST_SIZE_V02 __builtin_offsetof(persist_t, lights)   /* as Jangada 0.2 wrote it (no lights) */
_Static_assert(sizeof(persist_t) == PERSIST_SIZE_V02 + 4u, "lights: the last word, no padding before it");
#if FELUCCA_FLASH
static persist_t persist_saved;
#endif

static void persist_boot(void)                    /* before settings_init / panel_init */
{
#if FELUCCA_FLASH
    persist_t p;
    uint32_t f = irq_save();
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                        /* flash above 0x93000 reads as plaintext through XIP
                                                    * (user sample sets are played from there) */
    {
        uint32_t k;
        for (k = 0; k < SMP_USER_SLOTS; k++)
            smp_user_scan(k);
    }
    {
        int n = st_load(OBJ_SETTINGS, &p, sizeof p);
        if (n == (int)PERSIST_SIZE_V02)
            p.lights = 0;                           /* from 0.2: lights off, USB AUDIO MASTER */
        if ((n == (int)sizeof p || n == (int)PERSIST_SIZE_V02) && p.magic == PERSIST_MAGIC && p.palette < NPALETTES &&
            p.lowcut <= 1u) {
            settings.magic = SETTINGS_MAGIC;        /* (each value checked as it is read: after SLOOP 2.3) */
            settings.palette = p.palette;
            settings.lowcut = p.lowcut;             /* (p.zoom: reserved, ignored) */
            if (panel_valid(&p.panel))
                panel = p.panel;
            lights_from_word(p.lights);
            persist_saved = p;
        } else if (n == (int)(8u + sizeof(panel_t)) && p.magic == 0x50455231u) {   /* "PER1": palette, panel */
            const uint32_t *w = (const uint32_t *)&p;
            panel_t old;
            memcpy(&old, w + 2, sizeof old);
            settings.magic = SETTINGS_MAGIC;
            settings.palette = w[1] < NPALETTES ? w[1] : 5u;
            settings.lowcut = 0;
            if (panel_valid(&old))
                panel = old;
        }
    }
    {   /* projects: fill empty RAM slots from flash, so the slot list is right after power-on */
        uint32_t i;
        for (i = 0; i < 4u; i++)
            if (!proj_ok(&proj_slot[i]))
                proj_fetch(i);
    }
    up_boot();                                     /* user presets */
#endif
}

static int project_used(uint32_t slot) { return proj_ok(&proj_slot[slot & 3u]); }

static void settings_save(void)
{
#if FELUCCA_FLASH
    persist_t p;
    if (!flash_ok)
        return;
    memset(&p, 0, sizeof p);
    p.magic = PERSIST_MAGIC;
    p.palette = settings.palette;
    p.lowcut = settings.lowcut;
    p.zoom = 0;                                    /* (reserved) */
    p.panel = panel;
    p.lights = lights_word();
    if (!memcmp(&p, &persist_saved, sizeof p))
        return;                                    /* unchanged: no erase cycle */
    if (st_save(OBJ_SETTINGS, &p, sizeof p) == 0)
        persist_saved = p;
#endif
}

#if FELUCCA_FLASH
_Static_assert(sizeof(project_t) <= ST_PAYLOAD_MAX, "project does not fit one flash sector");
#endif
#endif /* PROJ_HOST */
