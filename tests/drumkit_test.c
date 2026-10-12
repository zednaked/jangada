/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the synthesised drum kits (drum_synth.c, after SLOOP) on the drum track: every kit plays
 * every GM note, stays bounded, ends (its voice comes free), is about as loud as the GM samples, and
 * costs what it should. Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "instr.h"
#ifndef VERBOSE_KITS
#define VERBOSE_KITS 0
#endif

static int32_t out[2 * CTL];
/* ui.c PATTERNS 12 (BAIAO bass; ui.c is not in the host build) */
static const uint8_t PAT_BAIAO[16] = {36, 0, 0, 43, 0, 0, 46, 0, 36, 0, 0, 43, 0, 42, 43, 46};
static const uint8_t PAT_BAIAOF[16] = {1, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0};

static void setup(uint32_t kit)
{
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.g[G_KIT] = (int16_t)kit;
    song.master_q12 = 4096;
}

/* one hit of note: its peak and energy over 2 s, and whether every voice came free */
static void hit(uint32_t note, int32_t *peak, double *energy, int *freed)
{
    uint32_t b, i, k;
    int32_t pk = 0;
    double e = 0;
    drum_on(note, 110);
    for (b = 0; b < 2u * FS / CTL; b++) {
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = out[i] < 0 ? -out[i] : out[i];
            if (a > pk)
                pk = a;
            e += (double)out[i] * out[i];
        }
    }
    for (k = 0, *freed = 1; k < NDRUM; k++)
        if (drums.v[k].active)
            *freed = 0;
    *peak = pk;
    *energy = e;
}

/* Jangada: beat b (DS_BEATS) into the drum track, as ui.c load_beat does */
static void put_beat(uint32_t b)
{
    uint32_t i, k;
    for (i = 0; i < 16u; i++) {
        step_t *s = &TDRUM->step[i];
        s->n = 0;
        for (k = 0; k < 4u; k++)
            s->note[k] = 0;
        for (k = 0; k < 4u; k++)
            if (DS_BEATS[b].note[i][k])
                s->note[s->n++] = DS_BEATS[b].note[i][k];
        s->time = s->n ? ST_NOTE : ST_REST;
        s->flags = DS_BEATS[b].flags[i];
        s->vel = DS_BEATS[b].vel[i];
    }
    memset(&TDRUM->x, 0, sizeof TDRUM->x);
    for (i = 0; i < 16u; i++)
        step_cond_set(TDRUM, i, DS_BEATS[b].cond >> (2u * i) & 3u);
    TDRUM->p[P_SLEN] = 16;
}

static uint32_t kit_of(const char *name)
{
    uint32_t k;
    for (k = 1; k < DRUM_KITS; k++)
        if (!strcmp(DRUM_KIT_NAMES[k], name))
            return k;
    return 0;
}

/* Jangada: a beat played by the sequencer (parts silent unless set up), secs seconds; its peak; a WAV if w */
static int32_t play(uint32_t secs, FILE *w)
{
    uint32_t f, i, frames = secs * FS;
    int32_t pk = 0;
    transport_req = 1;
    if (w)
        wav_hdr(w, frames);
    for (f = 0; f < frames; f += CTL) {
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++)
            if ((out[i] < 0 ? -out[i] : out[i]) > pk)
                pk = out[i] < 0 ? -out[i] : out[i];
        if (w)
            for (i = 0; i < CTL; i++)
                wav_put(w, out[2 * i], out[2 * i + 1]);
    }
    transport_req = 2;
    for (f = 0; f < FS / 2u; f += CTL)
        mix_block(out, CTL);
    return pk;
}

/* KIT_DEMOS=dir: the new kits' beats (and two with the Nordeste presets), ~8 s each, and every kit's 16
 * lanes one after another, as WAVs for listening (tools: ffmpeg / lame to MP3) */
static void demos(const char *dir)
{
    static const struct { const char *beat; uint16_t bpm; uint8_t parts; } D[] = {
        {"MARACATU", 96, 0}, {"BAIAO", 112, 0}, {"COCO", 104, 0}, {"GRIND", 92, 0}, {"ANVIL", 84, 0},
        {"ENGINE", 116, 0}, {"FRAGILE", 76, 0}, {"BAIAO", 112, 1}, {"COCO", 104, 2}};
    static const uint8_t LANE_GM[16] = {36, 38, 39, 42, 46, 43, 48, 49, 51, 70, 63, 37, 56, 75, 35, 40};
    char path[512];
    uint32_t d, b, i;
    for (d = 0; d < sizeof D / sizeof D[0]; d++) {
        FILE *w;
        for (b = 0; b < DS_NBEATS && strcmp(DS_BEATS[b].name, D[d].beat); b++)
            ;
        setup(0);
        for (i = 0; i < DRUM_KITS; i++)
            if (DS_KIT_BEAT[i] == b + 1u)
                break;
        song.g[G_KIT] = (int16_t)(i < DRUM_KITS ? i : kit_of("MANGUE"));
        song.g[G_BPM] = D[d].bpm;
        put_beat(b);
        for (i = 0; i < NPART; i++)
            trk[i].p[P_LEVEL] = 0;
        if (D[d].parts) {                           /* BAIAO BASS + SANFONA, or RABECA */
            uint32_t e, pi, k;
            static const uint8_t CH[2][3] = {{48, 55, 64}, {60, 67, 0}};
            track_t *t1 = &trk[0], *t2 = &trk[1];
            if (D[d].parts & 1u) {
                for (pi = 0; pi < ENGINES[0]->npresets && strcmp(ENGINES[0]->presets[pi].name, "BAIAO BASS"); pi++)
                    ;
                host_preset(t1, 0, pi);
                t1->p[P_LEVEL] = TP[P_LEVEL].def;
                for (k = 0; k < 16u; k++) {
                    uint8_t n = PAT_BAIAO[k];
                    put_step(t1, k, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, PAT_BAIAOF[k]);
                }
                t1->p[P_SLEN] = 16;
            }
            for (e = 0; e < NENGINES; e++)
                for (pi = 0; pi < ENGINES[e]->npresets; pi++)
                    if (!strcmp(ENGINES[e]->presets[pi].name, D[d].parts == 1u ? "SANFONA" : "RABECA"))
                        goto found;
        found:
            if (e < NENGINES) {
                const uint8_t *ch = CH[D[d].parts == 1u ? 0 : 1];
                uint32_t nch = ch[2] ? 3u : 2u;
                host_preset(t2, e, pi);
                t2->p[P_AMODE] = 0;                 /* the drone as a held chord of the sequencer */
                t2->p[P_LEVEL] = TP[P_LEVEL].def - 8;
                t2->p[P_SLEN] = 16;
                t2->p[P_SGATE] = 127;
                for (k = 0; k < 16u; k++)
                    put_step(t2, k, k ? 0u : nch, ch, k ? ST_TIE : ST_NOTE, 0);
            }
        }
        snprintf(path, sizeof path, "%s/%02u-%s%s.wav", dir, d + 1u, D[d].beat,
                 D[d].parts == 1u ? "-baixo-sanfona" : D[d].parts == 2u ? "-rabeca" : "");
        if ((w = fopen(path, "wb"))) {
            play(8, w);
            fclose(w);
            printf("demo %s\n", path);
        }
    }
    for (i = 0; i < 5u; i++) {                      /* the kits' 16 sounds, 0.5 s apart */
        static const char *const K[5] = {"RUST", "FORGE", "PISTON", "HURT", "MANGUE"};
        uint32_t l, f;
        FILE *w;
        setup(kit_of(K[i]));
        snprintf(path, sizeof path, "%s/sons-%s.wav", dir, K[i]);
        if (!(w = fopen(path, "wb")))
            continue;
        wav_hdr(w, 16u * FS / 2u);
        for (l = 0; l < 16u; l++) {
            drum_on(LANE_GM[l], 110);
            for (f = 0; f < FS / 2u; f += CTL) {
                uint32_t j;
                mix_block(out, CTL);
                for (j = 0; j < CTL; j++)
                    wav_put(w, out[2 * j], out[2 * j + 1]);
            }
        }
        fclose(w);
        printf("demo %s\n", path);
    }
}

int main(void)
{
    static const uint8_t NOTES[] = {35, 36, 37, 38, 39, 40, 41, 42, 44, 45, 46, 47, 48, 49, 50, 51, 53,
                                    54, 56, 57, 62, 63, 64, 69, 70, 75, 76, 81};
    uint32_t kit, n, fails = 0;
    double gm_kick = 0, gm_snare = 0;
    for (kit = 0; kit < DRUM_KITS; kit++) {
        int32_t worst = 0;
        for (n = 0; n < sizeof NOTES; n++) {
            int32_t pk;
            double e;
            int freed;
            setup(kit);
            hit(NOTES[n], &pk, &e, &freed);
            if (pk > worst)
                worst = pk;
            if (pk >= 32767) {
                printf("kit %s note %u clips (%d)\n", DRUM_KIT_NAMES[kit], NOTES[n], pk);
                fails++;
            }
            if (!freed) {
                printf("kit %s note %u: a voice still sounds after 2 s\n", DRUM_KIT_NAMES[kit], NOTES[n]);
                fails++;
            }
            if (kit == 0u && NOTES[n] == 36u)
                gm_kick = e;
            if (kit == 0u && NOTES[n] == 38u)
                gm_snare = e;
            if (kit && (NOTES[n] == 36u || NOTES[n] == 38u)) {   /* kick and snare near the GM kit's */
                double ref = NOTES[n] == 36u ? gm_kick : gm_snare, db = 10.0 * log10(e / ref);
                if (db < -12.0 || db > 12.0) {
                    printf("kit %s note %u: %+.1f dB from the GM kit\n", DRUM_KIT_NAMES[kit], NOTES[n], db);
                    fails++;
                }
            }
        }
        if (VERBOSE_KITS)
            printf("kit %-8s peak %5d\n", DRUM_KIT_NAMES[kit], worst);
    }
    printf("%-46s %s\n", "kits: every kit, every GM note: bounded, ends", fails ? "FAIL" : "ok");
    {   /* Jangada: the browser's order is every kit once, GM first, then Jangada's own (appended to G_KIT) */
        uint32_t seen[DRUM_KITS] = {0}, k, bad = 0;
        static const char *const OWN[5] = {"RUST", "FORGE", "PISTON", "HURT", "MANGUE"};
        for (k = 0; k < DRUM_KITS; k++)
            seen[DS_KIT_NAV[k]]++;
        for (k = 0; k < DRUM_KITS; k++)
            bad += seen[k] != 1u;
        bad += DS_KIT_NAV[0] != 0u || DRUM_KITS != 38u || strcmp(DRUM_KIT_NAMES[32], "JAZZ");   /* old indices kept */
        for (k = 0; k < 5u; k++)
            bad += strcmp(DRUM_KIT_NAMES[DS_KIT_NAV[k + 1u]], OWN[k]) != 0 || !DS_KIT_BEAT[DS_KIT_NAV[k + 1u]];
        bad += drum_kit_step(0, -1, 1) != DS_KIT_NAV[DRUM_KITS - 1u] || drum_kit_step(DS_KIT_NAV[DRUM_KITS - 1u], 1, 1) != 0u;
        bad += drum_kit_step(0, -1, 0) != 0u || drum_kit_step(0, 2, 0) != DS_KIT_NAV[2];
        printf("%-46s %s\n", "kits: browser order (Jangada's first), indices kept", bad ? "FAIL" : "ok");
        fails += bad;
    }
    {   /* Jangada: every factory beat on its kit and on GM: up to 4 hits a step, every note a sound, no clip */
        uint32_t b, i, k, bad = 0;
        for (b = 0; b < DS_NBEATS; b++)
            for (k = 0; k < 2u; k++) {
                uint32_t kit = 0;
                int32_t pk;
                for (i = 0; i < DRUM_KITS && k; i++)
                    if (DS_KIT_BEAT[i] == b + 1u)
                        kit = i;
                if (k && !kit)
                    kit = kit_of("MANGUE");
                setup(kit);
                put_beat(b);
                for (i = 0; i < 16u; i++)
                    if (TDRUM->step[i].n > 4u || (TDRUM->step[i].n && !TDRUM->step[i].vel))
                        bad++;
                for (i = 0; i < NPART; i++)
                    trk[i].p[P_LEVEL] = 0;
                pk = play(4, 0);
                if (pk >= 32700 || pk < 2000) {
                    printf("beat %s on %s: peak %d\n", DS_BEATS[b].name, DRUM_KIT_NAMES[kit], pk);
                    bad++;
                }
                fill_held = 1;                      /* its virada (FILL held): plays, differs, no clip */
                pk = play(4, 0);
                fill_held = 0;
                if (pk >= 32700 || pk < 2000 || !DS_BEATS[b].cond) {
                    printf("beat %s on %s, virada: peak %d\n", DS_BEATS[b].name, DRUM_KIT_NAMES[kit], pk);
                    bad++;
                }
            }
        printf("%-46s %s\n", "beats: every beat and its virada, kit and GM", bad ? "FAIL" : "ok");
        fails += bad;
    }
    if (getenv("KIT_DEMOS"))
        demos(getenv("KIT_DEMOS"));
    {   /* cost: six synthesised voices at once against six GM samples */
        uint64_t i0, i1, i2;
        uint32_t b;
        setup(0);
        for (n = 0; n < NDRUM; n++)
            drum_on(36u + n * 2u, 110);
        i0 = instr_now();
        for (b = 0; b < 100u; b++)
            mix_block(out, CTL);
        i1 = instr_now();
        setup(1);
        for (n = 0; n < NDRUM; n++)
            drum_on(36u + n * 2u, 110);
        for (b = 0; b < 100u; b++)
            mix_block(out, CTL);
        i2 = instr_now();
        if (i0)
            printf("%-46s %.0f / sample (GM %.0f)\n", "cost: 6 synthesised drum voices (808)", (double)(i2 - i1) / (100.0 * CTL),
                   (double)(i1 - i0) / (100.0 * CTL));
    }
    if (fails)
        printf("KITS: %u FAILED\n", fails);
    return fails != 0;
}
