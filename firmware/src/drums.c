/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* GM drum part (track 4): the SAMPLE engine's KIT set (General MIDI percussion map,
 * tools/gen_samples.py GM_KIT), played by its step pattern, the keys when track 4
 * is selected, and its own MIDI channel (GLO -> DRUMS, default 10). Its own voices
 * (outside the parts' voice budget). One-shots: note-offs are ignored; a closed or
 * pedal hi-hat chokes the open one. LEVEL / REV: GLO > DRUMS (G_DRLVL, G_DRREV);
 * PAN and MUTE: the drum track's P_PAN / P_MUTE. Rendered from the audio ISR. */
#define NDRUM 6
#include "drum_synth.c"       /* Jangada: the synthesised kits (after SLOOP); G_KIT > 0 plays them */
#define DRUM_KITS (1u + DS_NKITS)
static const char *const DRUM_KIT_NAMES[DRUM_KITS] = {"GM", DS_KIT_NAME_LIST};
static const char *const DRUM_KIT_STYLES[DRUM_KITS] = {"GM KIT", DS_KIT_STYLE_LIST};
static uint32_t drum_kit(void) { return (uint32_t)clamp(song.g[G_KIT], 0, DRUM_KITS - 1); }

/* Jangada: the kits in the browser's order (DS_KIT_NAV: GM, then Jangada's own, then SLOOP's);
 * G_KIT stays the index it always was, so a saved project keeps its kit. The kit d places away
 * from kit (wrap: around the ends, else it stops there) */
static uint32_t drum_kit_step(uint32_t kit, int32_t d, int wrap)
{
    int32_t pos = 0;
    while (pos < (int32_t)DRUM_KITS - 1 && DS_KIT_NAV[pos] != kit)
        pos++;
    pos += d;
    if (wrap)
        pos = ((pos % (int32_t)DRUM_KITS) + (int32_t)DRUM_KITS) % (int32_t)DRUM_KITS;
    return DS_KIT_NAV[clamp(pos, 0, (int32_t)DRUM_KITS - 1)];
}

static struct {
    voice_t v[NDRUM];
    uint32_t age;
    int16_t set;                 /* SMP_SETS index of "PERC" (GM map), -1 = none */
    int16_t beat;                /* Jangada: GLO > KIT BEAT, the factory beat last chosen (0 = --) */
    int32_t tail;                /* declick: the last output of cut voices, decaying */
    int32_t peak;                /* largest |output| since the UI last looked (TRACKS meter) */
    uint8_t kick;                /* Jangada: a kick (GM 35 / 36) since the master's DUCK looked (fx.c duck_block) */
    uint8_t synth[NDRUM];        /* Jangada: the voice plays a synthesised kit (ds[]) */
    dsv_t ds[NDRUM];
} drums = {.set = -2};
static int32_t ds_buf[CTL];

static int32_t drum_set(void)
{
    uint32_t i;
    if (drums.set == -2) {
        drums.set = -1;
        for (i = 0; i < SMP_NSETS; i++)
            if (str_eq(SMP_SETS[i].name, "PERC"))
                drums.set = (int16_t)i;
    }
    return drums.set;
}

/* a free voice, else the oldest (its last value fades: no click); the hi-hat choke first */
static voice_t *drum_voice(uint32_t note)
{
    voice_t *v = &drums.v[0];
    uint32_t i;
    if (note == 35u || note == 36u)
        drums.kick = 1;
    if (note == 42u || note == 44u)                 /* hi-hat choke */
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active && drums.v[i].note == 46u) {
                drums.v[i].active = 0;
                drums.tail += drums.v[i].s[7];
            }
    for (i = 0; i < NDRUM; i++) {
        if (!drums.v[i].active)
            return &drums.v[i];
        if (drums.v[i].age < v->age)
            v = &drums.v[i];
    }
    drums.tail += v->s[7];
    return v;
}

static void drum_on(uint32_t note, uint32_t vel)
{
    int32_t si = drum_set();
    const smp_set_t *set;
    voice_t *v = &drums.v[0];
    uint32_t i, zi = 0xFFFFu, kit = drum_kit();
    if (kit) {                                      /* Jangada: a synthesised kit */
        v = drum_voice(note);
        i = (uint32_t)(v - drums.v);
        v->note = (uint8_t)note;
        v->vel = (uint8_t)vel;
        v->active = 1;
        v->s[7] = 0;
        v->age = ++drums.age;
        drums.synth[i] = 1;
        ds_on(&drums.ds[i], &DS_KITS[kit - 1u], note, vel);
        return;
    }
    if (si < 0)
        return;
    set = &SMP_SETS[si];
    zi = smp_zone_pick(set, note);
    if (zi == 0xFFFFu)
        return;
    if (note == 35u || note == 36u)
        drums.kick = 1;
    if (note == 42u || note == 44u)                 /* hi-hat choke */
        for (i = 0; i < NDRUM; i++)
            if (drums.v[i].active && drums.v[i].note == 46u) {
                drums.v[i].active = 0;
                drums.tail += drums.v[i].s[7];          /* fade what it was playing, not a step */
            }
    for (i = 0; i < NDRUM; i++) {                   /* free voice, else the oldest */
        if (!drums.v[i].active) {
            v = &drums.v[i];
            break;
        }
        if (drums.v[i].age < v->age)
            v = &drums.v[i];
    }
    if (v->active)
        drums.tail += v->s[7];                      /* stolen voice: fade its last value */
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->active = 1;
    v->s[7] = 0;
    v->age = ++drums.age;
    v->s[4] = (int32_t)zi;
    v->ph[0] = v->ph[1] = 0;
    v->s[0] = v->s[1] = v->s[2] = 0;
    v->s[3] = sample_next(&SMP_ZONES[zi], v, 0);
    v->s[5] = (int32_t)((pow2_q16((int32_t)note * 16 - SMP_ZONES[zi].root16) >> 8) * (SMP_ZONES[zi].rate >> 8));
    drums.synth[v - drums.v] = 0;
}

/* adds the drums into the dry mix and the reverb send; mono != 0: into mono instead, before the
 * pan and the send (the SLICER, slicer.c slicer_drums, does those after it) */
static inline void drums_mix(int32_t *ml, int32_t *mr, int32_t *rev, int32_t *mono, uint32_t n)
{
    uint32_t k, i;
    int32_t lvl = song.g[G_DRLVL] * 200, send = thr_send(song.g[G_DRREV] * 258, 0), pk = drums.peak;
    int32_t pan = trk[TRK_DRUM].p[P_PAN], gl = 4096 - (pan > 0 ? pan * 64 : 0), gr = 4096 + (pan < 0 ? pan * 64 : 0);
    for (i = 0; i < n && drums.tail; i++) {         /* declick tail, ~0.4 ms */
        if (mono) {
            mono[i] += drums.tail;
        } else {
            ml[i] += drums.tail;
            mr[i] += drums.tail;
        }
        drums.tail -= drums.tail / 16 + (drums.tail > 0 ? 1 : drums.tail < 0 ? -1 : 0);
    }
    for (k = 0; k < NDRUM; k++) {                   /* Jangada: synthesised voices (they peak ~1.75 x the GM
                                                     * samples, as SLOOP measured: brought to the same level) */
        voice_t *v = &drums.v[k];
        uint32_t m = n < CTL ? n : CTL;
        int32_t gs = (lvl * 4681) >> 13;
        if (!v->active || !drums.synth[k])
            continue;
        if (!ds_render(&drums.ds[k], ds_buf, m))
            v->active = 0;
        for (i = 0; i < m; i++) {
            int32_t s = mulq15(ds_buf[i], gs);
            v->s[7] = s;
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                continue;
            }
            ml[i] += (s * gl) >> 12;
            mr[i] += (s * gr) >> 12;
            if (send)
                rev[i] += mulq15(s, send);
        }
    }
    for (k = 0; k < NDRUM; k++) {
        voice_t *v = &drums.v[k];
        const smp_zone_t *z = &SMP_ZONES[v->s[4]];
        uint32_t frac = v->ph[1], stepq = (uint32_t)v->s[5];   /* Q16 source samples per output (drum_on) */
        int32_t g;
        if (!v->active || drums.synth[k])
            continue;
        g = mulq15(lvl, v->vel * 258);
        for (i = 0; i < n; i++) {
            int32_t s;
            frac += stepq;
            while (frac >= 65536u) {
                frac -= 65536u;
                v->s[2] = v->s[3];
                if (v->ph[0] >= z->n) {
                    v->active = 0;
                    break;
                }
                v->s[3] = sample_next(z, v, 0);
            }
            if (!v->active)
                break;
            s = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
            s = mulq15(s, g);
            v->s[7] = s;
            if (s > pk || -s > pk)
                pk = s < 0 ? -s : s;
            if (mono) {
                mono[i] += s;
                continue;
            }
            ml[i] += (s * gl) >> 12;
            mr[i] += (s * gr) >> 12;
            if (send)
                rev[i] += mulq15(s, send);
        }
        v->ph[1] = frac;
    }
    drums.peak = pk;
}
static void drums_render(int32_t *ml, int32_t *mr, int32_t *rev, uint32_t n) { drums_mix(ml, mr, rev, 0, n); }
static void drums_render_mono(int32_t *mono, uint32_t n) { drums_mix(0, 0, 0, mono, n); }
