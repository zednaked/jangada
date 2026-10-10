/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Physical panel: which matrix button / encoder carries which printed label.
 * The default table can be overridden by HARDWARE CALIBRATION (hold OCT- and
 * OCT+ while powering on), which asks for each label in turn. The learned
 * table lives in .noinit and, with FELUCCA_FLASH, in flash with the settings
 * (project.c). */
enum { B_FX, B_SCL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static const char *const B_NAME[NB] = {"FX", "SCL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE",
                                        "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"};
static const char *const E_NAME[NE] = {"SELECT", "ALGORITHM", "PRESETS", "KNOB 1", "KNOB 2",
                                        "KNOB 3", "KNOB 4"};
#define PANEL_MAGIC 0x50414E35u          /* "PAN5": bump when PANEL_DEFAULT changes */

typedef struct {
    uint32_t magic;
    uint8_t btn[NB];             /* matrix button id (0..13) per label */
    uint8_t enc[NE];             /* matrix encoder (0..6) per role */
    int8_t dir[NE];              /* +1 / -1 so that clockwise is + */
} panel_t;
panel_t panel __attribute__((section(".noinit")));

static const panel_t PANEL_DEFAULT = {
    PANEL_MAGIC,
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},   /* PLAY = 12, REC = 13 */
    {0, 1, 6, 2, 3, 4, 5},                           /* SELECT = enc 0, ALGORITHM = enc 1, PRESETS = enc 6 */
    {1, 1, 1, 1, 1, 1, 1},
};

/* a table to use: a permutation of the buttons and of the knobs, each knob turning one way or the other
 * (Jangada, after SLOOP 2.3: 0.2 took any ids in range, so a damaged table could leave a label on no
 * button and two on one) */
static int panel_valid(const panel_t *q)
{
    uint32_t i, b = 0, e = 0;
    if (q->magic != PANEL_MAGIC)
        return 0;
    for (i = 0; i < NB; i++) {
        if (q->btn[i] >= 14u || (b >> q->btn[i]) & 1u)
            return 0;
        b |= 1u << q->btn[i];
    }
    for (i = 0; i < NE; i++) {
        if (q->enc[i] >= 7u || (e >> q->enc[i]) & 1u || (q->dir[i] != 1 && q->dir[i] != -1))
            return 0;
        e |= 1u << q->enc[i];
    }
    return 1;
}

static void panel_init(void)                     /* also after a flash load: ids are used as array indexes and shifts */
{
    if (!panel_valid(&panel))
        panel = PANEL_DEFAULT;
}

static uint32_t panel_btn_of(uint32_t matrix_id)        /* label of a matrix button, NB if none */
{
    uint32_t b;
    for (b = 0; b < NB; b++)
        if (panel.btn[b] == matrix_id)
            return b;
    return NB;
}

static void panel_led(uint32_t label, int on) { fm1_led_key(panel.btn[label], on); }

/* steps of a role, + = clockwise */
static uint32_t ui_input_ms;                    /* Jangada: the last button, key or knob turn (the autosave waits) */
static int32_t panel_enc(uint32_t role)
{
    int32_t s = fm1_enc_take(panel.enc[role]) * panel.dir[role];
    if (s)
        ui_input_ms = fm1_ms;                      /* a knob turning is not idle either */
    return s;
}

/* user settings that survive a reset */
#define SETTINGS_MAGIC 0x53455433u              /* "SET3" */
struct { uint32_t magic, palette, lowcut; } settings __attribute__((section(".noinit")));   /* (Jangada: ZOOM is gone) */

static void settings_save(void);              /* project.c: flash copy (FELUCCA_FLASH) */
static uint8_t settings_later;                 /* changed while playing: saved once stopped (project.c) */
static uint8_t vis_style;                      /* Jangada 0.7: the visualiser's style (ui_vis.c), kept here */
/* Jangada 0.9.6: menu 2ND CORE. AUTO: started on the boot loader's supply, held for good (OFF) if it misreads;
 * OFF: one core; BOOST: the supply raised first (hal/fm1_sys.h), at the owner's risk. Read at power-on only */
enum { CPU2_AUTO, CPU2_OFF, CPU2_BOOST, CPU2_N };
static uint8_t cpu2_mode, cpu2_boot = CPU2_OFF;    /* the setting; what this boot started with */
static uint8_t cpu2_parked;                     /* AUTO turned itself OFF this session (it misread) */

/* Jangada (after SLOOP 2.3): the lights for playing in the dark (menu LIGHTS / KEYS / NOTES) and the USB
 * audio level (menu USB AUDIO: fx.c usb_full), settings of the FM-1: kept in flash with the others (project.c
 * persist_t.lights), not in a project; not in .noinit, so nothing there moves */
enum { LIGHTS_OFF, LIGHTS_LOW, LIGHTS_MID, LIGHTS_HIGH, LIGHTS_N };
enum { KEYS_OFF, KEYS_C, KEYS_WHITE, KEYS_N };
static uint8_t lights_lvl, lights_keys;         /* every button glows (LIGHTS); the C or white keys too (KEYS) */
static uint8_t lights_notes;                    /* 1: the notes sounding light their keys */
static const uint16_t LIGHTS_NS[LIGHTS_N] = {0u, 500u, 1000u, 2000u};   /* the backlight pulse a frame (ns): a lit
                                                * LED ~95 us, the glow (landmarks) 4 us (fm1_input.h) */
static uint32_t lights_word(void)
{
    return (uint32_t)lights_lvl | (uint32_t)lights_keys << 4 | (uint32_t)(lights_notes != 0u) << 8 |
           (uint32_t)(usb_full != 0u) << 11 | (uint32_t)(midi_seq_out != 0u) << 12 | (uint32_t)(midi_clk_only != 0u) << 13 |
           (uint32_t)(vis_style & 7u) << 14 | (uint32_t)(cpu2_mode & 3u) << 17;
}
static void lights_from_word(uint32_t w)        /* (each field checked: a damaged word lights nothing) */
{
    lights_lvl = (uint8_t)((w & 15u) < LIGHTS_N ? (w & 15u) : LIGHTS_OFF);
    lights_keys = (uint8_t)(((w >> 4) & 15u) < KEYS_N ? ((w >> 4) & 15u) : KEYS_OFF);
    lights_notes = (uint8_t)((w >> 8) & 1u);
    usb_full = (uint8_t)((w >> 11) & 1u);
    midi_seq_out = (uint8_t)((w >> 12) & 1u);       /* Jangada 0.7: MIDI OUT = SEQ, MIDI IN = CLOCK (seq.c) */
    midi_clk_only = (uint8_t)((w >> 13) & 1u);
    vis_style = (uint8_t)((w >> 14) & 7u);          /* (ui_vis.c takes it modulo its styles) */
    cpu2_mode = (uint8_t)(((w >> 17) & 3u) < CPU2_N ? ((w >> 17) & 3u) : CPU2_AUTO);   /* (0.9.6: 0 before, AUTO) */
}

static void settings_init(void)
{
    if (settings.magic != SETTINGS_MAGIC || settings.palette >= NPALETTES || settings.lowcut > 1u) {
        settings.magic = SETTINGS_MAGIC;
        settings.palette = 5;                  /* CHOQUE (Jangada default) */
        settings.lowcut = 0;
    }
    palette_set(settings.palette);
    fx_lowcut = (uint8_t)(settings.lowcut != 0);
}
