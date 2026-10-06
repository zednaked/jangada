/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the modulation matrix. NMOD slots per part (P_M1SRC..), each SRC -> DST x AMT,
 * worked out per voice in track_render (voice.c).
 *
 * Sources: LFO (the part's LFO, bipolar), ENV (the voice's ADSR), VEL (note velocity),
 * KEY (the note around C4: +-1 at +-4 octaves), RND (a value fixed per note, from its age:
 * it does not draw from rng(), so nothing else changes when it is used), MODW / AT / EXPR (MIDI),
 * DRIFT (the part's slow random walk, drone.c: tens of seconds to minutes, never the same twice).
 *
 * Targets: FLT / PIT / SHP add to the voice's modulation as LFO DEST and ENV DEST do (the
 * same scale: AMT 63 is what DEST 63 is there). E1..E16 move the engine's own parameter for
 * this voice's render: full AMT sweeps its whole range. That is what the engine reads while
 * it renders; values an engine only reads at note-on are not modulated.
 *
 * With every SRC at OFF nothing here runs, and the sound is Felucca's to the sample. */
enum { MS_OFF, MS_LFO, MS_ENV, MS_VEL, MS_KEY, MS_RND, MS_MODW, MS_AT, MS_EXPR,   /* (MIDI: appended) */
       MS_DRIFT };                                     /* Jangada DRONES: the part's slow walk (drone.c) */
enum { MD_FLT, MD_PIT, MD_SHP, MD_E0 };
#include "drone.c"                                     /* Jangada DRONES: EVOL, TENS, RAMP and DRIFT */

/* DST names follow the engine: E1.. become its labels */
static const char *mod_dst_names[MD_E0 + NEDIT];
static const engine_t *mod_dst_eng;
static param_desc_t mod_dst_d;

static const param_desc_t *mod_dst_desc(const engine_t *e)
{
    uint32_t k;
    if (e != mod_dst_eng) {
        mod_dst_eng = e;
        mod_dst_names[MD_FLT] = "FLT";
        mod_dst_names[MD_PIT] = "PIT";
        mod_dst_names[MD_SHP] = "SHP";
        for (k = 0; k < NEDIT; k++)
            mod_dst_names[MD_E0 + k] = e->edit[k].label ? e->edit[k].label : "--";
        mod_dst_d.label = "DST";
        mod_dst_d.fmt = F_ENUM;
        mod_dst_d.min = 0;
        mod_dst_d.max = MD_E0 + NEDIT - 1;
        mod_dst_d.def = 0;
        mod_dst_d.names = mod_dst_names;
        mod_dst_d.unit = 0;
    }
    return &mod_dst_d;
}

__attribute__((noinline)) static int mod_active(const track_t *t)
{
    uint32_t k;
    for (k = 0; k < NMOD; k++)
        if (t->p[P_M1SRC + 3u * k] && t->p[P_M1AMT + 3u * k])
            return 1;
    return 0;
}

static int32_t mod_src(const track_t *t, const voice_t *v, uint32_t src, int32_t lfo, int32_t env)
{
    switch (src) {
    case MS_LFO:
        return lfo;
    case MS_ENV:
        return env;
    case MS_VEL:
        return (int32_t)v->vel * 258;
    case MS_KEY:
        return clamp((v->pitch16 - 60 * 16) * 32767 / (48 * 16), -32767, 32767);
    case MS_RND:
        return v->rnd;
    case MS_MODW:                                      /* Jangada: MIDI CC1, aftertouch, CC11 (0..1) */
        return (int32_t)t->mw * 258;
    case MS_AT:
        return (int32_t)t->at * 258;
    case MS_EXPR:
        return (int32_t)t->ex * 258;
    case MS_DRIFT:
        return drn[(uint32_t)(t - trk) % NTRK].w[0];
    }
    return 0;
}

/* DRIFT in a slot: the walks run (drone.c) */
__attribute__((noinline)) static uint32_t mod_drift(const track_t *t)
{
    uint32_t k;
    for (k = 0; k < NMOD; k++)
        if (t->p[P_M1SRC + 3u * k] == MS_DRIFT && t->p[P_M1AMT + 3u * k])
            return 1;
    return 0;
}

/* one voice: FLT / PIT / SHP into d[0..2] (cutoff, 1/16 semitone, shape: the units of vmod_t),
 * engine parameters straight into t->p, their own values kept in keep[]; returns a bit per
 * parameter moved, for mod_restore */
/* kept out of line: the audio ISR only pays a call when a slot is on */
__attribute__((noinline)) static uint32_t mod_voice(track_t *t, const engine_t *e, const voice_t *v, int32_t lfo,
                                                    int32_t env, int32_t *d, int16_t *keep)
{
    int32_t acc[NEDIT];
    uint32_t k, j, moved = 0;
    d[0] = d[1] = d[2] = 0;
    for (k = 0; k < NMOD; k++) {
        uint32_t src = (uint32_t)t->p[P_M1SRC + 3u * k], dst = (uint32_t)t->p[P_M1DST + 3u * k];
        int32_t amt = t->p[P_M1AMT + 3u * k], s;
        if (!src || !amt)
            continue;
        s = mod_src(t, v, src, lfo, env);
        if (dst == MD_FLT)
            d[0] += (s * amt) >> 7;
        else if (dst == MD_PIT)
            d[1] += (s * amt * 3) >> 15;
        else if (dst == MD_SHP)
            d[2] += (s * amt) >> 7;
        else if (dst < MD_E0 + NEDIT) {
            const param_desc_t *pd = &e->edit[j = dst - MD_E0];
            if (!pd->label || pd->max <= pd->min)
                continue;
            if (!(moved >> j & 1u)) {
                moved |= 1u << j;
                acc[j] = 0;
            }
            acc[j] += s * amt;                       /* summed unscaled: opposite slots cancel exactly */
        }
    }
    for (j = 0; j < NEDIT; j++)                     /* the sum of the slots, clamped once */
        if (moved >> j & 1u) {
            keep[j] = t->p[P_E0 + j];
            t->p[P_E0 + j] = (int16_t)clamp(keep[j] + (((acc[j] >> 6) * (e->edit[j].max - e->edit[j].min)) >> 15),
                                            e->edit[j].min, e->edit[j].max);   /* full AMT: the whole range */
        }
    return moved;
}

__attribute__((noinline)) static void mod_restore(track_t *t, uint32_t moved, const int16_t *keep)
{
    uint32_t j;
    for (j = 0; moved; j++, moved >>= 1)
        if (moved & 1u)
            t->p[P_E0 + j] = keep[j];
}
