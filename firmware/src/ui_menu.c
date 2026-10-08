/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Menu (HOME held): COLOR, SPEAKER (the low cut for the small speaker), LIGHTS, KEYS, NOTES (the panel
 * in the dark), USB AUDIO (the level the computer records), NEW PROJECT, ABOUT.
 * Jangada: ZOOM and HARDWARE CALIBRATION left the menu (calibration: OCT- + OCT+ held at power-on).
 * LIGHTS, KEYS, NOTES and USB AUDIO: Jangada, after SLOOP 2.3 (settings of the FM-1, panel.c). MIDI OUT
 * (KEYS / SEQ: the sequencer and the arp too) and MIDI IN (NOTES / CLOCK: the clock only): Jangada 0.7, after
 * SLOOP 2.4 (seq.c). OCT- goes back (the BACK row is gone). */
/* ------------------------------------------------------------ menu --- */
enum { MI_COLOR, MI_SPEAKER, MI_LIGHTS, MI_KEYS, MI_NOTES, MI_USB, MI_MOUT, MI_MIN, MI_NEW, MI_ABOUT, MI_COUNT };
static const char *const MI_NAME[MI_COUNT] = {"COLOR", "SPEAKER", "LIGHTS", "KEYS", "NOTES", "USB AUDIO",
                                              "MIDI OUT", "MIDI IN", "NEW PROJECT", "ABOUT"};
static const char *const LIGHTS_NAME[LIGHTS_N] = {"OFF", "LOW", "MID", "HIGH"};
static const char *const KEYS_NAME[KEYS_N] = {"OFF", "C KEYS", "WHITE KEYS"};
#define MI_DY 17                                    /* rows between two menu lines */
static uint8_t menu_new_armed;                      /* NEW PROJECT: OCT+ once arms, again clears */
static void felucca_init(void);                     /* main.c */

/* NEW PROJECT with the audio ISR running (Jangada): the transport stops and every note is let go at the
 * next block (transport_req / panic_req, as a project load), and the reset itself runs with the IRQ off,
 * so no block is rendered over half-written tracks (a step index past a shorter pattern, an engine with
 * another's values). felucca_init is RAM only, about a project load's work. The engine that sounds stays
 * until its released voices faded on it (voice.c engine_block, as any engine change); the step counters
 * go to 0 (the song is stopped: START counts from step 0 again) */
static void menu_new_project(void)
{
    uint8_t eng[NTRK];
    uint32_t i;
    transport_req = 2;
    panic_req = (uint8_t)((1u << NTRK) - 1u);
    fm1_irq_off();
    for (i = 0; i < NTRK; i++)
        eng[i] = trk[i].engine;
    felucca_init();                                 /* the power-on tracks, empty patterns */
    for (i = 0; i < NTRK; i++) {
        trk[i].engine = eng[i];
        trk[i].seq_idx = 0;
        trk[i].seq_pos = 0;
    }
    fm1_irq_on();
    sync_reload = 1;
}

static void draw_menu(void)
{
    uint32_t i, pass, sig = ui.menu * 7u + ui.menu_sel * 131u + settings.palette * 1009u + settings.lowcut * 7919u +
                            menu_new_armed * 104729u + lights_word() * 1299709u;
    if (!ui.force && sig == ui.menu_sig)
        return;
    ui.menu_sig = sig;
    if (ui.force)                                   /* head + rule + two bands cover rows 0..229 */
        lcd_fill(0, H_HEAD + 1 + 124 + 85, 240, 240 - (H_HEAD + 1 + 124 + 85), C_BG);
    cv_begin(240, H_HEAD, C_BG);
    cv_text(4, 1, &FONT_S, ui.menu == 2 ? "ABOUT" : "MENU", C_HI);
    cv_blit(0, Y_HEAD);
    lcd_fill(0, H_HEAD, 240, 1, C_BG);
    for (pass = 0; pass < 2u; pass++) {             /* the canvas holds 124 rows: draw in two bands */
        cv_begin(240, pass ? 85u : 124u, C_BG);
        cv_oy = pass ? -124 : 0;
        if (ui.menu == 2) {
            cv_text(4, 2, &FONT_L, "JANGADA", C_HI);
            cv_text(4, 32, &FONT_S, FELUCCA_VERSION, C_HI);
            cv_text(236 - text_w(&FONT_S, FELUCCA_DATE), 32, &FONT_S, FELUCCA_DATE, C_GRAY);   /* build date (build.py) */
            cv_text(4, 46, &FONT_S, "GITHUB.COM/ZEDNAKED/JANGADA", C_AMB);
            cv_text(4, 60, &FONT_S, "FORK OF FELUCCA BY", C_GRAY);
            cv_text(cv_text(4, 73, &FONT_S, "LEO KUROSHITA", C_HI) + 8, 73, &FONT_S, "H\xDCGELTON", C_AMB);   /* Latin-1 U-umlaut */
            cv_text(cv_text(4, 86, &FONT_S, "+ SLOOP", C_HI) + 8, 86, &FONT_S, "ISOD89", C_AMB);
            cv_text(4, 99, &FONT_S, "GPL-3.0, NO WARRANTY", C_HI);
            {   /* the credits, on a card */
                static const char *const CR[7] = {"FM6: MSFA / DEXED (APACHE)", "PHASE: CRISPYZEBRA (GPL)",
                                                  "VOICE: REF. KLATTSCH (MIT)", "SAMPLES: VERSILIAN (CC0)",
                                                  "+ H\xDCGELTON SAMPLE PACK", "FONT: INTER TIGHT (OFL)",
                                                  "ICONS: FUKIAI (MIT)"};
                cv_card(2, 117, 236, 92);
                for (i = 0; i < 7u; i++)
                    cv_text(10, 119 + (int32_t)i * 12, &FONT_S, CR[i], C_GRAY);
            }
        } else {
            static const char *const HINT[MI_COUNT] = {
                "", "ON: LESS BASS (SPEAKER)", "BUTTONS GLOW IN THE DARK", "KEYS GLOW TOO (WITH LIGHTS)",
                "SOUNDING NOTES LIGHT THEIR KEYS", "", "", "", "EVERY TRACK BACK TO START", ""};
            const char *hint = HINT[ui.menu_sel % MI_COUNT];
            for (i = 0; i < MI_COUNT; i++) {          /* a row card each, the selected one lit */
                int32_t y = 3 + (int32_t)i * MI_DY;
                int sel = i == ui.menu_sel;
                const char *v = i == MI_SPEAKER ? (settings.lowcut ? "LOW CUT ON" : "OFF") :
                                i == MI_LIGHTS ? LIGHTS_NAME[lights_lvl % LIGHTS_N] :
                                i == MI_KEYS ? KEYS_NAME[lights_keys % KEYS_N] :
                                i == MI_NOTES ? (lights_notes ? "ON" : "OFF") :
                                i == MI_USB ? (usb_full ? "FULL" : "MASTER") :
                                i == MI_MOUT ? (midi_seq_out ? "SEQ" : "KEYS") :
                                i == MI_MIN ? (midi_clk_only ? "CLOCK" : "NOTES") : 0;
                cv_rrect(4, y - 1, 232, 15, 5, sel ? C_SEL : C_SURF, C_BG);
                cv_text(14, y - 1, &FONT_S, MI_NAME[i], sel ? C_WHITE : C_GRAY);
                if (v)                                  /* (KEYS needs LIGHTS: gray while it is off) */
                    cv_text(110, y - 1, &FONT_S, v, i == MI_KEYS && !lights_lvl ? C_GRAY : C_HI);
                if (i == MI_COLOR) {
                    uint32_t k;
                    cv_text(90, y - 1, &FONT_S, PALETTES[settings.palette].name, C_HI);
                    for (k = 0; k < 5u; k++)
                        cv_rrect(160 + (int32_t)k * 14, y + 1, 11, 11, 2, pal[k], sel ? C_SEL : C_SURF);
                }
            }
            if (ui.menu_sel == MI_USB)
                hint = usb_full ? "FIXED LEVEL, NOT THE KNOB" : "FOLLOWS THE MASTER KNOB";
            if (ui.menu_sel == MI_MOUT)
                hint = midi_seq_out ? "KEYS + SEQUENCER + ARP" : "ONLY THE KEYS";
            if (ui.menu_sel == MI_MIN)
                hint = midi_clk_only ? "CLOCK ONLY, NO NOTES" : "NOTES AND CLOCK";
            if (ui.menu_sel == MI_NEW && menu_new_armed)
                hint = "OCT+ AGAIN: CLEAR ALL";
            cv_text(4, 3 + MI_COUNT * MI_DY, &FONT_S, hint[0] ? hint : "PRESETS MOVE   KNOB 1 SET",
                    menu_new_armed ? C_WHITE : hint[0] ? C_GRAY : C_DIM);
            cv_text(4, 3 + MI_COUNT * MI_DY + 15, &FONT_S, "OCT+ OK   OCT- BACK", C_DIM);
        }
        cv_oy = 0;
        cv_blit(0, H_HEAD + 1 + pass * 124u);
    }
}

static void enc_drop(void)                             /* knob turns nobody takes */
{
    uint32_t k;
    for (k = 0; k < NE; k++)
        panel_enc(k);
}

static void menu_close(void)
{
    if (song.playing)
        settings_later = 1;                            /* (saved once stopped: project.c autosave_tick) */
    else
        settings_save();                               /* palette, lights, panel table, if changed */
    ui.menu = 0;
    ui.force = 1;
    go_home();
}

/* menu: PRESETS moves, OCT+ confirms, OCT- cancels (ABOUT -> list -> close) */
static void menu_input(uint32_t pressed)
{
    int32_t s;
    uint32_t ok = (pressed >> panel.btn[B_OCTUP]) & 1u, back = (pressed >> panel.btn[B_OCTDN]) & 1u;
    if (back) {
        if (ui.menu == 2)
            ui.menu = 1, ui.force = 1;
        else
            menu_close();
        return;
    }
    if ((s = panel_enc(EN_PRESET)) != 0 && ui.menu == 1) {
        ui.menu_sel = (uint8_t)((ui.menu_sel + (s > 0 ? 1u : MI_COUNT - 1u)) % MI_COUNT);
        menu_new_armed = 0;
    }
    s = panel_enc(EN_K1);
    if (s != 0 && ui.menu == 1 && ui.menu_sel == MI_COLOR) {
        settings.palette = (settings.palette + (s > 0 ? 1u : NPALETTES - 1u)) % NPALETTES;
        palette_set(settings.palette);              /* (the menu signature redraws) */
    }
    if ((s != 0 || ok) && ui.menu == 1 && (ui.menu_sel == MI_NOTES || ui.menu_sel == MI_USB ||
                                           ui.menu_sel == MI_MOUT || ui.menu_sel == MI_MIN)) {
        /* KNOB 1: right = ON / FULL / SEQ / CLOCK, left = OFF / MASTER / KEYS / NOTES; OCT+ toggles */
        uint8_t *v = ui.menu_sel == MI_NOTES ? &lights_notes : ui.menu_sel == MI_USB ? &usb_full :
                     ui.menu_sel == MI_MOUT ? &midi_seq_out : &midi_clk_only;
        *v = (uint8_t)(s > 0 ? 1u : s < 0 ? 0u : !*v);
        ok = 0;
    }
    if ((s != 0 || ok) && ui.menu == 1 && (ui.menu_sel == MI_LIGHTS || ui.menu_sel == MI_KEYS)) {
        /* KNOB 1: brighter / more keys (stops at the ends); OCT+ steps round */
        uint8_t *v = ui.menu_sel == MI_LIGHTS ? &lights_lvl : &lights_keys;
        uint32_t n = ui.menu_sel == MI_LIGHTS ? LIGHTS_N : KEYS_N;
        if (s > 0 && *v + 1u < n)
            (*v)++;
        else if (s < 0 && *v > 0u)
            (*v)--;
        else if (!s)
            *v = (uint8_t)((*v + 1u) % n);
        if (ui.menu_sel == MI_KEYS && lights_keys && !lights_lvl)
            lights_lvl = LIGHTS_LOW;                   /* keys lit need a level: the lowest */
        ok = 0;
    }
    if ((s != 0 || ok) && ui.menu == 1 && ui.menu_sel == MI_SPEAKER) {
        /* KNOB 1: right = ON, left = OFF; OCT+ toggles */
        uint32_t *v = &settings.lowcut;
        *v = s > 0 ? 1u : s < 0 ? 0u : !*v;
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
        ok = 0;
    }
    if (ok && ui.menu == 1) {
        switch (ui.menu_sel) {
        case MI_COLOR:                                 /* OCT+ steps through the palettes too */
            settings.palette = (settings.palette + 1u) % NPALETTES;
            palette_set(settings.palette);
            break;
        case MI_NEW:                                   /* Jangada: OCT+ arms, OCT+ again: a new project */
            if (!menu_new_armed) {
                menu_new_armed = 1;
                break;
            }
            menu_new_armed = 0;
            menu_new_project();
            menu_close();
            ui_message("NEW PROJECT");
            break;
        case MI_ABOUT:
            ui.menu = 2;
            ui.force = 1;
            break;
        default:
            menu_close();
            break;
        }
    }
    enc_drop();                                        /* swallow the rest while the menu is up */
}

