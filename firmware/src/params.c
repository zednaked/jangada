/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Parameter descriptors, formatting and the page table. */
static const char *const N_LWAVE[] = {"SIN", "TRI", "SAW", "SQR", "S&H"};
static const char *const N_AMODE[] = {"OFF", "UP", "DN", "UPDN", "RND", "ORD", "UDI", "RPT"};   /* UDI: UPDN with the ends repeated; RPT: the whole chord */
static const char *const N_DIV[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T"};
/* arp RATE and sequencer DIV: N_DIV plus long values for drones (appended, so saved indices keep their meaning) */
static const char *const N_DIVL[] = {"1/4", "1/8", "1/16", "1/32", "8T", "16T", "1/2", "1/1", "2BAR", "4BAR"};
static const char *const N_SCALE[] = {"CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM",
                                    "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH",
                                    "NORD"};   /* Jangada: appended (saved indices keep their scale) */
static const char *const N_ONOFF[] = {"OFF", "ON"};
static const char *const N_T4[] = {"DRUM", "SYNTH"};
/* Jangada modulation matrix (mod.c): sources, and the targets before the engine's own (MD_E0..) */
static const char *const N_MSRC[] = {"OFF", "LFO", "ENV", "VEL", "KEY", "RND", "MODW", "AT", "EXPR", "DRIFT"};
static const char *const N_MDST[] = {"CUT", "PIT", "SHP", "E1", "E2", "E3", "E4", "E5", "E6", "E7", "E8",
                                     "E9", "E10", "E11", "E12", "E13", "E14", "E15", "E16"};
static const char *const N_QUANT[] = {"OFF", "SNAP", "WHITE"};   /* seq.c kb_map; 1 = SNAP as the old ON */
static const char *const N_VOICE[] = {"POLY", "MONO", "LEG", "UNI"};   /* V_POLY .. V_UNISON */
static const char *const N_GLMODE[] = {"RATE", "TIME"};
static const char *const N_PRIO[] = {"LAST", "LOW", "HIGH"};
static const char *const N_ALLOC[] = {"ROT", "REUSE"};
static const char *const N_ORDER[] = {"NOTE", "PLAY"};
static const char *const N_CLOCK[] = {"INT", "USB", "TRS"};   /* Jangada: follow the MIDI clock of USB or the TRS jack */
static const char *const N_SYNC[] = {"OFF", "OUT"};    /* Jangada: OUT = send MIDI clock (USB) */
static const char *const N_CHORD[] = {"OFF", "TRIAD", "7TH", "9TH", "SUS4", "POWER"};   /* seq.c CHORD_DEG (Jangada) */
static const char *const N_KIT[] = {"GM", DS_KIT_NAME_LIST};   /* drums.c DRUM_KIT_NAMES (Jangada) */
static const char *const N_BEAT[] = {"--", DS_BEAT_NAME_LIST};  /* Jangada: GLO > KIT BEAT (DS_BEATS) */
static const char *const N_RTYPE[] = {"ROOM", "SPRING", "PLATE"};   /* fx.c (Jangada) */
/* Jangada GRIT: the DIST types (fx.c track_dist); SOFT first: older projects and presets keep their sound */
static const char *const N_DTYPE[] = {"SOFT", "FUZZ", "FOLD", "CRUSH", "RING"};
/* Jangada DRONES (drone.c): the RAMP of TENSION, in bars (DR_RAMP_BARS) */
static const char *const N_TRAMP[] = {"OFF", "1BAR", "2BAR", "4BAR", "8BAR", "16BAR", "32BAR"};
static const char *const N_NOTE[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *const N_DASH[] = {"--"};
static const char *const N_GO[] = {"--", "GO"};
static const char *const N_SLCR[] = {"OFF", "GATE", "STUT"};             /* SL_OFF .. SL_STUT (slicer.c) */
static const char *const N_SLDIV[] = {"1/8", "1/16", "1/32", "8T", "16T", "32T"};   /* SL_DEN */
static const char *const N_ENGNAME[] = {"ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN",
#if FELUCCA_SLICE
                                             "SLICE",
#endif
};

#define PD(l, f, mn, mx, df) {l, f, mn, mx, df, 0, 0}
#define PE(l, n, df) {l, F_ENUM, 0, (int16_t)(sizeof(n) / sizeof(n[0]) - 1), df, n, 0}

static const param_desc_t TP[P_COUNT] = {
    [P_LEVEL] = PD("LVL", F_DB, 0, 127, 104),
    [P_ATK] = PD("ATK", F_TIME, 0, 127, 10),
    [P_DEC] = PD("DEC", F_TIME, 0, 127, 70),
    [P_SUS] = PD("SUS", F_PCT, 0, 127, 90),
    [P_REL] = PD("REL", F_TIME, 0, 127, 60),
    [P_ED_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_ED_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_ED_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_ED_FX] = PD("FX", F_BIPCT, -64, 63, 0),
    [P_LRATE] = PD("RATE", F_LFOHZ, 0, 127, 60),
    [P_LWAVE] = PE("WAVE", N_LWAVE, 0),
    [P_LPHASE] = PD("PHS", F_INT, 0, 127, 0),
    [P_LFADE] = PD("FADE", F_TIME, 0, 127, 0),
    [P_LD_PIT] = PD("PIT", F_BIPCT, -64, 63, 0),
    [P_LD_FLT] = PD("FLT", F_BIPCT, -64, 63, 0),
    [P_LD_SHP] = PD("SHP", F_BIPCT, -64, 63, 0),
    [P_LD_AMP] = PD("AMP", F_PCT, 0, 127, 0),
    [P_AMODE] = PE("MODE", N_AMODE, 0),
    [P_ARATE] = PE("RATE", N_DIVL, 2),
    [P_AOCT] = PD("OCT", F_INT, 1, 4, 1),
    [P_AGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_ASWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_APROB] = PD("PROB", F_PCT, 0, 127, 127),
    [P_AHOLD] = PE("HOLD", N_ONOFF, 0),
    [P_AORDER] = PE("ORD", N_ORDER, 0),
    [P_ROOT] = PD("ROOT", F_NOTE, 0, 11, 0),
    [P_SCALE] = PE("SCL", N_SCALE, 0),
    [P_QUANT] = PE("QNT", N_QUANT, 0),
    [P_TRANS] = PD("TRN", F_SEMI, -24, 24, 0),
    [P_SLEN] = PD("LEN", F_STEPS, 1, NSTEP, 16),
    [P_SDIV] = PE("DIV", N_DIVL, 2),
    [P_SSWING] = PD("SWG", F_PCT, 0, 100, 0),
    [P_SGATE] = PD("GATE", F_PCT, 1, 127, 64),
    [P_DIST] = PD("DST", F_PCT, 0, 127, 0),
    [P_CHOR] = PD("CHO", F_PCT, 0, 127, 0),
    [P_DLY] = PD("DLY", F_PCT, 0, 127, 0),
    [P_REV] = PD("REV", F_PCT, 0, 127, 0),
    [P_VOICE] = PE("VCE", N_VOICE, 0),
    [P_GLIDE] = PD("GLD", F_TIME, 0, 127, 0),
    [P_GLMODE] = PE("GLMOD", N_GLMODE, 0),
    [P_PRIO] = PE("PRIO", N_PRIO, 0),
    [P_ALLOC] = PE("ALLOC", N_ALLOC, 0),
    [P_DETUNE] = PD("DTUNE", F_INT, 0, 127, 40),
    [P_PAN] = PD("PAN", F_BIPCT, -64, 63, 0),
    [P_MUTE] = PE("MUTE", N_ONOFF, 0),
    [P_SLCR] = PE("SLCR", N_SLCR, 0),
    [P_SLPAT] = PD("PAT", F_INT, 1, 16, 1),        /* SL_PAT[] */
    [P_SLRATE] = PE("RATE", N_SLDIV, 1),
    [P_SLDEPTH] = PD("DEPTH", F_PCT, 0, 127, 127),
    /* Jangada: modulation matrix (mod.c). DST names follow the engine (track_desc) */
    [P_M1SRC] = PE("SRC", N_MSRC, 0), [P_M1DST] = PE("DST", N_MDST, 0), [P_M1AMT] = PD("AMT", F_BIPCT, -64, 63, 0),
    [P_M2SRC] = PE("SRC", N_MSRC, 0), [P_M2DST] = PE("DST", N_MDST, 0), [P_M2AMT] = PD("AMT", F_BIPCT, -64, 63, 0),
    [P_M3SRC] = PE("SRC", N_MSRC, 0), [P_M3DST] = PE("DST", N_MDST, 0), [P_M3AMT] = PD("AMT", F_BIPCT, -64, 63, 0),
    [P_M4SRC] = PE("SRC", N_MSRC, 0), [P_M4DST] = PE("DST", N_MDST, 0), [P_M4AMT] = PD("AMT", F_BIPCT, -64, 63, 0),
    [P_CHORD] = PE("CHRD", N_CHORD, 0),
    /* Jangada GRIT: the DIST type and the RING carrier (30 Hz .. 16 kHz, CUTOFF_HZ; 48 = 320 Hz) */
    [P_DTYPE] = PE("TYPE", N_DTYPE, 0),
    [P_DRING] = PD("FREQ", F_CUTOFF, 0, 127, 48),
    /* Jangada DRONES (drone.c): EVOL the slow walk, TENS the tension macro, RAMP its time in bars */
    [P_EVOL] = PD("EVOL", F_PCT, 0, 127, 0),
    [P_TENS] = PD("TENS", F_PCT, 0, 127, 0),
    [P_TRAMP] = PE("RAMP", N_TRAMP, 0),
    [P_TFLT] = PD("FILT", F_FILT, -64, 63, 0),     /* Jangada 0.7: the track's filter (fx.c) */
};

static const param_desc_t GP[G_COUNT] = {
    [G_BPM] = PD("BPM", F_BPM, 40, 240, 120),
    [G_SWING] = PD("SWG", F_PCT, 0, 100, 0),
    [G_CLOCK] = PE("CLK", N_CLOCK, 0),
    [G_TUNE] = PD("TUNE", F_INT, -50, 50, 0),
    [G_DTIME] = PE("TIME", N_DIV, 1),
    [G_DFDBK] = PD("FDBK", F_PCT, 0, 120, 60),
    [G_DCOLOR] = PD("COLR", F_PCT, 0, 127, 70),
    [G_DMIX] = PD("MIX", F_PCT, 0, 127, 90),
    [G_RSIZE] = PD("SIZE", F_PCT, 0, 127, 90),
    [G_RDAMP] = PD("DAMP", F_PCT, 0, 127, 60),
    [G_CRATE] = PD("CRT", F_LFOHZ, 0, 127, 40),
    [G_CDEPTH] = PD("CDP", F_PCT, 0, 127, 60),
    [G_MIDI] = PE("MIDI", N_DASH, 0),
    [G_SYNC] = PE("SYNC", N_SYNC, 0),
    [G_ROUTE] = PE("ROUT", N_DASH, 0),
    [G_INFO] = PD("CPU", F_INT, 0, 0, 0),
    [G_SLOT] = PD("SLOT", F_INT, 1, 4, 1),
    [G_NAME] = PE("NAME", N_DASH, 0),
    [G_LOAD] = PE("LOAD", N_GO, 0),
    [G_SAVE] = PE("SAVE", N_GO, 0),
    [G_ENGSEL] = PE("ENG", N_ENGNAME, 0),
    [G_ENGGO] = PE("SET", N_GO, 0),
    [G_CLRSEQ] = PE("CLRSQ", N_GO, 0),
    [G_INITSND] = PE("INIT", N_GO, 0),
    [G_DRCH] = PD("CH", F_INT, 0, 16, 10),            /* GM drum part MIDI channel, 0 = off */
    [G_DRLVL] = PD("LVL", F_INT, 0, 127, 100),
    [G_DRREV] = PD("REV", F_INT, 0, 127, 16),
    [G_T4] = PE("T4", N_T4, 0),                        /* Jangada: track 4 DRUM / SYNTH */
    [G_DUST] = PD("DUST", F_PCT, 0, 127, 0),           /* Jangada: the master bus (fx.c) */
    [G_DUCK] = PD("DUCK", F_PCT, 0, 127, 0),
    [G_FILT] = PD("FILT", F_BIPCT, -64, 63, 0),
    [G_KIT] = PE("KIT", N_KIT, 0),                     /* Jangada: the drum track's kit */
    [G_RTYPE] = PE("TYPE", N_RTYPE, 0),                /* Jangada: the reverb model */
    /* Jangada GRIT: the master's worn tape and the hum of an analog recording (fx.c) */
    [G_TAPE] = PD("TAPE", F_PCT, 0, 127, 0),
    [G_HUM] = PD("HUM", F_PCT, 0, 127, 0),
};

static const param_desc_t *track_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id < P_E0 + NEDIT) {            /* the engine asked for (t->engine follows after a fade) */
        const engine_t *e = ENGINES[t->eng_req % NENGINES];
        const param_desc_t *d = e->desc ? e->desc(t, id - P_E0) : 0;   /* a mode-dependent label / names */
        return d ? d : &e->edit[id - P_E0];
    }
    if (id == P_M1DST || id == P_M2DST || id == P_M3DST || id == P_M4DST)
        return mod_dst_desc(ENGINES[t->eng_req % NENGINES]);
    if (id == P_DIST && t->p[P_DTYPE] > 0) {           /* Jangada GRIT: DIST is named by its type (FX page) */
        static const param_desc_t DIST_AS[4] = {PD("FUZZ", F_PCT, 0, 127, 0), PD("FOLD", F_PCT, 0, 127, 0),
                                                PD("CRUSH", F_PCT, 0, 127, 0), PD("RING", F_PCT, 0, 127, 0)};
        return &DIST_AS[(t->p[P_DTYPE] - 1) & 3];
    }
    return &TP[id];
}

/* value string (<= 5 chars) and unit for a parameter value */
static void param_format(const param_desc_t *d, int32_t v, char *val, const char **unit)
{
    *unit = "";
    switch (d->fmt) {
    case F_PCT:
        fmt_int(val, (v * 100 + 63) / 127);
        *unit = "%";
        break;
    case F_BIPCT:
        fmt_int(val, v * 100 / 64);
        if (v > 0) {
            char t[8];
            fmt_int(t, v * 100 / 64);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "%";
        break;
    case F_TIME: {
        uint32_t ms10 = TIME_MS_X10[v & 127];
        if (ms10 < 100u) {
            fmt_fix(val, (int32_t)ms10, 1);
            *unit = "ms";
        } else if (ms10 < 10000u) {
            fmt_int(val, (int32_t)((ms10 + 5u) / 10u));
            *unit = "ms";
        } else {
            fmt_fix(val, (int32_t)(ms10 / 100u), 2);
            if (ms10 >= 100000u)
                fmt_fix(val, (int32_t)(ms10 / 1000u), 1);
            *unit = "s";
        }
        break;
    }
    case F_LFOHZ: {
        uint32_t h = LFO_HZ_X100[v & 127];
        if (h < 1000u)
            fmt_fix(val, (int32_t)h, 2);
        else
            fmt_fix(val, (int32_t)(h / 10u), 1);
        *unit = "Hz";
        break;
    }
    case F_CUTOFF: {
        uint32_t h = CUTOFF_HZ[v & 127];
        if (h < 1000u) {
            fmt_int(val, (int32_t)h);
            *unit = "Hz";
        } else {
            fmt_fix(val, (int32_t)(h / 100u), 1);
            *unit = "kHz";
        }
        break;
    }
    case F_DB:
        if (v <= 0) {
            str_cpy(val, "OFF", 6);
        } else {
            fmt_fix(val, LEVEL_DB_X10[v], 1);
            *unit = "dB";
        }
        break;
    case F_SEMI:
        fmt_int(val, v);
        if (v > 0) {
            char t[8];
            fmt_int(t, v);
            val[0] = '+';
            str_cpy(val + 1, t, 6);
        }
        *unit = "st";
        break;
    case F_ENUM:
        str_cpy(val, d->names[v < d->min ? d->min : v > d->max ? d->max : v], 6);
        if (d->unit)
            *unit = d->unit;
        break;
    case F_BPM:
        fmt_int(val, v);
        *unit = "BPM";
        break;
    case F_NOTE:
        str_cpy(val, N_NOTE[v % 12], 6);
        break;
    case F_ONOFF:
        str_cpy(val, N_ONOFF[v ? 1 : 0], 6);
        break;
    case F_STEPS:
        fmt_int(val, v);
        *unit = "STEP";
        break;
    case F_FILT:                                      /* LP 0..100 closing, HP 0..100 opening */
        if (!v) {
            str_cpy(val, "OFF", 6);
        } else {
            str_cpy(val, v < 0 ? "LP" : "HP", 6);
            fmt_int(val + 2, v < 0 ? (-v * 100 + 32) / 64 : (v * 100 + 31) / 63);
        }
        break;
    default:
        if (d->names) {                               /* F_INT with a 0-terminated name list: the range */
            uint32_t k = 0;                           /* split evenly over the names (engine desc hooks) */
            while (d->names[k])
                k++;
            str_cpy(val, d->names[(uint32_t)(clamp(v, d->min, d->max) - d->min) * k / (uint32_t)(d->max - d->min + 1)], 6);
        } else {
            fmt_int(val, v);
        }
        if (d->unit)
            *unit = d->unit;
        break;
    }
}

/* ------------------------------------------------------------ pages --- */
enum { FAM_HOME, FAM_ENV, FAM_LFO, FAM_FX, FAM_SCL, FAM_EDIT, FAM_GLO, FAM_SAVE, FAM_ARP, FAM_SEQ, FAM_TRK,
       FAM_COUNT };
enum { SC_TRACK, SC_GLOBAL, SC_ENGINE, SC_STEP, SC_TRK };   /* SC_TRK: the TRACKS page (ui_input.c tracks_edit) */
enum { GR_NONE, GR_ADSR, GR_LFO, GR_STEPS, GR_ARP, GR_SCALE, GR_FX, GR_ROLL, GR_BROWSE, GR_SLOTS, GR_USER, GR_TRK,
       GR_SLCR, GR_DRONE };

typedef struct {
    const char *title;
    uint8_t fam, scope, graph;
    uint8_t id[4];               /* param ids; 0xFF = empty slot */
} page_t;
#define PG_BEAT 0xFEu            /* Jangada: GLO > KIT's BEAT column (drums.beat, page_desc; not a G_ param) */

static const page_t PAGES[] = {
    {"ENV", FAM_ENV, SC_TRACK, GR_ADSR, {P_ATK, P_DEC, P_SUS, P_REL}},
    {"ENV DEST", FAM_ENV, SC_TRACK, GR_NONE, {P_ED_FLT, P_ED_PIT, P_ED_SHP, 0xFF}},   /* (P_ED_FX: nothing reads it) */
    {"LFO", FAM_LFO, SC_TRACK, GR_LFO, {P_LRATE, P_LWAVE, P_LPHASE, P_LFADE}},
    {"LFO DEST", FAM_LFO, SC_TRACK, GR_NONE, {P_LD_PIT, P_LD_FLT, P_LD_SHP, P_LD_AMP}},
    {"MOD 1", FAM_LFO, SC_TRACK, GR_NONE, {P_M1SRC, P_M1DST, P_M1AMT, 0xFF}},   /* Jangada: matrix */
    {"MOD 2", FAM_LFO, SC_TRACK, GR_NONE, {P_M2SRC, P_M2DST, P_M2AMT, 0xFF}},
    {"MOD 3", FAM_LFO, SC_TRACK, GR_NONE, {P_M3SRC, P_M3DST, P_M3AMT, 0xFF}},
    {"MOD 4", FAM_LFO, SC_TRACK, GR_NONE, {P_M4SRC, P_M4DST, P_M4AMT, 0xFF}},
    {"FX", FAM_FX, SC_TRACK, GR_FX, {P_DIST, P_CHOR, P_DLY, P_REV}},
    {"DIST", FAM_FX, SC_TRACK, GR_NONE, {P_DTYPE, P_DIST, P_DRING, P_TFLT}},   /* Jangada GRIT: TYPE, DIST, FREQ (RING); the
                                                                                 * track's FILT (drum track too) */
    {"SLICER", FAM_FX, SC_TRACK, GR_SLCR, {P_SLCR, P_SLPAT, P_SLRATE, P_SLDEPTH}},   /* drum track too */
    {"DLY", FAM_FX, SC_GLOBAL, GR_NONE, {G_DTIME, G_DFDBK, G_DCOLOR, G_DMIX}},
    {"REV/CHO", FAM_FX, SC_GLOBAL, GR_NONE, {G_RSIZE, G_RDAMP, G_CRATE, G_CDEPTH}},
    {"REVERB", FAM_FX, SC_GLOBAL, GR_NONE, {G_RTYPE, G_RSIZE, G_RDAMP, 0xFF}},   /* Jangada: ROOM SPRING PLATE */
    {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_QUANT, P_TRANS}},
    {"CHORD", FAM_SCL, SC_TRACK, GR_NONE, {P_CHORD, 0xFF, 0xFF, 0xFF}},   /* Jangada: one key, a chord */
    {"EDIT 1", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E0, P_E1, P_E2, P_E3}},
    {"EDIT 2", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E4, P_E5, P_E6, P_E7}},
    {"EDIT 3", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E8, P_E9, P_E10, P_E11}},     /* Jangada: shown when the */
    {"EDIT 4", FAM_EDIT, SC_ENGINE, GR_NONE, {P_E12, P_E13, P_E14, P_E15}},   /* engine has them (page_used) */
    {"VOICE", FAM_EDIT, SC_TRACK, GR_NONE, {P_VOICE, P_GLIDE, P_GLMODE, P_PRIO}},
    {"VOICE 2", FAM_EDIT, SC_TRACK, GR_NONE, {P_ALLOC, P_DETUNE, P_PAN, P_MUTE}},
    {"GLOBAL", FAM_GLO, SC_GLOBAL, GR_NONE, {G_BPM, G_SWING, G_CLOCK, G_TUNE}},
    {"SYSTEM", FAM_GLO, SC_GLOBAL, GR_NONE, {G_MIDI, G_SYNC, G_ROUTE, G_INFO}},
    {"DRUMS", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DRCH, G_DRLVL, G_DRREV, G_T4}},   /* GM kit on MIDI ch 10; T4: Jangada */
    {"MASTER", FAM_GLO, SC_GLOBAL, GR_NONE, {G_DUST, G_DUCK, G_FILT, G_TAPE}},
    {"MASTER 2", FAM_GLO, SC_GLOBAL, GR_NONE, {G_HUM, G_TAPE, G_DUST, 0xFF}},   /* Jangada GRIT: the noise of the past */
    {"KIT", FAM_GLO, SC_GLOBAL, GR_NONE, {G_KIT, G_DRLVL, G_DRREV, PG_BEAT}},   /* Jangada: the drum kit, a beat */
    {"PRESETS", FAM_SAVE, SC_GLOBAL, GR_BROWSE, {0xFF, 0xFF, 0xFF, 0xFF}},   /* browser: PRESETS knob / KNOB 1 */
    {"USER", FAM_SAVE, SC_GLOBAL, GR_USER, {0xFF, 0xFF, 0xFF, 0xFF}},       /* user presets: SLOT LOAD ERASE SAVE */
    {"PROJECT", FAM_SAVE, SC_GLOBAL, GR_SLOTS, {G_SLOT, 0xFF, G_LOAD, G_SAVE}},
    {"TOOLS", FAM_SAVE, SC_GLOBAL, GR_NONE, {G_CLRSEQ, G_INITSND, 0xFF, 0xFF}},
    {"ARP", FAM_ARP, SC_TRACK, GR_ARP, {P_AMODE, P_ARATE, P_AOCT, P_AGATE}},
    {"ARP 2", FAM_ARP, SC_TRACK, GR_NONE, {P_ASWING, P_APROB, P_AHOLD, P_AORDER}},
    {"DRONE", FAM_ARP, SC_TRACK, GR_DRONE, {P_AHOLD, P_EVOL, P_TENS, P_TRAMP}},   /* Jangada DRONES (drone.c) */
    {"STEP", FAM_SEQ, SC_STEP, GR_ROLL, {0, 1, 2, 3}},
    {"STEP 2", FAM_SEQ, SC_STEP, GR_ROLL, {0, 4, 5, 0xFF}},   /* Jangada: STEP RTCH CHNC */
    {"PATTERN", FAM_SEQ, SC_TRACK, GR_STEPS, {P_SLEN, P_SDIV, P_SSWING, P_SGATE}},
    {"TRACKS", FAM_TRK, SC_TRK, GR_TRK, {0, 1, 2, 3}},   /* REC button; TYPE LEVEL LEN PAN (Jangada: TYPE) */
};
#define NPAGES (sizeof(PAGES) / sizeof(PAGES[0]))

/* the drum track has no sound of its own: it uses the global pages (not the preset
 * pages, nor TOOLS > INIT: page_desc), STEP, PATTERN, SLICER and TRACKS; every other page
 * shows "DRUM TRACK" */
static int page_for_drum(const page_t *pg)
{
    if (pg->scope == SC_GLOBAL)
        return pg->graph != GR_BROWSE && pg->graph != GR_USER;
    return pg->scope != SC_ENGINE && (pg->scope != SC_TRACK || pg->fam == FAM_SEQ || pg->graph == GR_SLCR);
}

/* Jangada: an EDIT page is shown only when the engine has a parameter on it (EDIT 3 / 4:
 * engines with more than 8) */
static int page_used(uint32_t pi)
{
    const page_t *pg = &PAGES[pi];
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    uint32_t k;
    if (pg->scope != SC_ENGINE)
        return 1;
    for (k = 0; k < 4u; k++)
        if (pg->id[k] >= P_E0 && pg->id[k] < P_E0 + NEDIT && e->edit[pg->id[k] - P_E0].label)
            return 1;
    return 0;
}

static const param_desc_t *page_desc(const page_t *pg, uint32_t slot, int16_t **valp)
{
    static const param_desc_t BEAT_D = PE("BEAT", N_BEAT, 0);
    uint32_t id = pg->id[slot];
    if (id == PG_BEAT) {                               /* Jangada: not saved; turning it loads a beat */
        *valp = &drums.beat;
        return &BEAT_D;
    }
    if (id == 0xFFu || (is_drum(TSEL) && (!page_for_drum(pg) || (pg->scope == SC_GLOBAL && id == G_INITSND)))) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_STEP || pg->scope == SC_TRK) {
        *valp = 0;
        return 0;
    }
    if (pg->scope == SC_GLOBAL) {
        *valp = &song.g[id];
        return &GP[id];
    }
    *valp = &TSEL->p[id];
    return track_desc(TSEL, id);
}
