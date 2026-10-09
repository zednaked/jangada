/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca core types: tracks, voices, engines, parameters.
 * Four tracks: tracks 1..3 are synth parts (each its own engine, preset, parameters,
 * voices and 64-step pattern), track 4 is the GM drum part (drums.c; its own voices,
 * pattern and the pattern parameters of its track_t). The parts share one budget of
 * NVOICE sounding voices (voice.c). */
#include <stdint.h>
#define NVOICE 8                 /* voices per part, and the budget shared by all parts */
#define NPOLY 8
#define NPART 3                  /* synth parts: tracks 1..3 */
#define NTRK 4                   /* + the drum track */
#define TRK_DRUM 3
enum { V_POLY, V_MONO, V_LEGATO, V_UNISON };   /* P_VOICE */
#define NSTEP 64
#define HALF_FRAMES 256          /* I2S half buffer: 5.8 ms at 44.1 kHz */
#ifndef FELUCCA_SLICE
#define FELUCCA_SLICE 0          /* the SLICE engine (eng_slice.c): kept in the tree, not built by default */
#endif
#define NENGINES (13 + FELUCCA_SLICE)  /* SLICE, when built, comes last: the other engines keep their numbers
                                        * (Jangada: FM6 is 9) */
#define ENGI_GRAIN 8u            /* engines.c ENGINES[]: the ones with state of their own in the engine arena (0.9) */
#define ENGI_NOISE 10u           /* Jangada 0.9: NOISE and PHYS, appended after FM6 (9): the stores keep the numbers */
#define ENGI_PHYS 11u
#define ENGI_ROBO 12u           /* Jangada 0.9.1: ROBO (VOSIM, GENDY, WALSH; issue #4) */
#define UP_SLOTS 32u             /* user presets (upreset.c) */

/* ------------------------------------------------------- parameters --- */
enum {
    F_INT, F_PCT, F_BIPCT, F_TIME, F_LFOHZ, F_CUTOFF, F_DB, F_SEMI, F_ENUM, F_BPM, F_NOTE,
    F_ONOFF, F_OCT, F_STEPS, F_FILT
};

typedef struct {
    const char *label;
    uint8_t fmt;
    int16_t min, max, def;
    const char *const *names;   /* F_ENUM */
    const char *unit;           /* F_INT / F_ENUM optional unit */
} param_desc_t;

enum {                          /* per-track parameters */
    P_LEVEL,
    P_ATK, P_DEC, P_SUS, P_REL,
    P_ED_FLT, P_ED_PIT, P_ED_SHP, P_ED_FX,     /* P_ED_FX: unused, kept for the formats / protocol */
    P_LRATE, P_LWAVE, P_LPHASE, P_LFADE,
    P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP,
    P_AMODE, P_ARATE, P_AOCT, P_AGATE,
    P_ASWING, P_APROB, P_AHOLD, P_AORDER,
    P_ROOT, P_SCALE, P_QUANT, P_TRANS,
    P_SLEN, P_SDIV, P_SSWING, P_SGATE,
    P_DIST, P_CHOR, P_DLY, P_REV,
    P_VOICE, P_GLIDE, P_PAN, P_MUTE,
    P_GLMODE, P_PRIO, P_ALLOC, P_DETUNE,
    P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH,      /* SLICER insert (slicer.c) */
    /* Jangada: the modulation matrix (mod.c), NMOD slots of SRC DST AMT */
    P_M1SRC, P_M1DST, P_M1AMT, P_M2SRC, P_M2DST, P_M2AMT,
    P_M3SRC, P_M3DST, P_M3AMT, P_M4SRC, P_M4DST, P_M4AMT,
    P_CHORD,                                   /* Jangada (after SLOOP): one key plays a chord of the scale (seq.c) */
    P_DTYPE, P_DRING,                          /* Jangada GRIT: the DIST type (SOFT FUZZ FOLD CRUSH RING) and the RING
                                                * carrier (fx.c track_dist) */
    /* the engine's own parameters, NEDIT of them (Felucca had 8). Saved data does not depend
     * on these positions: projects and user presets store stable keys (P_KEY, params.c) */
    P_E0, P_E1, P_E2, P_E3, P_E4, P_E5, P_E6, P_E7,
    P_E8, P_E9, P_E10, P_E11, P_E12, P_E13, P_E14, P_E15,
    /* Jangada DRONES (drone.c): evolution (a slow random walk on the engine's natural targets), TENSION
     * (a macro that opens the sound) and its RAMP (bars to reach it). After the engine's own: P_E0 keeps
     * its place (saved data use the keys anyway) */
    P_EVOL, P_TENS, P_TRAMP,
    P_TFLT,                                    /* Jangada 0.7 (after SLOOP 2.4): the track's filter, < 0 a low-pass
                                                * closing, > 0 a high-pass opening, 0 off (fx.c djf_block) */
    P_STRUM,                                   /* Jangada 0.7 (after SLOOP 2.4): a chord's notes one after the
                                                * other, ms each (> 0 low to high, < 0 high to low; seq.c) */
    P_VLEAD,                                   /* Jangada 0.7 (after SLOOP 2.4): CHORD, each chord voiced nearest
                                                * the last (seq.c chord_vlead) */
    P_ITYPE, P_IA, P_IB, P_IC, P_IMIX,         /* Jangada 0.9 (after Felucca 1.5): the INSERT after DIST, its TYPE,
                                                * three values and MIX (fx.c track_insert) */
    P_COUNT
};
#define NEDIT 16                 /* engine parameters P_E0.. */
#define NMOD 4                   /* modulation slots */

enum {                          /* global parameters */
    G_BPM, G_SWING, G_CLOCK, G_TUNE,
    G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX,
    G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH,
    G_MIDI, G_SYNC, G_ROUTE, G_INFO,
    G_SLOT, G_NAME, G_LOAD, G_SAVE,
    G_ENGSEL, G_ENGGO,          /* no page (the ENGINE page is gone); a SET of G_ENGSEL switches the engine (editor) */
    G_CLRSEQ, G_INITSND,
    G_DRCH, G_DRLVL, G_DRREV,
    G_T4,                       /* Jangada: track 4 is the GM drum track (0) or a fourth synth part (1) */
    G_DUST, G_DUCK, G_FILT,     /* Jangada (after SLOOP): the master bus, fx.c: an old sampler and a record,
                                 * the kick ducking the synth parts, the DJ filter (< 0 LP, > 0 HP) */
    G_KIT,                      /* Jangada: the drum track's kit: 0 GM (samples), 1.. synthesised (drum_synth.c) */
    G_RTYPE,                    /* Jangada: the reverb model: 0 ROOM, 1 SPRING (Felucca 1.0), 2 PLATE (SLOOP's FDN) */
    /* Jangada GRIT (fx.c): the master through a worn tape (saturation, wow, flutter, dull highs) and the
     * hum of an analog recording (60 Hz and its odd harmonics, hiss, a rare crackle) */
    G_TAPE, G_HUM,
    /* Jangada 0.8 (issue #2): four macro knobs for playing live, sources MAC1..MAC4 of every part's matrix
     * (mod.c): point slots of one part or of all at a MAC and one knob moves them all */
    G_MAC1, G_MAC2, G_MAC3, G_MAC4,
    G_COUNT
};
/* the globals a stored project keeps: all before the macros. The macros are where the hands are, not part of
 * the song (a load leaves them as they are); and the worst-case project fills its flash object to 2 bytes */
#define G_STORED G_MAC1

/* ----------------------------------------------------------- voices --- */
typedef struct {
    uint8_t note, vel, gate, active;
    uint8_t stage;               /* env: 0 off, 1 attack, 2 decay/sustain, 3 release */
    int32_t env;                 /* Q24 */
    int32_t env_out;             /* last control-rate amplitude, Q15 */
    int32_t pitch16, pitch_cur;  /* 1/16 semitone, with glide */
    int32_t gstep;               /* glide TIME mode: 1/16 st per control tick, 0 = RATE mode */
    int32_t fine;                /* unison detune: phase increment * (1 + fine / 4096) */
    uint32_t ph[3];
    int32_t s[8];                /* engine state (filters, envs) */
    uint32_t age;
    int16_t rnd;                 /* Jangada: the matrix's RND source, fixed at note-on (Q15) */
} voice_t;

typedef struct {                 /* per-voice control-rate modulation, computed in voice.c */
    uint32_t inc;                /* phase increment of the base pitch */
    int32_t pitch16;
    int32_t amp0, amp1;          /* Q15 ramp over the block */
    int32_t cutoff;              /* 0..127 << 8 */
    int32_t shape;               /* 0..127 << 8 */
    int32_t envq15;              /* env value (for engines that use it as a mod source) */
    int32_t fine;                /* Jangada: below 1/16 semitone (unison detune + tune), 1/4096: FM6 */
} vmod_t;

typedef struct {
    const char *name;
    int8_t e[8];                 /* P_E0..P_E7 (signed: an interval below the note; every value fits) */
    uint8_t env[4];              /* ATK DEC SUS REL */
    int8_t fenv;                 /* ENV -> FILTER amount (-64..63) */
    uint8_t mono;                /* 1 = MONO (bass / lead), 0 = POLY */
    /* the rest of the patch; each value is stored + 1, 0 = the default */
    uint8_t fx[4];               /* DIST, CHORUS, DELAY, REVERB sends */
    uint8_t arp[4];              /* MODE, RATE, OCT, GATE */
    uint8_t pat;                 /* sequence pattern (PATTERNS[pat - 1]), loaded only into an empty sequencer */
    int16_t x[NEDIT - 8];        /* Jangada: P_E8.. stored + 1, 0 = the engine's default; last, so the
                                  * positional initializers above stay as they are (.x = {..}) */
    const int16_t (*set)[2];     /* Jangada: any other parameters, {P_*, value} .. {-1}: SET(..) */
    uint8_t cat;                 /* Jangada 0.8.1 (issue #2): the category, PC_* (CAT(..)); the PRESETS page filters by it */
} preset_t;
/* preset categories (ui.c PC_NAMES; 0 = none: an FM6 bank voice, a user preset) */
enum { PC_NONE, PC_BASS, PC_LEAD, PC_PAD, PC_KEYS, PC_PLUCK, PC_PERC, PC_DRONE, PC_FX, PC_COUNT };
#define CAT(c) .cat = PC_##c
#define FX(d, c, dl, r) .fx = {(d) + 1, (c) + 1, (dl) + 1, (r) + 1}
#define ARP(m, rt, o, g) .arp = {(m) + 1, (rt) + 1, (o) + 1, (g) + 1}
#define PAT(n) .pat = (n)
/* Jangada: a preset may set any track parameter (LFO, matrix, HOLD, ..): SET({P_LRATE, 8}, ..) */
#define SET(...) .set = (const int16_t[][2]){__VA_ARGS__, {-1, 0}}

struct track;
typedef struct {
    const char *name;            /* "VA" */
    const char *page_title[2];
    param_desc_t edit[NEDIT];    /* P_E0..; no label = not used by this engine */
    const preset_t *presets;
    uint8_t npresets;
    int8_t fil_page;             /* EDIT page that holds the filter, -1 = none */
    void (*note_on)(struct track *t, voice_t *v);
    void (*render)(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m);
    uint16_t color;              /* accent colour of the engine (RGB565) */
    uint8_t macro[4];            /* HOME: the four parameters on KNOB 1..4 */
    uint8_t poly;                /* voice cap for POLY and UNISON, 0 = NVOICE */
    /* optional (0 = none): the voice amplitude instead of the ADSR curve, once per control tick;
     * gets the ADSR value (Q15, env_tick already ran: it still gates the voice), returns Q15 */
    int32_t (*amp)(struct track *t, voice_t *v, int32_t adsr);
    /* optional: a mode-dependent descriptor of EDIT k (the same range and default as edit[k],
     * another label / value names), 0 = edit[k] */
    const param_desc_t *(*desc)(const struct track *t, uint32_t k);
    /* optional: once per block and part, before its voices (also with no voice sounding) */
    void (*block)(struct track *t);
    /* Jangada (after Felucca 1.0): the engine's own envelopes are the amplitude (no ADSR, no velocity
     * scaling) and done() ends the voice (FM6) */
    uint8_t ownenv;
    int (*done)(struct track *t, voice_t *v);
} engine_t;

/* ------------------------------------------------------------ track --- */
enum { ST_NOTE, ST_TIE, ST_REST };
#define SF_ACCENT 1u
#define SF_SLIDE 2u
/* Jangada: ratchet and chance in the free bits (7-bit safe for SysEx; bit 2 is the TIE
 * marker of user-preset patterns, so it stays free). Old projects have 0: x1, 100 %. */
#define SF_RATCH_SH 3u
#define SF_RATCH (3u << SF_RATCH_SH)       /* 0..3: x1 x2 x3 x4 hits in the step */
#define SF_CHANCE_SH 5u
#define SF_CHANCE (3u << SF_CHANCE_SH)     /* 0..3: 100 75 50 25 % */
#define SF_STEP (SF_ACCENT | SF_SLIDE | SF_RATCH | SF_CHANCE)
typedef struct {                 /* acid-style step: up to 4 notes (POLY), time, accent, slide */
    uint8_t note[4];
    uint8_t n;                   /* notes in use, 0 = empty */
    uint8_t time;                /* ST_NOTE / ST_TIE / ST_REST */
    uint8_t flags;               /* SF_ACCENT | SF_SLIDE */
    uint8_t vel;
} step_t;

/* Jangada (after SLOOP 2.4, isod89): micro timing, step conditions (fills) and parameter locks. sx[] holds
 * one byte a step: bits 0..5 the nudge (6-bit two's complement, MICRO_MIN..MICRO_MAX in 1/64 of the step:
 * - early, + late), bits 6..7 the condition (FC_*: plays always, only during a fill, never during one).
 * A lock: on step `step` the track's p[param] is `val` (every lockable parameter fits an int8: p_lockable) */
#define MICRO_MIN (-32)
#define MICRO_MAX 31
#define SX_MICRO 0x3Fu
#define SX_COND_SH 6u
enum { FC_NORM, FC_FILL, FC_NOFILL };
#define NLOCK 12                 /* locks a track (several may share a step: other parameters) */
#define LOCK_FREE 0u              /* plock_t.step of a free slot: it holds the step + 1 (zeroed = no lock) */
typedef struct { uint8_t step, param; int8_t val; } plock_t;
typedef struct { uint8_t sx[NSTEP]; plock_t lock[NLOCK]; } seqx_t;

typedef struct track {
    int16_t p[P_COUNT];
    uint8_t engine, preset;      /* engine: what the audio ISR renders */
    uint8_t eng_req;             /* engine the UI asked for (the ISR switches at a block start) */
    uint8_t user;                /* user preset slot + 1 the sound came from (UI), 0 = none */
    voice_t v[NVOICE];
    /* LFO */
    uint32_t lfo_ph;
    int32_t lfo_val;             /* Q15 */
    int32_t lfo_fade;            /* Q15 ramp after note-on */
    uint32_t lfo_rnd;
    /* keyboard / arp input: held notes in press order */
    uint8_t held[16];
    uint8_t nheld;
    uint8_t arp_phys;            /* keys physically held for the arp */
    uint8_t latched;             /* HOLD: keep notes after release */
    /* arp runtime */
    uint32_t arp_pos;            /* q8 samples into the current arp step */
    uint32_t arp_idx;
    uint8_t arp_note;            /* sounding arp note, 0 = none */
    uint8_t arp_chord[NVOICE];   /* RPT: the sounding chord (arp_nch notes) */
    uint8_t arp_nch;
    uint32_t arp_off;            /* q8 sample time of its note-off */
    /* sequencer */
    step_t step[NSTEP];
    uint32_t seq_pos;            /* q8 samples into the current step */
    uint16_t seq_idx;
    uint8_t seq_notes[4];        /* sounding seq notes */
    uint8_t seq_n;
    uint8_t seq_hold;            /* last step slides: keep the notes until the next step */
    uint8_t slide_glide;         /* next legato note glides (slide) */
    uint32_t seq_off;
    /* Jangada: ratchet of the playing step */
    uint8_t rat_left, rat_idx;   /* hits still to come, the step they repeat */
    uint32_t rat_pos, rat_sub, rat_gate;   /* samples into the sub-step, its length, its gate */
    /* Jangada (after SLOOP 2.4): the steps' nudges, conditions and locks; the locks in force (seq.c lock_step:
     * the parameters overridden now, what they were, what the lock set); the nudge's state (seq_tick) */
    seqx_t x;
    uint8_t lk_n, lk_param[NLOCK];
    int16_t lk_base[NLOCK], lk_set[NLOCK];
    uint8_t mx_due;              /* the grid step seq_idx has not fired yet (nudged late) */
    uint8_t mx_early;            /* the next step fired already (nudged early) */
    uint8_t seq_active;          /* any step programmed */
    uint8_t rskip_idx;           /* live recording put notes into the step about to play: */
    uint8_t rskip_n, rskip[4];   /* do not trigger them again there (they sound already) */
    /* live recording of held notes (seq.c rec_hold): the steps they are held into become TIEs */
    uint8_t rh_n, rh_note[4];    /* recorded notes still held, 0 = none */
    uint8_t rh_start;            /* the step they were recorded into */
    uint8_t rh_ties;             /* TIE steps written after it */
    uint8_t rh_last;             /* the last of them; rh_bak: what it held (an early release puts it back) */
    step_t rh_bak;
    /* mono */
    uint8_t mono_stack[8];
    uint8_t nmono;
    uint8_t mono_note;           /* note the MONO / LEGATO / UNISON voice(s) play, 0 = none */
    uint8_t rr;                  /* POLY ROTATE: next voice to try */
    /* mix runtime */
    int32_t peak;
    int32_t dist_hp, dist_lp1, dist_lp2;   /* DIST insert state (fx.c) */
    int32_t dist_x1, dist_x2;    /* Jangada GRIT: the other DIST types' state (FUZZ FOLD CRUSH), */
    uint32_t dist_ph;            /* the RING carrier's phase, */
    uint8_t dist_mode;           /* the type the ISR ran last (0 off, 1 + P_DTYPE): a change crossfades */
    uint8_t tail;                /* blocks to mix after the last voice (the DIST / INSERT tail) */
    uint8_t ins_run;             /* the INSERT still sounds (fx.c track_insert: its wet share above 0) */
    int16_t armp, aholdp;        /* P_AMODE / P_AHOLD as last seen by the ISR */
    /* Jangada: MIDI controllers of the track's channel (seq.c midi_cc): pitch bend in 1/16 semitones
     * (+-2 st), MOD WHEEL / AFTERTOUCH / EXPRESSION 0..127 (mod.c sources), the sustain pedal and the
     * notes it holds */
    int16_t bend16;
    uint8_t mw, at, ex, sus;
    uint32_t sus_held[4];
    /* engine switch (voice.c engine_block): the old engine's voices fade out, then it switches */
    uint8_t xf_on, xf;           /* fading; blocks of the fade still to render */
    int16_t pe_old[NEDIT];       /* P_E0.. of the sounding engine: the fade renders with these */
    uint8_t xp_n, xp_note[4], xp_vel[4];   /* note-ons during the fade, played on the new engine */
} track_t;

typedef struct {
    int16_t g[G_COUNT];
    uint8_t playing, seq_mode;
    uint8_t rec;                 /* live recording armed: bit per track */
    uint8_t sel;                 /* selected track 0..NTRK-1: keys, pages, editor */
    int8_t octave;
    uint8_t solo;                /* Jangada: soloed tracks, bit per track (GLO layer); 0 = none. Not saved */
    uint32_t tick;               /* sub-blocks since play */
    uint32_t cpu_q8;             /* audio ISR load, 1/256 */
    uint32_t master_q12;
    uint8_t t4;                  /* Jangada: track 4 a synth (1), as the audio ISR sees it: G_T4 is the request,
                                  * ui.c t4_follow / project_load apply it atomically, the track ready */
    int32_t batt_raw;            /* smoothed ADC ch3 (battery divider), 0 = not read yet */
} song_t;

static track_t trk[NTRK];        /* the instrument: three parts and the drum track */
static song_t song;
#define TSEL (&trk[song.sel])    /* the selected track */
#define TDRUM (&trk[TRK_DRUM])
static int is_drum(const track_t *t) { return t == TDRUM && !song.t4; }
/* Jangada: track i plays an engine (tracks 1..3 always, track 4 when GLO > DRUMS T4 is SYNTH) */
static int trk_synth(uint32_t i) { return i < NPART || (i == TRK_DRUM && song.t4); }
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")   /* slot store before the index update */
static volatile uint32_t fm1_ms;  /* milliseconds since boot (TIMER4-based, TIMER5 ISR in main.c) */
/* boot-loop guard (main.c): two boots in a row that die in the first 30 s -> the USB rescue
 * (recovery.c); the rescue itself dying -> UBOOT (Jangada, after SLOOP: bootguard.h) */
#include "bootguard.h"
bootguard_t bootguard __attribute__((section(".noinit")));

#include "keys.h"                /* Jangada: stable keys of the P_* parameters (saved data) */
