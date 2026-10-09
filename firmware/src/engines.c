/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Engine table: order = PRESETS browsing order (and the engine numbers of the editor protocol). */
#include "dsp.c"
/* Jangada 0.9 (after SLOOP 2.5): the big per-track state of GRAIN, FM6 and PHYS shares one arena per track (a
 * track plays one engine at a time: on a switch the old engine fades out, its voices end, then the new one starts,
 * voice.c engine_block). The first engine to ask for it after another one gets it cleared, as at power-on */
static void *eng_arena_of(const track_t *t, uint32_t eng);
#include "eng_analog.c"
#include "eng_digital.c"
#include "eng_phase.c"
#include "eng_lofi.c"
#include "eng_sample.c"
#include "eng_formant.c"
#include "eng_trio.c"
#include "eng_drawbar.c"
#include "eng_grain.c"
#include "eng_fm6.c"          /* Jangada: 6-operator FM (msfa, after Felucca 1.0) */
#include "eng_noise.c"        /* Jangada 0.9: NOISE (after Felucca 1.0 / SLOOP 2.5) */
#include "eng_phys.c"         /* Jangada 0.9: PHYS (after Felucca 1.0 / SLOOP 2.5; DaisySP, Rings: MIT) */
#include "eng_robo.c"         /* Jangada 0.9.1: ROBO, the machine voices (issue #4) */

typedef union {
    gr_part_t grain;
    fm6_note_t fm6[FM6_POLY];
    uint32_t phys[PHYS_ARENA / 4u];      /* eng_phys.c: two SYMP slots or three small ones */
    robo_part_t robo;                    /* eng_robo.c: GENDY's points, WALSH's wave */
} eng_arena_t;
static eng_arena_t eng_arena[NTRK] __attribute__((section(".pool")));   /* (track 4 too: G_T4 SYNTH) */
static uint8_t eng_arena_own[NTRK];      /* the engine + 1 whose state is in it, 0 = none */
static void *eng_arena_of(const track_t *t, uint32_t eng)
{
    uint32_t p = (uint32_t)(t - trk);
    if (p >= NTRK)
        return 0;
    if (eng_arena_own[p] != eng + 1u) {
        memset(&eng_arena[p], 0, sizeof eng_arena[p]);
        eng_arena_own[p] = (uint8_t)(eng + 1u);
    }
    return &eng_arena[p];
}
#if FELUCCA_SLICE
#include "eng_slice.c"
#endif

static const engine_t *const ENGINES[NENGINES] = {&ENG_ANALOG, &ENG_DIGITAL, &ENG_PHASE, &ENG_LOFI, &ENG_SAMPLE,
                                                    &ENG_FORMANT, &ENG_TRIO, &ENG_DRAWBAR, &ENG_GRAIN, &ENG_FM6,
                                                    &ENG_NOISE, &ENG_PHYS, &ENG_ROBO,
#if FELUCCA_SLICE
                                                    &ENG_SLICE,
#endif
};
_Static_assert(ENGI_GRAIN == 8u && ENGI_FM6 == 9u && ENGI_NOISE == 10u && ENGI_PHYS == 11u && ENGI_ROBO == 12u, "ENGINES[] order");
