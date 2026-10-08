/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part): a format 2 ("FUN2",
 * 53 parameters per track) and a format 1 ("FUN1") project, built byte for byte as the firmware
 * before the SLICER stored them, convert to format 3 ("FUN3"): every old value at its parameter, the
 * SLICER parameters at their defaults (OFF), steps, globals, selection, the engine bytes (0..7 kept:
 * the engines added since were appended; the drum track's byte 0); damaged ones are refused.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
static uint32_t trk_def_engine(uint32_t i)       /* ui.c TRK_DEF: ANALOG, DIGITAL, LOFI */
{
    static const uint8_t E[NPART] = {0, 1, 3};
    return i < NPART ? E[i] : 0u;
}
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* the value parameter k (old id) of track t had in the old project */
static int16_t oldv(uint32_t t, uint32_t k) { return (int16_t)(t * 100u + k * 3u + 1u); }

static const uint8_t OLD_ENG[NTRK] = {7, 0, 6, 8};   /* WHEEL, ANALOG, TRIO; the drum track: 8 (none) */
static void fill_v2_track(proj_trk_v2_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V2; k++)
        d->p[k] = oldv(t, k);
    d->engine = OLD_ENG[t];
    d->preset = (uint8_t)(t + 5u);
    for (k = 0; k < NSTEP; k++) {
        step_t *s = &d->step[k];
        s->note[0] = (uint8_t)(36u + (k + t) % 40u);
        s->n = (uint8_t)(k % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)(k & 3u);
        s->vel = (uint8_t)(64u + t);
    }
}

/* track t of the converted project has the old values where they belong */
static int track_ok(const proj_trk_t *n, const proj_trk_v2_t *o, uint32_t t)
{
    uint32_t k;
    int ok = (t == TRK_DRUM ? n->engine == 0 && n->preset == 0 : n->engine == o->engine && n->preset == o->preset) &&
             !memcmp(n->step, o->step, sizeof n->step);
    for (k = 0; k <= P_DETUNE; k++)
        ok &= n->p[k] == oldv(t, k);
    ok &= n->p[P_SLCR] == 0 && n->p[P_SLPAT] == TP[P_SLPAT].def && n->p[P_SLRATE] == TP[P_SLRATE].def &&
          n->p[P_SLDEPTH] == TP[P_SLDEPTH].def;
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == oldv(t, 45u + k);
    for (k = P_M1SRC; k <= P_M4AMT; k++)              /* Jangada's matrix: off */
        ok &= n->p[k] == TP[k].def;
    for (k = 8u; k < NEDIT; k++)                      /* E9..: the engine's defaults */
        ok &= n->p[P_E0 + k] == ENGINES[t == TRK_DRUM ? 0u : o->engine % NENGINES]->edit[k].def;
    return ok;
}

int main(void)
{
    static project_v2_t v2;
    static project_v1_t v1;
    static project_t q, q2;
    static project_v3_t v3;
    static union {
        project_t ram;
        project_v3_t v3;
        project_v2_t v2;
        project_v1_t v1;
        uint8_t jng[4096];
    } buf;
    uint32_t i, t;
    int bad = 0, ok;

    bad += check("keys: Felucca's format-3 positions are keys 0..56",
                 P_KEY[P_LEVEL] == 0 && P_KEY[P_SLDEPTH] == 48 && P_KEY[P_E0] == 49 && P_KEY[P_E7] == 56);
    bad += check("JNG1 fits one flash object", JNG_SIZE(P_COUNT, G_COUNT) <= 4096u - 256u &&
                                                   sizeof(project_v3_t) <= 4096u - 256u);

    /* format 2, as written before the SLICER */
    memset(&v2, 0, sizeof v2);
    v2.magic = PROJ_MAGIC_V2;
    v2.size = sizeof v2;
    for (i = 0; i < PROJ_NG_V2; i++)
        v2.g[i] = (int16_t)(500 + i);
    v2.sel = 2;
    for (t = 0; t < NTRK; t++)
        fill_v2_track(&v2.t[t], t);
    v2.sum = proj_hash(&v2, sizeof v2 - 4u);
    bad += check("FUN2 image is 2552 bytes (as stored)", sizeof v2 == 2552u);
    memcpy(&buf, &v2, sizeof v2);
    ok = proj_import(&q, &buf, (int)sizeof v2);
    bad += check("FUN2 -> FUN3: converted, valid format 3 slot", ok && proj_ok(&q) && q.magic == PROJ_MAGIC);
    ok = q.sel == 2;
    for (i = 0; i < G_COUNT; i++)                      /* globals added since (G_T4): their defaults */
        ok &= q.g[i] == (i < PROJ_NG_V2 ? (int16_t)(500 + i) : GP[i].def);
    bad += check("FUN2 -> FUN3: globals and selected track", ok);
    ok = 1;
    for (t = 0; t < NTRK; t++)
        ok &= track_ok(&q.t[t], &v2.t[t], t);
    bad += check("FUN2 -> FUN3: every parameter mapped, SLICER OFF (4 tracks)", ok);

    bad += check("FUN2 -> FUN3: engine bytes kept (WHEEL 7, ANALOG 0, TRIO 6), drum 0",
                 q.t[0].engine == 7 && q.t[1].engine == 0 && q.t[2].engine == 6 && q.t[3].engine == 0 &&
                 str_eq(ENGINES[7]->name, "WHEEL") && str_eq(ENGINES[6]->name, "TRIO") && NENGINES > 8);

    /* a Felucca 0.9 project (FUN3: 57 values = keys 0..56) from this one (an engine added
     * since: SLICE, 8) */
    q.t[1].engine = 8;
    for (i = 8u; i < NEDIT; i++)                       /* E9..: FUN3 has none: engine 8's defaults */
        q.t[1].p[P_E0 + i] = PROJ_DEF;
    proj_fill(&q);
    q.sum = proj_sum(&q);
    memset(&v3, 0, sizeof v3);
    v3.magic = PROJ_MAGIC_V3;
    v3.size = sizeof v3;
    for (i = 0; i < PROJ_NG_V2; i++)
        v3.g[i] = q.g[i];
    v3.sel = q.sel;
    for (t = 0; t < NTRK; t++) {
        for (i = 0; i < PROJ_NP_V3; i++)
            v3.t[t].p[i] = q.t[t].p[key_param(i)];
        v3.t[t].engine = q.t[t].engine;
        v3.t[t].preset = q.t[t].preset;
        memcpy(v3.t[t].step, q.t[t].step, sizeof v3.t[t].step);
    }
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    memcpy(&buf, &v3, sizeof v3);
    bad += check("FUN3 (Felucca 0.9) -> today: every value (engine 8 too)",
                 proj_import(&q2, &buf, (int)sizeof v3) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == 8);

    {   /* JNG1: the stored form, keyed */
        uint32_t n;
        q.t[2].p[P_M2SRC] = 3;                         /* values only Jangada has */
        q.t[2].p[P_M2AMT] = -20;
        q.t[0].p[P_E0 + 12] = 5;
        q.sum = proj_sum(&q);
        n = proj_to_jng(&q, buf.jng);
        q.g[G_T4] = 1;                                 /* track 4 as a synth: its engine, kept */
        q.t[TRK_DRUM].engine = 6;
        q.t[TRK_DRUM].preset = 2;
        q.sum = proj_sum(&q);
        n = proj_to_jng(&q, buf.jng);
        bad += check("JNG1: T4 SYNTH with its engine / preset", proj_import(&q2, buf.jng, (int)n) && q2.g[G_T4] == 1 &&
                                                                q2.t[TRK_DRUM].engine == 6 && q2.t[TRK_DRUM].preset == 2);
        bad += check("JNG1 -> today: as stored", n == JNG_SIZE(P_COUNT, G_COUNT) &&
                                                     proj_import(&q2, buf.jng, (int)n) && !memcmp(&q, &q2, sizeof q));
        buf.jng[JNG_HDR + P_LEVEL] = 120;              /* a key this build does not know, instead of LEVEL's */
        {
            uint32_t sum = proj_hash(buf.jng, n - 4u);
            memcpy(buf.jng + n - 4u, &sum, 4);
        }
        ok = proj_import(&q2, buf.jng, (int)n);
        for (t = 0; t < NTRK; t++)
            for (i = 0; i < P_COUNT; i++)
                ok &= q2.t[t].p[i] == (i == P_LEVEL ? TP[P_LEVEL].def : q.t[t].p[i]);
        bad += check("JNG1: unknown key skipped, missing one its default", ok);
        buf.jng[20]++;
        bad += check("JNG1 with a bad checksum: refused", !proj_import(&q2, buf.jng, (int)n));
    }
    {   /* JNG1 sections (Jangada 0.5): the tracks' FM6 patches; a project of 0.4 has none */
        uint32_t n, k, sum;
        bad += check("JNG1 of Jangada 0.4 (no sections): no FM6 patches (PTCH's patch on load)",
                     proj_import(&q2, buf.jng, 0) == 0 && (proj_to_jng(&q, buf.jng), proj_import(&q2, buf.jng, (int)JNG_SIZE(P_COUNT, G_COUNT))) &&
                         !q2.has_fm6 && buf.jng[11] == 0);
        for (t = 0; t < NTRK; t++)
            for (k = 0; k < FM6_PACKED; k++)
                q.fm6[t][k] = (uint8_t)((t * 31u + k * 7u) & 127u);
        q.has_fm6 = 1;
        q.sum = proj_sum(&q);
        n = proj_to_jng(&q, buf.jng);
        bad += check("JNG1 with the FM6 section: stored and read back as it was",
                     n == JNG_SIZE(P_COUNT, G_COUNT) + JNG_FM6_SIZE && buf.jng[11] == 1 && n <= 4096u - 256u &&
                         proj_import(&q2, buf.jng, (int)n) && !memcmp(&q, &q2, sizeof q) && q2.has_fm6);
        /* a section a later firmware may add (tag 9, 5 bytes) before the FM6 one: skipped */
        memmove(buf.jng + JNG_SIZE(P_COUNT, G_COUNT) - 4u + 8u, buf.jng + JNG_SIZE(P_COUNT, G_COUNT) - 4u, JNG_FM6_SIZE);
        memcpy(buf.jng + JNG_SIZE(P_COUNT, G_COUNT) - 4u, "\x09\x05\x00" "abcde", 8);
        buf.jng[11] = 2;
        n += 8u;
        memcpy(buf.jng + 4, &n, 4);
        sum = proj_hash(buf.jng, n - 4u);
        memcpy(buf.jng + n - 4u, &sum, 4);
        bad += check("JNG1: an unknown section skipped, the FM6 one read",
                     proj_import(&q2, buf.jng, (int)n) && !memcmp(q2.fm6, q.fm6, sizeof q.fm6) && q2.has_fm6);
        buf.jng[JNG_SIZE(P_COUNT, G_COUNT) - 4u + 1u] = 6;   /* its length now runs past the end */
        sum = proj_hash(buf.jng, n - 4u);
        memcpy(buf.jng + n - 4u, &sum, 4);
        bad += check("JNG1: sections that do not fill it exactly: refused", !proj_import(&q2, buf.jng, (int)n));
        q.has_fm6 = 0;
        q.sum = proj_sum(&q);
    }
    {   /* JNG1 section 2 (Jangada 0.7, after SLOOP 2.4): nudges, conditions, locks; at its largest (every step
         * nudged, every lock slot used, the FM6 section too) it still fits one flash object */
        uint32_t n, k;
        for (t = 0; t < NTRK; t++) {
            for (k = 0; k < NSTEP; k++)
                q.t[t].x.sx[k] = (uint8_t)(((k * 5u + t) & SX_MICRO) | ((k + t) % 3u) << SX_COND_SH);
            for (k = 0; k < NLOCK; k++) {
                q.t[t].x.lock[k].step = (uint8_t)(1u + (k * 7u + t) % NSTEP);
                q.t[t].x.lock[k].param = (uint8_t)(k & 1u ? P_E0 + k % 8u : P_ATK + k % 4u);
                q.t[t].x.lock[k].val = (int8_t)(k * 9 - 40);
            }
        }
        for (t = 0; t < NTRK; t++)
            for (k = 0; k < FM6_PACKED; k++)
                q.fm6[t][k] = (uint8_t)((t * 13u + k) & 127u);
        q.has_fm6 = 1;
        q.sum = proj_sum(&q);
        n = proj_to_jng(&q, buf.jng);
        bad += check("JNG1 section 2 at its largest: fits one flash object",
                     n == JNG_SIZE(P_COUNT, G_COUNT) + JNG_FM6_SIZE + JNG_SEQX_MAX && n <= 4096u - 256u && buf.jng[11] == 2);
        bad += check("JNG1 section 2: nudges, conditions, locks read back as they were",
                     proj_import(&q2, buf.jng, (int)n) && !memcmp(&q, &q2, sizeof q));
        memset(&q.t[1].x, 0, sizeof q.t[1].x);         /* a track with none, one with only locks */
        memset(q.t[2].x.sx, 0, sizeof q.t[2].x.sx);
        memset(q.fm6, 0, sizeof q.fm6);
        q.has_fm6 = 0;
        q.sum = proj_sum(&q);
        n = proj_to_jng(&q, buf.jng);
        bad += check("JNG1 section 2: tracks with none / only locks, without the FM6 section",
                     n == JNG_SIZE(P_COUNT, G_COUNT) + 3u + (1u + NSTEP + 3u * NLOCK) * 2u + 1u + 3u * NLOCK + 1u &&
                         proj_import(&q2, buf.jng, (int)n) && !memcmp(&q, &q2, sizeof q) && !q2.has_fm6);
        for (t = 0; t < NTRK; t++)
            memset(&q.t[t].x, 0, sizeof q.t[t].x);
        q.sum = proj_sum(&q);
        n = proj_to_jng(&q, buf.jng);
        bad += check("JNG1: no nudge, condition or lock: no section 2", n == JNG_SIZE(P_COUNT, G_COUNT) && buf.jng[11] == 0);
    }

    /* damaged / wrong size */
    v2.t[1].p[3]++;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v2));
    v2.t[1].p[3]--;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a wrong length: refused", !proj_import(&q2, &buf, (int)sizeof v2 - 2));
    memcpy(&buf, &v3, sizeof v3);
    buf.v3.magic = PROJ_MAGIC_V2;
    bad += check("FUN3 size with a FUN2 magic: refused", !proj_import(&q2, &buf, (int)sizeof v3));

    /* format 1: one instrument -> track 1, the others their defaults */
    memset(&v1, 0, sizeof v1);
    v1.magic = PROJ_MAGIC_V1;
    v1.size = sizeof v1;
    for (i = 0; i < PROJ_NG_V2; i++)
        v1.g[i] = (int16_t)(700 + i);
    fill_v2_track(&v1.t, 0);
    v1.sum = proj_hash(&v1, sizeof v1 - 4u);
    memcpy(&buf, &v1, sizeof v1);
    ok = proj_import(&q, &buf, (int)sizeof v1) && proj_ok(&q) && track_ok(&q.t[0], &v1.t, 0) && q.g[5] == 705;
    for (t = 1; t < NTRK; t++)
        ok &= q.t[t].preset == 0xFF && q.t[t].p[P_SLCR] == 0 && q.t[t].p[P_LEVEL] == TP[P_LEVEL].def &&
              q.t[t].p[P_E0] == ENGINES[trk_def_engine(t)]->edit[0].def && q.t[t].step[0].time == ST_REST;
    bad += check("FUN1 -> FUN3: track 1 mapped, tracks 2..4 defaults", ok);

    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
