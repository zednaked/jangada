/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI input: LEDs, knobs and buttons, SEQ step entry, panel setup. */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_REC};   /* button of each page family */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

static int layer_visible(void);                     /* ui_layers.c */
static uint32_t layer_now(void);
static uint32_t layers_leds(uint8_t *nl, uint8_t *br);
static uint32_t layers_key_glow(void);

/* Jangada (after SLOOP 2.3, @renebohne's NOTES): what sounds on track t, as keys (bit k = key k, 0 = F3).
 * A synth track: the voices still held, mapped back through the keyboard (octave, scale, chords). The
 * drum track: each hit lights its key ~100 ms */
static uint32_t keys_sounding(const track_t *t)
{
    static uint32_t seen, until[27];
    uint32_t i, k, m = 0;
    if (is_drum(t)) {
        for (i = 0; i < NDRUM; i++) {
            const voice_t *v = &drums.v[i];
            if (v->active && (int32_t)(v->age - seen) > 0)
                for (k = 0; k < 27u; k++)
                    if (DRUM_KEYS[k] == v->note)
                        until[k] = fm1_ms + 100u;
        }
        seen = drums.age;
        for (k = 0; k < 27u; k++)
            if ((int32_t)(until[k] - fm1_ms) > 0)
                m |= 1u << k;
        return m;
    }
    for (i = 0; i < NVOICE; i++) {
        const voice_t *v = &t->v[i];
        if (v->active && v->gate && v->stage <= 2u)
            for (k = 0; k < 27u; k++)
                if (kb_map(t, k) == v->note)
                    m |= 1u << k;
    }
    return m;
}

static uint32_t lights_keys_mask(void)             /* menu KEYS: the Cs, or every white key (with LIGHTS) */
{
    uint32_t k, m = 0;
    if (!lights_lvl || !lights_keys)
        return 0u;
    for (k = 0; k < 27u; k++) {
        uint32_t pc = (53u + k) % 12u;             /* key 0 = F3 (53) */
        if (lights_keys == KEYS_C ? pc == 0u : ((0xAB5u >> pc) & 1u) != 0u)   /* 0xAB5: C D E F G A B */
            m |= 1u << k;
    }
    return m;
}

/* three layers of light (fm1_input.h): lit (nl), the glow (dl: landmarks, NOTES under a layer's keys)
 * and the backlight (bl: menu LIGHTS, every button and the KEYS) */
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, dl[FM1_NCOL] = {0}, bl[FM1_NCOL] = {0}, br[FM1_NCOL] = {0};
    uint32_t k, c, keys, glow = 0, back, sounding = lights_notes ? keys_sounding(TSEL) : 0u;
    uint32_t fam = cur_fam();
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    led_put(nl, panel.btn[B_PLAY], song.playing && ((song.tick / 64u) & 1u) == 0u);   /* blinks: intended */
    led_put(nl, panel.btn[B_REC], song.rec != 0u);
    if (ui.confirm || (ui.menu && menu_new_armed)) {    /* a dialog: OCT- (back) lit, OCT+ (do it) breathes */
        led_put(nl, panel.btn[B_OCTDN], 1);
        led_put(br, panel.btn[B_OCTUP], 1);
    } else {
        led_put(nl, panel.btn[B_OCTDN], song.octave < 0);
        led_put(nl, panel.btn[B_OCTUP], song.octave > 0);
    }
    if (layer_visible()) {                              /* Jangada: a layer shows what its keys do */
        keys = layers_leds(nl, br);                     /* (its button; the keys it lights) */
        glow = (layers_key_glow() | sounding) & ~keys;  /* landmarks; NOTES: what sounds glows under the keys */
    } else {
        keys = fm1_in.notes | sounding;                 /* menu NOTES: what sounds lights its key */
    }
    back = lights_keys_mask() & ~keys & ~glow;
    for (k = 0; k < 27u; k++) {
        led_put(nl, 14u + k, (int)((keys >> k) & 1u));
        led_put(dl, 14u + k, (int)((glow >> k) & 1u));
        led_put(bl, 14u + k, (int)((back >> k) & 1u));
    }
    if (lights_lvl)                                     /* menu LIGHTS: every button glows, the lit ones full */
        for (k = 0; k < NB; k++)
            led_put(bl, panel.btn[k], 1);
    for (c = 0; c < FM1_NCOL; c++) {
        fm1_led[c] = nl[c];
        fm1_led_dim[c] = dl[c];
        fm1_led_bg[c] = (uint8_t)(bl[c] & ~nl[c]);
        fm1_led_breath[c] = (uint8_t)(br[c] & ~nl[c]);
    }
    fm1_led_bg_ns = LIGHTS_NS[lights_lvl % LIGHTS_N];
}

/* ---------------------------------------------------------- input --- */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    uint32_t now = fm1_ticks(), dt = now - ui.enc_t[role];
    ui.enc_t[role] = now;
    if (range > 40 && dt < 60u * 1000u * FM1_TICKS_PER_US)
        return s * (range > 150 ? 6 : 3);
    return s;
}

#include "ui_layers.c"          /* Jangada: hold FX / GLO = a layer (after SLOOP) */

/* TRACKS page: KNOB 1 TRACK, 2 LEVEL (0 = mute; the drum track: GLO > DRUMS LEVEL),
 * 3 LEN of its pattern, 4 PAN. A track muted with MUTE (VOICE 2, the editor): the first
 * turn of KNOB 2 unmutes it (the drum track has no VOICE 2 page to do that) */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d;
    switch (slot) {
    case 0:                                               /* Jangada: TYPE (ALGORITHM picks the track) */
        if (song.sel == TRK_DRUM)
            song.g[G_T4] = (int16_t)(steps > 0);         /* right SYNTH, left DRUM: t4_follow does the rest */
        else
            ui_message("TRACKS 1-3: SYNTH");
        return;
    case 1:
        if (t->p[P_MUTE]) {
            t->p[P_MUTE] = 0;
            return;
        }
        vp = is_drum(t) ? &song.g[G_DRLVL] : &t->p[P_LEVEL];
        d = is_drum(t) ? &GP[G_DRLVL] : &TP[P_LEVEL];
        break;
    case 2:
        vp = &t->p[P_SLEN];
        d = &TP[P_SLEN];
        break;
    default:
        vp = &t->p[P_PAN];
        d = &TP[P_PAN];
        break;
    }
    *vp = (int16_t)clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
}

/* REC tap on TRACKS: arm / disarm live recording on the selected track; arming while
 * stopped starts the transport too */
static void tracks_rec_tap(void)
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    song.rec ^= bit;
    if ((song.rec & bit) && !song.playing)
        transport_req = 1;
}

static void step_edit(uint32_t slot, int32_t steps)
{
    step_t *st = &TSEL->step[ui.cursor];
    uint32_t i, id = cur_page()->id[slot];
    switch (id) {
    case 0:                                               /* STEP: the cursor */
        cursor_set(ui.cursor + steps);
        break;
    case 1:                                               /* NOTE: transpose the step */
        if (!st->n) {
            st->note[0] = last_note;
            st->n = 1;
            st->time = ST_NOTE;
            break;
        }
        for (i = 0; i < st->n; i++)
            st->note[i] = (uint8_t)clamp(st->note[i] + steps, 1, 127);
        st->time = ST_NOTE;
        last_note = st->note[0];
        break;
    case 2:
        st->time = (uint8_t)clamp((int32_t)st->time + (steps > 0 ? 1 : -1), ST_NOTE, ST_REST);
        break;
    case 4:                                               /* RTCH: x1..x4 (Jangada) */
    case 5: {                                             /* CHNC: 100..25 % (Jangada) */
        uint32_t sh = id == 4u ? SF_RATCH_SH : SF_CHANCE_SH, m = 3u << sh;
        int32_t v = (int32_t)((st->flags & m) >> sh) + (steps > 0 ? 1 : -1);
        st->flags = (uint8_t)((st->flags & ~m) | ((uint32_t)clamp(v, 0, 3) << sh));
        break;
    }
    case 0xFF:
        break;
    default: {                                            /* FLAG: - / ACC / SLD / A+S */
        uint32_t f = (st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u);
        f = (uint32_t)clamp((int32_t)f + (steps > 0 ? 1 : -1), 0, 3);
        st->flags = (uint8_t)((st->flags & ~(SF_ACCENT | SF_SLIDE)) | (f & 1u ? SF_ACCENT : 0u) | (f & 2u ? SF_SLIDE : 0u));
        break;
    }
    }
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    uint32_t id = pg->id[slot];
    int32_t v;
    if (is_drum(TSEL) && !page_for_drum(pg))
        return;                                           /* "DRUM TRACK": nothing to edit here */
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_BROWSE) {                         /* KNOB 1: one preset, KNOB 2 (Jangada 0.8.2): the category
                                                           * (ENG was here: KIND does what it did, and reaches the FM6
                                                           * banks and the user presets, and keeps to the category),
                                                           * KNOB 3 (Jangada 0.6, after SLOOP): the next / previous
                                                           * group (an engine's presets, an FM6 bank, the user presets) */
        if (slot == 0u && !is_drum(TSEL)) {
            uint32_t total, cur = preset_pos(&total);
            if (total)
                preset_go(preset_step(cur, steps));      /* (the category filter, KNOB 2) */
        } else if (slot == 1u && !is_drum(TSEL)) {
            preset_cat_turn(steps);
        } else if (slot == 2u && !is_drum(TSEL)) {
            uint32_t total, cur = preset_pos(&total);
            if (total)
                preset_go(preset_group_jump(cur, steps));
        }
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 slot; LOAD / ERASE / SAVE: GO buttons */
        static const char *const UP_GO[3] = {"LOAD", "ERASE", "SAVE"};
        if (slot == 0u) {
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
            ui.arm = 0;
            return;
        }
        if (steps <= 0)
            return;
        if (ui.arm != 0xE0u + slot) {                     /* one detent arms, a second one within ~1.5 s acts */
            ui.arm = (uint8_t)(0xE0u + slot);
            ui.arm_t = 90;
            ui_say("AGAIN: ", UP_GO[slot - 1u]);
            return;
        }
        ui.arm = 0;
        up_ui(slot - 1u, ui.uslot);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    if (pg->scope == SC_GLOBAL && id == G_KIT) {          /* Jangada: the kits in the browser's order */
        drum_kit_set(drum_kit_step(drum_kit(), accel(EN_K1 + slot, steps, d->max - d->min), 0));
        return;
    }
    if (id == PG_BEAT) {                                  /* Jangada: a factory beat into the drum track */
        if (!is_drum(TDRUM)) {
            ui_message("TRACK 4: SYNTH");
            return;
        }
        if (!seq_replaceable(TDRUM) && ui.arm != PG_BEAT) {   /* the user's own pattern: a second turn */
            ui.arm = (uint8_t)PG_BEAT;
            ui.arm_t = 90;
            ui_say("AGAIN: ", d->label);
            return;
        }
        ui.arm = 0;
        v = clamp(*vp + (steps > 0 ? 1 : -1), 1, d->max);
        load_beat((uint32_t)v - 1u);
        ui_say("BEAT ", d->names[v]);
        return;
    }
    v = clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    *vp = (int16_t)v;
    if (pg->scope == SC_TRACK || pg->scope == SC_ENGINE)
        ml_knob(id);                                      /* MIDI LEARN: this parameter is picked */
    if (pg->scope == SC_TRACK && (id == P_VOICE || id == P_ALLOC))
        panic_req |= (uint8_t)(1u << song.sel);           /* Jangada: as a preset change: a POLY note on v[0] hung */
    if (pg->scope == SC_TRACK && id == P_CHORD)
        chord_poly(TSEL);
    if (!v)
        return;
    if (pg->scope == SC_GLOBAL && (id == G_LOAD || id == G_SAVE || id == G_CLRSEQ || id == G_INITSND) &&
        ui.arm != id) {                                   /* one detent arms, a second one within ~1.5 s acts */
        *vp = 0;
        ui.arm = (uint8_t)id;
        ui.arm_t = 90;
        ui_say("AGAIN: ", d->label);
        return;
    }
    ui.arm = 0;
    if (pg->scope != SC_GLOBAL)
        return;
    switch (id) {                                         /* GO buttons: act, then back to 0 */
    case G_LOAD:
        *vp = 0;
        project_load((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_SAVE:
        *vp = 0;
        project_save((uint32_t)song.g[G_SLOT] - 1u);
        break;
    case G_CLRSEQ:
        *vp = 0;
        fm1_irq_off();
        track_defaults_steps(TSEL);
        fm1_irq_on();
        ui_message("PATTERN CLEARED");
        break;
    case G_INITSND:
        *vp = 0;
        set_engine(TSEL->eng_req);                              /* engine defaults + its first preset */
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    default:
        break;
    }
}

/* SEQ step entry, acid style: the keys pressed together (POLY: up to 4, MONO:
 * the last one) become the cursor step; releasing all keys moves on */
static void seq_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        uint32_t note;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note == KB_SILENT)
            continue;
        if (!ui.entry_open) {
            ui.entry_open = 1;
            st->n = 0;
            st->time = ST_NOTE;
        }
        if (t->p[P_VOICE]) {
            st->note[0] = (uint8_t)note;
            st->n = 1;
        } else if (st->n < 4u) {
            st->note[st->n++] = (uint8_t)note;
        }
        last_note = (uint8_t)note;
    }
    if (ui.entry_open && !fm1_in.notes)
        cursor_set(ui.cursor + 1);
}

/* HOME / REC: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* Jangada 0.6: a button pressed while the top bar asks "FM6 BANK n? SAVE=YES" (fm6_sysex.c) answers it and does
 * nothing else: SAVE yes, OCT- / OCT+ bank 1 / 2 (the question stays), any other no. The press is no tap, no hold,
 * no layer */
static void fm6_ask_input(uint32_t *pressed)
{
    uint32_t dn = 1u << panel.btn[B_OCTDN], up = 1u << panel.btn[B_OCTUP];
    if (!fm6_ask.on || fm6_ask.answer || !*pressed)
        return;
    if (*pressed & (1u << panel.btn[B_SAVE]))
        fm6_ask.answer = 1;
    else if (!(*pressed & ~(dn | up))) {
        fm6_ask.bank = (*pressed & up) ? 1u : 0u;
        fm6_ask.ms = fm1_ms;                            /* (another 15 s) */
        ui.msg_t = 0;                                   /* (fm6_sysex.c says the question again) */
    } else
        fm6_ask.answer = 2;
    if (*pressed & (1u << panel.btn[B_HOME]))
        ui.home_t0 |= 2u;
    if (*pressed & (1u << panel.btn[B_REC]))
        ui.rec_t0 |= 2u;
    if (*pressed & (1u << panel.btn[B_ARP]))
        ui.arp_t0 |= 2u;
    *pressed = 0;
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k, fam = cur_fam();
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t rec = btn_hold(&ui.rec_t0, B_REC, now, !ui.menu && (fam == FAM_SEQ || fam == FAM_ARP || fam == FAM_TRK));
    uint32_t arp = btn_hold(&ui.arp_t0, B_ARP, now, !ui.menu);
    int32_t s;
    static int8_t punch_shown = -1;
    t4_follow();
    if (pressed || notes || fm1_in.buttons || fm1_in.notes)
        ui_input_ms = fm1_ms;                           /* Jangada: not idle (project.c autosave) */
    fm6_ask_input(&pressed);                            /* Jangada 0.6: "FM6 BANK n? SAVE=YES" */
    layers_input(&pressed, fm1_ms);                     /* Jangada: FX / GLO tap, hold, lock (ui_layers.c) */
    ml_poll();                                          /* MIDI LEARN: a CC came for the picked parameter */
    fm6_poll();                                         /* Jangada: FM6 PTCH turned -> its patch */
    /* Jangada: the punch-in effect's name on screen when one starts (ui_layers.c: the FX layer) */
    if (punch.req != punch_shown) {
        punch_shown = punch.req;
        if (punch_shown >= 0)
            ui_message(PUNCH_NAME[punch_shown]);
    }
    if (arp == BT_TAP && !ui.menu && !ui.confirm)       /* Jangada: ARP acts on release, as HOME: a tap */
        open_family(FAM_ARP);                           /* opens its page, a hold (below) does not */
    if (arp == BT_HOLD) {                               /* Jangada: ARP held: every latched (HOLD) chord off */
        uint32_t i, any = 0;
        for (i = 0; i < NTRK; i++)
            if (trk_synth(i) && trk[i].p[P_AHOLD] && trk[i].nheld && !trk[i].arp_phys)
                any |= 1u << i;
        if (any) {
            latch_off_req |= (uint8_t)any;
            ui_message("DRONE OFF");                    /* it fades with its release; */
        } else {
            for (i = 0; i < NTRK && trk_synth(i); i++)  /* held again: the tails stop now */
                for (k = 0; k < NVOICE; k++)
                    if (trk[i].v[k].active && !trk[i].v[k].gate)
                        any |= 1u << i;
            hush_req |= (uint8_t)any;
            ui_message(any ? "SILENCE" : "NO DRONE");
        }
    }
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;                             /* (a clear dialog is cancelled) */
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (ui.menu) {                                      /* HOME / REC taps do nothing here */
        if (ui.rec_t0)
            ui.rec_t0 |= 2u;                            /* a REC press in the menu is no tap later */
        if (!ui.home_t0)
            menu_input(pressed);
        return;
    }
    if (rec == BT_HOLD) {                               /* REC held on SEQ / ARP: "clear the sequence?", */
        ui.confirm = fam == FAM_TRK ? 2 : 1;            /* on TRACKS "clear track n?" (the selected one) */
        ui.confirm_trk = song.sel;
        ui.force = 1;
    } else if (rec == BT_TAP && !ui.confirm) {          /* REC records what the page shows: */
        if (fam == FAM_TRK)
            tracks_rec_tap();                           /* TRACKS: arm the selected track (PLAY too) */
        else if (fam == FAM_SEQ || fam == FAM_ARP)
            song.rec ^= (uint8_t)(1u << song.sel);      /* SEQ / ARP: arm live recording */
        else
            open_family(FAM_TRK);                       /* nothing to record here: the TRACKS page */
    }
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ clears; nothing else reacts */
        if ((pressed >> panel.btn[B_OCTUP]) & 1u) {
            track_t *t = &trk[ui.confirm_trk % NTRK];
            fm1_irq_off();
            track_defaults_steps(t);
            fm1_irq_on();
            t->nheld = 0;                               /* and the latched arp chord */
            t->arp_phys = 0;
            ui.force = 1;
            if (ui.confirm == 2) {
                char b[12] = "1 CLEARED";
                b[0] = (char)('1' + ui.confirm_trk);
                ui_say("TRACK ", b);
            } else {
                ui_message("SEQUENCE CLEARED");
            }
            ui.confirm = 0;
        } else if ((pressed >> panel.btn[B_OCTDN]) & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        enc_drop();
        return;
    }
    if (home == BT_TAP) {                               /* HOME acts on release: a hold opens the menu */
        if (vis_shown()) {                              /* 0.7: the visualiser shown: HOME, as from any page */
            vis_on = 0;
            go_home();
        } else if (!ui.home && cur_page()->scope == SC_TRK) {
            vis_open();                                 /* on the TRACKS screen: the visualiser (ui_vis.c) */
        } else {
            go_home();
        }
    }
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            transport_req = song.playing ? 2 : 1;
            break;
        case B_REC:                                     /* tap / hold: above */
        case B_ARP:                                     /* Jangada: tap / hold, above */
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both;
            if (b == B_OCTDN && ml_oct_down())          /* MIDI LEARN: OCT- clears the picked one's CC */
                break;
            both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            break;
        }
        case B_EDIT:
            if (song.seq_mode && cur_page()->scope == SC_STEP) {   /* STEP page: EDIT clears the step */
                step_clear(&TSEL->step[ui.cursor]);
                stepx_clear(TSEL, ui.cursor);
                cursor_set(ui.cursor + 1);
                ui_message("STEP CLEARED");
                break;
            }
            /* fall through */
        default: {                                      /* page family buttons (HOME, REC: above) */
            uint32_t f;
            for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
                if (FAM_BTN[f] == b)
                    open_family(f);
            break;
        }
        }
    }
    if (song.seq_mode && cur_page()->scope == SC_STEP && !layer_now())
        seq_entry(notes);

    if (layer_now() == LY_STEP && ly.held)
        steps_held_encs();                              /* SEQ with steps held: SELECT, ALGORITHM, PRESETS (ui_layers.c) */
    if ((s = panel_enc(EN_PRESET)) != 0 && (ui.home || cur_page()->graph == GR_BROWSE || cur_fam() == FAM_TRK)) {
        /* PRESETS browses the selected part's presets (all engines, the FM6 bank voices, then user presets) on
         * HOME, the PRESETS page and TRACKS only (the drum track: nothing):
         * elsewhere a stray turn would throw away the sound being edited. Jangada 0.6 (after SLOOP): HOME held
         * while turning goes a group at a time (an engine's presets, each FM6 bank, the user presets), as KNOB 3
         * on the PRESETS page, the group named in the top bar; that HOME press then opens nothing (no tap, no
         * menu) */
        uint32_t total, cur = preset_pos(&total);
        int kind = ((fm1_in.buttons >> panel.btn[B_HOME]) & 1u) && ui.home_t0;
        if (kind)
            ui.home_t0 |= 2u;                           /* (as a hold already taken: btn_hold) */
        if (is_drum(TSEL)) {                        /* Jangada: the drum track browses the kits */
            drum_kit_set(drum_kit_step(drum_kit(), s > 0 ? 1 : -1, 1));   /* the browser's order (DS_KIT_NAV) */
            ui_say("KIT ", DRUM_KIT_NAMES[drum_kit()]);
            ui.force = 1;
        } else if (total && kind) {
            uint32_t n = preset_group_jump(cur, s);
            preset_go(n);
            ui_message(preset_kind_long(n));
        } else if (total)
            preset_go(preset_step(cur, s));             /* past the factory ones: user presets (the filter: KNOB 2) */
    }
    if ((s = panel_enc(EN_ALGO)) != 0)             /* ALGORITHM: the selected track, on every page */
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    if (vis_shown() && (s = panel_enc(EN_SELECT)) != 0)
        vis_select(s);                              /* the visualiser: SELECT its style */
    if ((s = panel_enc(EN_SELECT)) != 0) {          /* SELECT knob = global tempo */
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;                              /* the header's BPM lights up; no message over the header */
    }
    if (layer_now()) {                                  /* Jangada: a layer has the knobs */
        layers_knobs(layer_now());
        return;
    }
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            (pg->graph == GR_USER && k == 0u)) {     /* (not an empty column, nor "DRUM TRACK") */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            *vp = (int16_t)clamp(*vp + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max);
        } else {
            edit_param(k, s);
        }
    }
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 10, 240, &FONT_S, "HARDWARE CALIBRATION", C_WHITE, 1);
    draw_text_box(0, 30, 240, &FONT_S, "TEACH EACH BUTTON AND KNOB", C_GRAY, 1);
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        draw_text_box(0, 80, 240, &FONT_S, "PRESS", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, B_NAME[i], C_WHITE, 1);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        draw_text_box(0, 80, 240, &FONT_S, "TURN RIGHT", C_GRAY, 1);
        draw_text_box(0, 100, 240, &FONT_L, E_NAME[i], C_WHITE, 1);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, C_BLACK);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}

