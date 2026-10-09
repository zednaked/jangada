/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: LAYERS, after SLOOP 2.2 (isod89/sloop-fm1, GPL-3.0; teenage-engineering style). Hold a
 * function button: the 16 white keys and KNOB 1..4 change job while it is held, and after SHOW_MS the
 * screen shows the keys as 16 tiles (4 x 4) and the knobs as dials. Tapped (let go within TAP_MS,
 * nothing touched) the button opens its pages as before.
 *   FX    the 16 punch-in effects (punch.c, run by seq.c keyboard_block)   knobs: FILTER DUST DUCK TAPE
 *   GLO   keys 1..4 mute, 5..8 solo, 9 held: a fill, 10: the next bar a fill (after SLOOP 2.4), 14: MIDI
 *         LEARN on / off (0.9, midi_learn.c), the last white key: tap tempo                                             knobs: the levels of tracks 1..4
 *   SEQ   the 16 steps of the page (Elektron style): an empty step is set at once with the note played
 *         last, a set one is cleared when its key is let go, unless a knob edited it meanwhile; the
 *         first four black keys pick the page (steps 1-16 .. 49-64)
 *         knobs, no step held: NOTE (the pen)  DIV  SWING  LEN;  steps held: NOTE  RTCH  CHNC  FLAG
 *         and (after SLOOP 2.4) SELECT nudges them (1/64 of a step early / late), ALGORITHM picks a sound
 *         parameter and PRESETS locks it on them (its value on those steps only), OCT+ their condition
 *         (ALWAYS, FILL: only in a fill, NO FILL: never in one), OCT- takes their locks and nudge away
 *         black keys from D#4: SHIFT < >, LEN 1/2 x2, TRN - +, F#5 held = ERASE (playing: the steps the
 *         playhead passes; stopped: the whole pattern). OCT- / OCT+ while SEQ is held: undo / redo
 *   EDIT  keys 1..10: the engine of the track (and its first preset); the last key: track 4 DRUM / SYNTH
 *         knobs: PRESET (of the engine)  VOICE  GLIDE  LEVEL
 *   SCL   any key: the key of the song (ROOT of every synth track)
 *         knobs: CHORD (the track: one key plays a chord of the scale)  SCALE (every synth track)  QNT  TRN
 * HOME tapped while a layer button is held locks the layer open (both hands free); any other button
 * lets it go and does only that, PLAY, REC and OCT- / OCT+ keep working inside it.
 * Colours: the palette's (CHOQUE by default), white for what is on. */
#define TAP_MS 450u                                     /* a press shorter than this, untouched: a tap */
#define SHOW_MS 140u                                    /* the layer shows after this (a tap does not flash it) */
static const uint8_t LAYER_BTN[LY_COUNT] = {NB, B_FX, B_GLO, B_SEQ, B_SCL, B_EDIT};
static const char *const LAYER_NAME[LY_COUNT] = {"", "PUNCH", "MIX", "STEPS", "KEY", "ENGINE"};

static struct {
    uint8_t btn;                 /* the layer whose button is held (0 none) */
    uint8_t lock;                /* the layer locked open by HOME (0 none) */
    uint8_t used;                /* a key or a knob was touched while the button was held: no tap */
    uint8_t shown;               /* the screen holds a layer */
    uint32_t t0;                 /* the press, fm1_ms */
    uint8_t tap_n;               /* tap tempo */
    uint32_t tap_ms[4];
    uint32_t head, tiles, foot;  /* drawn-state signatures */
    uint8_t klay[27];            /* the layer each key went down in (its key-up goes there) */
    uint8_t page;                /* SEQ: steps page * 16 .. */
    uint16_t held;               /* SEQ: step keys down (white key bits) */
    uint16_t pend_off;           /* SEQ: steps that clear when their key is let go */
    uint8_t erase;               /* SEQ: the ERASE key is down */
    uint8_t snap;                /* SEQ: this hold of SEQ took its undo snapshot */
    uint8_t lkp;                 /* SEQ: the parameter ALGORITHM picked for the locks (P_*) */
    struct {                     /* SEQ: one level of undo of a track's pattern */
        uint8_t valid, undone, trk;
        int16_t len;
        step_t step[NSTEP];
        seqx_t x;                /* (its nudges, conditions and locks) */
    } undo;
} ly;

static uint32_t layer_now(void) { return ly.lock ? ly.lock : ly.btn; }
#include "ui_vis.c"           /* Jangada 0.7: the full-screen visualiser on the TRACKS screen (layers_draw) */
static int layer_visible(void) { return layer_now() && (ly.lock || fm1_ms - ly.t0 >= SHOW_MS); }
static uint32_t key_of_white(uint32_t w)                /* white key 0..15 (from the lowest F) -> key index */
{
    static const uint8_t OFF[7] = {0, 2, 4, 6, 7, 9, 11};
    return (w / 7u) * 12u + OFF[w % 7u];
}
static int16_t *trk_level_p(uint32_t i)                 /* the track's LEVEL (the drum track: GLO > DRUMS) */
{
    return is_drum(&trk[i]) ? &song.g[G_DRLVL] : &trk[i].p[P_LEVEL];
}

/* --------------------------------------------------------------- GLO --- */
static void tap_tempo(uint32_t now)
{
    uint32_t i, n, sum = 0;
    if (ly.tap_n && now - ly.tap_ms[(ly.tap_n - 1u) & 3u] > 2000u)
        ly.tap_n = 0;                                   /* a pause: a new count */
    ly.tap_ms[ly.tap_n & 3u] = now;
    ly.tap_n++;
    if (ly.tap_n < 2u)
        return;
    n = ly.tap_n - 1u > 3u ? 3u : ly.tap_n - 1u;
    for (i = 0; i < n; i++)
        sum += ly.tap_ms[(ly.tap_n - 1u - i) & 3u] - ly.tap_ms[(ly.tap_n - 2u - i) & 3u];
    if (sum) {
        song.g[G_BPM] = (int16_t)clamp((int32_t)((60000u * n + sum / 2u) / sum), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
}

/* --------------------------------------------------------------- SEQ --- */
static uint32_t trk_len(const track_t *t) { return t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u; }

/* undo: the pattern as it was before this hold of SEQ changed it (one level); undo and redo swap it */
static void undo_mark(void)
{
    if (ly.snap)
        return;
    ly.snap = 1;
    ly.undo.valid = 1;
    ly.undo.undone = 0;
    ly.undo.trk = song.sel;
    ly.undo.len = TSEL->p[P_SLEN];
    memcpy(ly.undo.step, TSEL->step, sizeof ly.undo.step);
    ly.undo.x = TSEL->x;
}

static void undo_swap(int redo)
{
    track_t *t = &trk[ly.undo.trk % NTRK];
    static step_t tmp[NSTEP];
    seqx_t tx;
    int16_t len;
    if (!ly.undo.valid || ly.undo.undone != (uint8_t)redo) {
        ui_message(redo ? "NOTHING TO REDO" : "NOTHING TO UNDO");
        return;
    }
    fm1_irq_off();
    memcpy(tmp, t->step, sizeof tmp);
    memcpy(t->step, ly.undo.step, sizeof tmp);
    memcpy(ly.undo.step, tmp, sizeof tmp);
    tx = t->x;
    t->x = ly.undo.x;
    ly.undo.x = tx;
    len = t->p[P_SLEN];
    t->p[P_SLEN] = ly.undo.len;
    ly.undo.len = len;
    fm1_irq_on();
    ly.undo.undone = (uint8_t)!redo;
    ly.snap = 0;                                        /* (the next edit: a new snapshot; no redo after it) */
    ui_message(redo ? "REDO" : "UNDO");
}

/* the pattern tools (black keys of the SEQ layer) */
static void pattern_tool(uint32_t tool)
{
    track_t *t = TSEL;
    uint32_t len = trk_len(t), i, j;
    step_t keep;
    char b[8];
    undo_mark();
    fm1_irq_off();
    switch (tool) {
    case 0:                                             /* SHIFT <: every step one earlier */
    case 1: {                                           /* SHIFT >: every step one later */
        uint8_t kx;
        if (tool == 0u) {
            keep = t->step[0], kx = t->x.sx[0];
            for (i = 0; i + 1u < len; i++)
                t->step[i] = t->step[i + 1u], t->x.sx[i] = t->x.sx[i + 1u];
            t->step[len - 1u] = keep, t->x.sx[len - 1u] = kx;
        } else {
            keep = t->step[len - 1u], kx = t->x.sx[len - 1u];
            for (i = len - 1u; i > 0; i--)
                t->step[i] = t->step[i - 1u], t->x.sx[i] = t->x.sx[i - 1u];
            t->step[0] = keep, t->x.sx[0] = kx;
        }
        for (i = 0; i < NLOCK; i++) {                   /* the locks move with their steps (step + 1 stored) */
            plock_t *l = &t->x.lock[i];
            if (l->step == LOCK_FREE || l->step > len)
                continue;
            l->step = (uint8_t)((l->step - 1u + (tool == 0u ? len - 1u : 1u)) % len + 1u);
        }
        break;
    }
    case 2:                                             /* LEN 1/2 */
        if (len >= 2u)
            t->p[P_SLEN] = (int16_t)(len / 2u);
        break;
    case 3:                                             /* LEN x2: the pattern again after itself */
        if (len * 2u <= NSTEP) {
            for (i = 0; i < len; i++)
                t->step[len + i] = t->step[i], t->x.sx[len + i] = t->x.sx[i];
            for (i = 0; i < NLOCK; i++) {               /* the locks again, while there are free slots */
                plock_t l = t->x.lock[i];
                if (l.step != LOCK_FREE && l.step <= len)
                    lock_set(t, l.step - 1u + len, l.param, l.val);
            }
            t->p[P_SLEN] = (int16_t)(len * 2u);
        }
        break;
    default:                                            /* TRN - / +: every note a semitone */
        if (!is_drum(t))
            for (i = 0; i < NSTEP; i++)
                for (j = 0; j < t->step[i].n && j < 4u; j++)
                    t->step[i].note[j] = (uint8_t)clamp(t->step[i].note[j] + (tool == 4u ? -1 : 1), 1, 127);
        break;
    }
    fm1_irq_on();
    if (ly.page * 16u >= trk_len(t))
        ly.page = (uint8_t)((trk_len(t) - 1u) / 16u);
    if (tool == 2u || tool == 3u) {
        fmt_int(b, t->p[P_SLEN]);
        ui_say("STEPS ", b);
    } else {
        static const char *const M[6] = {"SHIFT <", "SHIFT >", "", "", "TRANSPOSE -", "TRANSPOSE +"};
        ui_message(M[tool]);
    }
}

/* ERASE held: playing, the step under the playhead goes as it passes; stopped, the whole pattern */
static void erase_tick(void)
{
    track_t *t = TSEL;
    if (!ly.erase || !song.playing)
        return;
    if (t->seq_idx < trk_len(t) && t->step[t->seq_idx].n) {
        undo_mark();
        fm1_irq_off();
        step_clear(&t->step[t->seq_idx]);
        stepx_clear(t, t->seq_idx);
        fm1_irq_on();
    }
}

static void step_down(uint32_t w)
{
    track_t *t = TSEL;
    uint32_t idx = ly.page * 16u + w;
    step_t *st = &t->step[idx];
    if (idx >= trk_len(t))
        return;
    if (step_on(st)) {                                  /* set: cleared when let go (unless edited) */
        ly.pend_off |= (uint16_t)(1u << w);
        return;
    }
    undo_mark();
    fm1_irq_off();
    st->note[0] = last_note;
    st->n = 1;
    st->time = ST_NOTE;
    st->flags = 0;
    st->vel = 100;
    fm1_irq_on();
}

static void step_up(uint32_t w)
{
    track_t *t = TSEL;
    uint32_t idx = ly.page * 16u + w;
    if (!((ly.pend_off >> w) & 1u))
        return;
    ly.pend_off &= (uint16_t)~(1u << w);
    if (idx < trk_len(t)) {
        undo_mark();
        fm1_irq_off();
        step_clear(&t->step[idx]);
        stepx_clear(t, idx);
        fm1_irq_on();
    }
}

/* a knob with step keys held: k 0 NOTE, 1 RTCH, 2 CHNC, 3 FLAG (- ACC SLD A+S), on every held step */
static void steps_held_edit(uint32_t k, int32_t s)
{
    track_t *t = TSEL;
    uint32_t w, i;
    ly.pend_off &= (uint16_t)~ly.held;                  /* edited: kept when let go */
    undo_mark();
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ly.page * 16u + w;
        step_t *st = &t->step[idx];
        if (!((ly.held >> w) & 1u) || idx >= trk_len(t) || !step_on(st))
            continue;
        if (k == 0u) {
            for (i = 0; i < st->n; i++)
                st->note[i] = (uint8_t)clamp(st->note[i] + s, 1, 127);
            last_note = st->note[0];
        } else {
            uint32_t sh = k == 1u ? SF_RATCH_SH : k == 2u ? SF_CHANCE_SH : 0u, m = 3u << sh;
            int32_t v = (int32_t)((st->flags & m) >> sh) + (s > 0 ? 1 : -1);
            st->flags = (uint8_t)((st->flags & ~m) | ((uint32_t)clamp(v, 0, 3) << sh));
        }
    }
    fm1_irq_on();
}


/* ---- nudges, conditions, locks of the held steps (after SLOOP 2.4) ---- */
/* the order ALGORITHM walks the parameters in: the engine's first, then the sound's (P_* order), the rest */
static uint32_t lock_order(uint32_t i) { return i < NEDIT ? P_E0 + i : i < NEDIT + P_E0 ? i - NEDIT : i; }
static uint32_t lock_pos(uint32_t id) { return id >= P_E0 && id < P_E0 + NEDIT ? id - P_E0 : id < P_E0 ? id + NEDIT : id; }
static uint32_t lock_pick(const track_t *t, uint32_t id, int32_t d)   /* the next lockable one that way */
{
    uint32_t i, pos = lock_pos(id % P_COUNT);
    for (i = 0; i < P_COUNT; i++) {
        pos = (pos + (d > 0 ? 1u : P_COUNT - 1u)) % P_COUNT;
        if (p_lockable(t, lock_order(pos)))
            return lock_order(pos);
    }
    return P_COUNT;
}
static uint32_t lock_cur(const track_t *t)             /* the picked parameter (a lockable one), P_COUNT none */
{
    if (!p_lockable(t, ly.lkp))
        ly.lkp = (uint8_t)lock_pick(t, P_COUNT - 1u, 1);
    return ly.lkp;
}
static int32_t held_first(void)                         /* the first held step on the page, -1 none */
{
    uint32_t w;
    for (w = 0; w < 16u; w++)
        if (((ly.held >> w) & 1u) && ly.page * 16u + w < trk_len(TSEL))
            return (int32_t)(ly.page * 16u + w);
    return -1;
}
static void held_touch(void)                            /* edited: the held steps stay when let go */
{
    ly.used = 1;
    ly.pend_off &= (uint16_t)~ly.held;
    undo_mark();
}
/* SELECT, ALGORITHM and PRESETS with steps held (ui_input, before they do their own jobs) */
static void steps_held_encs(void)
{
    track_t *t = TSEL;
    uint32_t w, id;
    int32_t s;
    if ((s = panel_enc(EN_ALGO)) != 0 && (id = lock_cur(t)) < P_COUNT) {
        ly.used = 1;
        ly.lkp = (uint8_t)lock_pick(t, id, s);
    }
    if ((s = panel_enc(EN_PRESET)) != 0 && (id = lock_cur(t)) < P_COUNT) {
        const param_desc_t *d = lock_desc(t, id);
        int32_t st = accel(EN_PRESET, s, d->max - d->min);
        int full = 0;
        held_touch();
        fm1_irq_off();
        for (w = 0; w < 16u; w++) {
            uint32_t idx = ly.page * 16u + w;
            int k;
            if (!((ly.held >> w) & 1u) || idx >= trk_len(t))
                continue;
            k = lock_find(t, idx, id, 0);
            if (!lock_set(t, idx, id, (k >= 0 ? t->x.lock[k].val : p_unlocked(t, id)) + st))
                full = 1;
        }
        fm1_irq_on();
        if (full)
            ui_message("NO LOCK LEFT");
    }
    if ((s = panel_enc(EN_SELECT)) != 0) {              /* the nudge, 1/64 of a step a detent */
        held_touch();
        fm1_irq_off();
        for (w = 0; w < 16u; w++) {
            uint32_t idx = ly.page * 16u + w;
            if (((ly.held >> w) & 1u) && idx < trk_len(t))
                step_micro_set(t, idx, step_micro(t, idx) + s);
        }
        fm1_irq_on();
    }
}
/* OCT+ / OCT- with steps held: the next condition; their locks and nudge away */
static void steps_held_oct(int up)
{
    static const char *const FCN[3] = {"ALWAYS", "FILL ONLY", "NO FILL"};
    track_t *t = TSEL;
    int32_t f = held_first();
    uint32_t w, c = f >= 0 ? (step_cond(t, (uint32_t)f) + 1u) % 3u : 0u;
    held_touch();
    fm1_irq_off();
    for (w = 0; w < 16u; w++) {
        uint32_t idx = ly.page * 16u + w;
        if (!((ly.held >> w) & 1u) || idx >= trk_len(t))
            continue;
        if (up) {
            step_cond_set(t, idx, c);
        } else {
            lock_del(t, idx, P_COUNT);
            step_micro_set(t, idx, 0);
        }
    }
    fm1_irq_on();
    ui_message(up ? FCN[c] : "LOCKS CLEARED");
}
/* the label of a lockable parameter, with its group where the short one is ambiguous */
static void lock_label(const track_t *t, uint32_t id, char *b)
{
    const char *pre = id >= P_ATK && id <= P_REL ? "ENV " : id >= P_ED_FLT && id <= P_ED_SHP ? "ENV>" :
                      id >= P_LRATE && id <= P_LFADE ? "LFO " : id >= P_LD_PIT && id <= P_LD_AMP ? "LFO>" :
                      id == P_M1AMT ? "MOD1 " : id == P_M2AMT ? "MOD2 " : id == P_M3AMT ? "MOD3 " : id == P_M4AMT ? "MOD4 " :
                      id == P_DRING ? "RING " : "";
    str_cpy(b, pre, 12);
    str_cpy(b + str_len(b), lock_desc(t, id)->label, 12 - str_len(b));
}

/* a key of the layer (seq.c lk_q): k the key index, down / up, now its time */
static void layer_key(uint32_t layer, uint32_t k, uint32_t down, uint32_t now)
{
    int32_t w = punch_key(k);                           /* white key 0..15, -1 black */
    if (layer == LY_STEP) {
        if (w < 0) {                                    /* black keys: pages 1..4, then the tools */
            static const int8_t PG[12] = {-1, 0, -1, 1, -1, 2, -1, -1, 3, -1, -1, -1};
            if (k == 25u) {                             /* F#5: ERASE while held */
                ly.erase = (uint8_t)down;
                if (down && !song.playing) {
                    uint32_t i;
                    undo_mark();
                    fm1_irq_off();
                    for (i = 0; i < NSTEP; i++)
                        step_clear(&TSEL->step[i]);
                    seqx_clear(TSEL);
                    fm1_irq_on();
                    ui_message("PATTERN ERASED");
                }
                return;
            }
            if (!down)
                return;
            if (k < 12u && PG[k] >= 0) {
                if ((uint32_t)PG[k] * 16u < trk_len(TSEL))
                    ly.page = (uint8_t)PG[k];
            } else {                                    /* D#4 F#4 G#4 A#4 C#5 D#5: the tools 0..5 */
                static const uint8_t TOOL_KEY[6] = {10, 13, 15, 17, 20, 22};
                uint32_t i;
                for (i = 0; i < 6u; i++)
                    if (TOOL_KEY[i] == k)
                        pattern_tool(i);
            }
            return;
        }
        if (down) {
            uint32_t k, idx = ly.page * 16u + (uint32_t)w;
            if (!ly.held)                               /* (the first step held: ALGORITHM starts at its lock) */
                for (k = 0; k < NLOCK; k++)
                    if (TSEL->x.lock[k].step == idx + 1u) {
                        ly.lkp = TSEL->x.lock[k].param;
                        break;
                    }
            ly.held |= (uint16_t)(1u << w);
            step_down((uint32_t)w);
        } else {
            ly.held &= (uint16_t)~(1u << w);
            step_up((uint32_t)w);
        }
        return;
    }
    if (layer == LY_ENGINE) {                           /* keys 1..NENGINES: the engine; the last: T4 type */
        if (!down || w < 0)
            return;
        if (w == 15) {
            song.g[G_T4] = (int16_t)!song.g[G_T4];      /* ui.c t4_follow does the rest */
        } else if ((uint32_t)w < NENGINES) {
            if (is_drum(TSEL))
                ui_message("DRUM TRACK");
            else
                select_engine((uint32_t)w);
        }
        return;
    }
    if (layer == LY_SCALE) {                            /* any key: the key of the song, every synth track */
        uint32_t i, root = (53u + k) % 12u;
        if (!down)
            return;
        for (i = 0; i < NTRK; i++)
            if (trk_synth(i))
                trk[i].p[P_ROOT] = (int16_t)root;
        ui_say("KEY ", N_NOTE[root]);
        return;
    }
    if (layer == LY_MIX && w == 8) {                    /* key 9: a fill while held (seq.c step_plays) */
        fill_held = (uint8_t)down;
        return;
    }
    if (layer != LY_MIX || w < 0 || !down)
        return;
    if (w == 9) {                                       /* key 10: the next bar is a fill (again: not) */
        fill_arm = (uint8_t)(!fill_arm && song.playing);
        if (!song.playing)
            ui_message("PLAY FIRST");
        return;
    }
    if (w < 4) {
        trk[w].p[P_MUTE] = (int16_t)!trk[w].p[P_MUTE];
    } else if (w < 8) {
        song.solo ^= (uint8_t)(1u << (w - 4));
    } else if (w == 13) {
        ml_toggle();                                    /* key 14: MIDI LEARN on / off (midi_learn.c) */
    } else if (w == 15) {
        tap_tempo(now);
    }
}

/* the layer buttons: tap / hold / lock. pressed: this frame's press edges; the ones handled here are
 * taken out of it. Returns 1 when HOME's press belongs to the layer (it locked or let go a lock). */
static uint32_t fam_of_btn(uint32_t b)
{
    uint32_t f;
    for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
        if (FAM_BTN[f] == b)
            return f;
    return FAM_HOME;
}

static void layers_input(uint32_t *pressed, uint32_t now)
{
    uint32_t l, id, bit;
    const uint32_t keep = (1u << panel.btn[B_PLAY]) | (1u << panel.btn[B_REC]) | (1u << panel.btn[B_OCTDN]) |
                          (1u << panel.btn[B_OCTUP]);
    if (ui.menu || ui.confirm) {                        /* no layer over the menu or a dialog */
        ly.btn = ly.lock = 0;
        kb_layer = 0;
        return;
    }
    if (ly.lock) {                                      /* locked: another button lets it go, and does only that */
        uint32_t other = *pressed & ~keep;
        if (other) {
            ly.lock = 0;
            ly.btn = 0;
            *pressed &= ~other;
            if ((other >> panel.btn[B_HOME]) & 1u)
                ui.home_t0 |= 2u;                       /* (no HOME tap on its release) */
            ui.force = 1;
        }
    }
    for (l = LY_FX; l < LY_COUNT; l++) {               /* a layer button pressed: never acts on the press */
        bit = 1u << panel.btn[LAYER_BTN[l]];
        if (!(*pressed & bit))
            continue;
        *pressed &= ~bit;
        ly.btn = (uint8_t)l;
        ly.t0 = now;
        ly.used = 0;
        ly.snap = 0;                                    /* SEQ: a new hold, a new undo step */
    }
    if (ly.btn && !((fm1_in.buttons >> panel.btn[LAYER_BTN[ly.btn]]) & 1u)) {   /* let go */
        if (!ly.used && !ly.lock && now - ly.t0 < TAP_MS) {   /* a tap: its pages */
            if (ly.btn == LY_ENGINE && song.seq_mode && cur_page()->scope == SC_STEP) {
                step_clear(&TSEL->step[ui.cursor]);     /* (EDIT on a STEP page: clears the step) */
                stepx_clear(TSEL, ui.cursor);
                cursor_set(ui.cursor + 1);
                ui_message("STEP CLEARED");
            } else {
                open_family(fam_of_btn(LAYER_BTN[ly.btn]));
            }
        }
        ly.btn = 0;
        ui.force = 1;
    }
    if (ly.btn) {
        if ((*pressed >> panel.btn[B_HOME]) & 1u) {     /* HOME while held: lock it open */
            ly.lock = ly.btn;
            ly.used = 1;
            *pressed &= ~(1u << panel.btn[B_HOME]);
            ui.home_t0 |= 2u;
            ui_message("LOCKED");
        }
        if (fm1_in.notes)
            ly.used = 1;
        for (id = 0; id < 14u; id++)                    /* other buttons while held: no tap either */
            if ((*pressed >> id) & 1u)
                ly.used = 1;
    }
    if (layer_now() == LY_STEP) {                       /* SEQ: OCT- undo, OCT+ redo (not the octave) */
        uint32_t dn = 1u << panel.btn[B_OCTDN], upb = 1u << panel.btn[B_OCTUP];
        if (*pressed & (dn | upb)) {
            if (ly.held)                                /* steps held: their condition / no locks */
                steps_held_oct((*pressed & upb) != 0u);
            else
                undo_swap((*pressed & upb) != 0u);
            *pressed &= ~(dn | upb);
            ly.used = 1;
        }
    }
    kb_layer = (uint8_t)layer_now();
    while (lk_r != lk_w) {                              /* the layer keys from the ISR */
        uint32_t v = lk_q[lk_r % LKQ], t = lk_t[lk_r % LKQ], k = (v & 0x7Fu) % 27u, up = (v & LK_UP) != 0u;
        lk_r++;
        if (!up)
            ly.klay[k] = (uint8_t)(v >> 8);
        layer_key(ly.klay[k], k, !up, t);
    }
    if (!kb_layer)
        ly.erase = 0;
    erase_tick();
}

/* KNOB 1..4 while a layer is held: what the layer gives them (the page does not see them) */
static void layers_knobs(uint32_t layer)
{
    uint32_t k;
    int32_t s;
    for (k = 0; k < 4u; k++) {
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        ly.used = 1;
        ui.hot_col = (uint8_t)k;
        ui.hot_t = 40;
        if (layer == LY_FX) {
            if (k == 0u)
                song.g[G_FILT] = (int16_t)clamp(song.g[G_FILT] + accel(EN_K1, s, 127), -64, 63);
            else if (k == 1u)
                song.g[G_DUST] = (int16_t)clamp(song.g[G_DUST] + accel(EN_K2, s, 127), 0, 127);
            else if (k == 2u)
                song.g[G_DUCK] = (int16_t)clamp(song.g[G_DUCK] + accel(EN_K3, s, 127), 0, 127);
            else                                        /* Jangada GRIT: the worn tape */
                song.g[G_TAPE] = (int16_t)clamp(song.g[G_TAPE] + accel(EN_K4, s, 127), 0, 127);
        } else if (layer == LY_MIX) {
            int16_t *lv = trk_level_p(k);
            *lv = (int16_t)clamp(*lv + accel(EN_K1 + k, s, 127), 0, 127);
        } else if (layer == LY_ENGINE) {
            track_t *t = TSEL;
            const engine_t *e = ENGINES[t->eng_req % NENGINES];
            if (is_drum(t)) {
                if (k == 3u)
                    song.g[G_DRLVL] = (int16_t)clamp(song.g[G_DRLVL] + accel(EN_K4, s, 127), 0, 127);
            } else if (k == 0u && e->npresets) {        /* the engine's presets */
                uint32_t n = e->npresets;
                apply_preset((t->preset + (s > 0 ? 1u : n - 1u)) % n);
                ui.force = 1;
            } else if (k == 1u) {
                t->p[P_VOICE] = (int16_t)clamp(t->p[P_VOICE] + (s > 0 ? 1 : -1), 0, 3);
                panic_req |= (uint8_t)(1u << song.sel);
            } else if (k == 2u) {
                const param_desc_t *d = &TP[P_GLIDE];
                t->p[P_GLIDE] = (int16_t)clamp(t->p[P_GLIDE] + accel(EN_K3, s, d->max - d->min), d->min, d->max);
            } else if (k == 3u) {
                t->p[P_LEVEL] = (int16_t)clamp(t->p[P_LEVEL] + accel(EN_K4, s, 127), 0, 127);
            }
        } else if (layer == LY_SCALE) {
            track_t *t = TSEL;
            if (k == 1u) {                              /* the scale: every synth track */
                uint32_t i;
                int16_t v = (int16_t)clamp(t->p[P_SCALE] + s, TP[P_SCALE].min, TP[P_SCALE].max);
                for (i = 0; i < NTRK; i++)
                    if (trk_synth(i))
                        trk[i].p[P_SCALE] = v;
            } else if (!is_drum(t)) {
                uint32_t id = k == 0u ? P_CHORD : k == 2u ? P_QUANT : P_TRANS;
                t->p[id] = (int16_t)clamp(t->p[id] + s, TP[id].min, TP[id].max);
                if (id == P_CHORD)
                    chord_poly(t);
            }
        } else if (layer == LY_STEP) {
            track_t *t = TSEL;
            if (ly.held) {
                steps_held_edit(k, s);
            } else if (k == 0u) {
                last_note = (uint8_t)clamp(last_note + s, 1, 127);   /* the pen: the note a new step gets */
            } else if (k == 1u) {
                t->p[P_SDIV] = (int16_t)clamp(t->p[P_SDIV] + s, TP[P_SDIV].min, TP[P_SDIV].max);
            } else if (k == 2u) {
                const param_desc_t *d = track_desc(t, P_SSWING);
                t->p[P_SSWING] = (int16_t)clamp(t->p[P_SSWING] + accel(EN_K3, s, d->max - d->min), d->min, d->max);
            } else {
                t->p[P_SLEN] = (int16_t)clamp(t->p[P_SLEN] + accel(EN_K4, s, 63), 1, NSTEP);
                if ((uint32_t)ly.page * 16u >= trk_len(t))
                    ly.page = (uint8_t)((trk_len(t) - 1u) / 16u);
            }
        }
    }
}

/* the key LEDs while a layer is shown: what is on (an effect, an unmuted track, a solo); the first key
 * of each row of FX tiles (1, 5, 9, 13) glows dimly (layers_key_glow) to find them without looking */
static uint32_t layers_key_leds(void)
{
    uint32_t w, m = 0, layer = layer_now();
    for (w = 0; w < 16u; w++) {
        int on = 0;
        if (layer == LY_FX)
            on = punch.req == (int8_t)w;
        else if (layer == LY_MIX)
            on = w < 4u ? !trk[w].p[P_MUTE] : w < 8u ? (int)((song.solo >> (w - 4u)) & 1u) :
                 w == 8u ? fill_now : w == 9u ? fill_arm || fill_bar_on : w == 15u;
        else if (layer == LY_ENGINE)                    /* the track's engine; T4 SYNTH */
            on = w == 15u ? song.g[G_T4] != 0 : !is_drum(TSEL) && w == TSEL->eng_req;
        else if (layer == LY_SCALE)                     /* the root's keys */
            on = (53u + key_of_white(w)) % 12u == (uint32_t)TSEL->p[P_ROOT] % 12u;
        else if (layer == LY_STEP) {                    /* the set steps; the playhead blinks off */
            uint32_t idx = ly.page * 16u + w;
            on = idx < trk_len(TSEL) && step_on(&TSEL->step[idx]);
            if (song.playing && idx == TSEL->seq_idx)
                on = !on;
        }
        if (on)
            m |= 1u << key_of_white(w);
    }
    return m;
}

static uint32_t layers_key_glow(void)                  /* dim: the FX landmarks (Jangada, after SLOOP) */
{
    if (layer_now() != LY_FX)
        return 0u;
    return 1u << key_of_white(0) | 1u << key_of_white(4) | 1u << key_of_white(8) | 1u << key_of_white(12);
}

static uint32_t layers_leds(uint8_t *nl, uint8_t *br) /* ui_leds: the layer's button; the keys lit */
{
    led_put(ly.lock ? br : nl, panel.btn[LAYER_BTN[layer_now()]], 1);   /* locked: breathes (0.7: no blink) */
    return layers_key_leds() | fm1_in.notes;
}

/* ----------------------------------------------------------- drawing --- */
typedef struct {
    char lab[8];
    uint16_t bg, fg, top;        /* fill, text, the 3-pixel top band (0 = none) */
    uint8_t marks;               /* small squares under the label (a ratchet), 0 = none */
    uint8_t dots;                /* corner marks: TD_LOCK (a lock or a nudge), TD_FILL / TD_NOFILL (its condition) */
} tile_t;
#define TD_LOCK 1u
#define TD_FILL 2u
#define TD_NOFILL 4u

static uint32_t ly_hash(uint32_t h, const char *p) { while (*p) h = h * 31u + (uint8_t)*p++; return h; }

static void tiles_draw(const tile_t *tl)
{
    uint32_t r, c, sig = 7u;
    for (r = 0; r < 16u; r++)
        sig = ly_hash(sig * 31u + tl[r].bg * 3u + tl[r].fg * 5u + tl[r].top * 7u + tl[r].marks + tl[r].dots * 64u, tl[r].lab);
    if (!ui.force && sig == ly.tiles)
        return;
    ly.tiles = sig;
    for (r = 0; r < 4u; r++) {
        cv_begin(240, 36, C_BG);
        for (c = 0; c < 4u; c++) {
            const tile_t *t = &tl[r * 4u + c];
            int32_t x = 2 + (int32_t)c * 60;
            uint32_t m;
            cv_rrect(x, 2, 56, 32, 5, t->bg, C_BG);        /* a rounded tile (Felucca 1.0's cards) */
            if (t->top)
                cv_rect(x + 5, 3, 46, 2, t->top);
            cv_text(x + 28 - text_w(&FONT_S, t->lab) / 2, t->marks ? 7 : 10, &FONT_S, t->lab, t->fg);
            for (m = 0; m < t->marks; m++)
                cv_rrect(x + 22 + (int32_t)m * 5, 26, 3, 3, 1, t->fg, t->bg);
            if (t->dots & TD_LOCK)                         /* top right: a lock or a nudge */
                cv_rect(x + 46, 7, 4, 4, t->fg);
            if (t->dots & TD_FILL)                         /* top left: plays only in a fill (full) */
                cv_rect(x + 6, 7, 4, 4, t->fg);
            else if (t->dots & TD_NOFILL) {                /* never in one (hollow) */
                cv_rect(x + 6, 7, 4, 1, t->fg);
                cv_rect(x + 6, 10, 4, 1, t->fg);
                cv_rect(x + 6, 7, 1, 4, t->fg);
                cv_rect(x + 9, 7, 1, 4, t->fg);
            }
        }
        cv_blit(0, 40 + r * 36);
    }
}

static void layer_title(const char *name, const char *sub)
{
    uint32_t sig = ly_hash(ly_hash(ly.lock * 7919u, name), ui.msg_t ? ui.msg : sub);
    if (!ui.force && sig == ly.head)
        return;
    ly.head = sig;
    cv_begin(240, 40, C_BG);
    cv_text(4, 2, &FONT_L, name, C_HI);
    cv_text(4 + text_w(&FONT_L, name) + 10, 18, &FONT_S, ui.msg_t ? ui.msg : sub, ui.msg_t ? C_WHITE : C_GRAY);
    if (ly.lock) {                                      /* locked open: any button lets it go */
        int32_t w = text_w(&FONT_S, "LOCK") + 12;
        cv_rrect(236 - w, 4, w, 18, 9, C_WHITE, C_BG);
        cv_text(242 - w, 5, &FONT_S, "LOCK", C_BLACK);
    }
    cv_blit(0, 0);
}

/* a dial: a 270-degree ring lit up to ratio (0..1000) with a pointer; -1 = a plain ring */
static void ly_dial(int32_t cx, int32_t cy, int32_t r, int32_t ratio, uint16_t c, uint16_t dim)
{
    int32_t i, k, end = ratio < 0 ? 768 : ratio * 768 / 1000;
    for (i = 0; i <= 768; i += 8) {
        uint32_t a = (uint32_t)(384 + i) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        for (k = r - 2; k <= r; k++)
            cv_pset(cx + ((co * k) >> 15), cy + ((si * k) >> 15), ratio < 0 || i <= end ? c : dim);
    }
    if (ratio >= 0) {
        uint32_t a = (uint32_t)(384 + end) & 1023u;
        int32_t co = SINE[(a + 256u) & 1023u], si = SINE[a];
        cv_line(cx, cy, cx + ((co * (r - 4)) >> 15), cy + ((si * (r - 4)) >> 15), C_WHITE);
        cv_rect(cx - 1, cy - 1, 3, 3, C_WHITE);
    }
}

/* the dial strip: KNOB 1..4, a label and a value under each (no label: an empty column) */
/* src cut (at the end) until it is no wider than maxw; d holds 12 */
static void ly_fit(char *d, const char *src, const felucca_font_t *f, int32_t maxw)
{
    str_cpy(d, src, 12);
    while (d[0] && text_w(f, d) > maxw)
        d[str_len(d) - 1u] = 0;
    while (d[0] && d[str_len(d) - 1u] == ' ')         /* (no space left hanging at the cut) */
        d[str_len(d) - 1u] = 0;
}

static void layer_dials(const char *const lab[4], const char *const val[4], const int32_t ratio[4], uint32_t sig)
{
    uint32_t k;
    for (k = 0; k < 4u; k++)
        sig = ly_hash(ly_hash(sig * 7u + (uint32_t)ratio[k] + (ui.hot_t && ui.hot_col == k) * 5003u, lab[k]), val[k]);
    if (!ui.force && sig == ly.foot)
        return;
    ly.foot = sig;
    cv_begin(240, 56, C_BG);
    for (k = 0; k < 4u; k++) {                          /* a card per knob: dial, label, value */
        int32_t cx = 30 + 60 * (int32_t)k;
        uint16_t hot = ui.hot_t && ui.hot_col == k ? C_WHITE : C_HI;
        const felucca_font_t *vf = text_w(&FONT_M, val[k]) <= 52 ? &FONT_M : &FONT_S;
        char l[12], v[12];
        if (!lab[k][0])
            continue;
        ly_fit(l, lab[k], &FONT_S, 52);                  /* (a long preset name: cut to the card; */
        ly_fit(v, val[k], vf, 52);                       /* ENGINE shows it whole in its title) */
        cv_card(cx - 28, 0, 56, 56);
        ly_dial(cx, 14, 11, ratio[k], C_HI, C_LINE);
        cv_text(cx - text_w(&FONT_S, l) / 2, 24, &FONT_S, l, C_GRAY);
        cv_text(cx - text_w(vf, v) / 2, 38, vf, v, hot);
    }
    cv_blit(0, 184);
}

static void layer_screen_draw(void)
{
    static tile_t tl[16];
    static char v[4][10];
    const char *lab[4] = {"", "", "", ""}, *val[4] = {v[0], v[1], v[2], v[3]}, *sub = "";
    int32_t ratio[4] = {-1, -1, -1, -1};
    uint32_t i, layer = layer_now();
    if (!ly.shown) {
        lcd_fill(0, 0, 240, 240, C_BG);
        ui.force = 1;
        ly.shown = 1;
    }
    for (i = 0; i < 4u; i++)
        v[i][0] = 0;
    memset(tl, 0, sizeof tl);
    if (layer == LY_FX) {                               /* the 16 punch-in effects */
        static const char *const PSHORT[16] = {"LOOP4", "LOOP8", "LOOP16", "LOOP32", "STUTT", "REV", "STOP", "HALF",
                                               "LOW", "HIGH", "PHONE", "CRUSH", "ALIAS", "GATE", "ECHO", "WOBBL"};
        sub = "HOLD + KEY";
        for (i = 0; i < 16u; i++) {
            int on = punch.req == (int8_t)i;
            str_cpy(tl[i].lab, PSHORT[i], 8);
            tl[i].bg = on ? C_WHITE : C_SURF;
            tl[i].fg = on ? C_BLACK : C_AMB;
            tl[i].top = on ? 0 : (i & 4u) ? C_DIM : C_GRAY;   /* rows alternate: easier to count */
        }
        lab[0] = "FILT", lab[1] = "DUST", lab[2] = "DUCK", lab[3] = "TAPE";
        {
            int32_t f = song.g[G_FILT];
            if (!f)
                str_cpy(v[0], "OFF", 10);
            else {
                str_cpy(v[0], f < 0 ? "LP" : "HP", 10);
                fmt_int(v[0] + 2, f < 0 ? (-f * 100 + 32) / 64 : (f * 100 + 31) / 63);
            }
            fmt_int(v[1], song.g[G_DUST] * 100 / 127);
            fmt_int(v[2], song.g[G_DUCK] * 100 / 127);
            fmt_int(v[3], song.g[G_TAPE] * 100 / 127);
        }
        ratio[0] = (song.g[G_FILT] + 64) * 1000 / 127;
        ratio[1] = song.g[G_DUST] * 1000 / 127;
        ratio[2] = song.g[G_DUCK] * 1000 / 127;
        ratio[3] = song.g[G_TAPE] * 1000 / 127;
    } else if (layer == LY_MIX) {                       /* mute 1..4, solo 1..4, tap */
        static const char *const L[4] = {"T1", "T2", "T3", "T4"};
        sub = "MUTE SOLO FILL TAP";
        for (i = 0; i < 16u; i++) {
            tl[i].bg = C_BG;
            tl[i].fg = C_DIM;
        }
        for (i = 0; i < 4u; i++) {
            int m = trk[i].p[P_MUTE] != 0, so = (song.solo >> i) & 1u;
            str_cpy(tl[i].lab, "MUTE 1", 8);
            tl[i].lab[5] = (char)('1' + i);
            tl[i].bg = m ? C_SURF : C_AMB;
            tl[i].fg = m ? C_DIM : C_BLACK;
            str_cpy(tl[4 + i].lab, "SOLO 1", 8);
            tl[4 + i].lab[5] = (char)('1' + i);
            tl[4 + i].bg = so ? C_WHITE : C_SURF;
            tl[4 + i].fg = so ? C_BLACK : C_GRAY;
            tl[4 + i].top = C_DIM;
        }
        str_cpy(tl[8].lab, "FILL", 8);                  /* held: a fill (after SLOOP 2.4) */
        tl[8].bg = fill_now ? C_WHITE : C_SURF;
        tl[8].fg = fill_now ? C_BLACK : C_AMB;
        str_cpy(tl[9].lab, "FILL>", 8);                 /* the next bar a fill */
        tl[9].bg = fill_bar_on ? C_WHITE : C_SURF;
        tl[9].fg = fill_bar_on ? C_BLACK : C_AMB;
        tl[9].top = fill_arm ? C_WHITE : C_DIM;         /* armed: lit until its bar comes */
        str_cpy(tl[13].lab, "LEARN", 8);               /* MIDI LEARN (midi_learn.c) */
        tl[13].bg = ml_ui.on ? C_WHITE : C_SURF;
        tl[13].fg = ml_ui.on ? C_BLACK : C_GRAY;
        str_cpy(tl[14].lab, "TAP>", 8);
        fmt_int(tl[15].lab, song.g[G_BPM]);
        tl[15].bg = song.playing && clk_pos < BEAT_U / 4u ? C_WHITE : C_DIM;   /* the beat */
        tl[15].fg = tl[15].bg == C_WHITE ? C_BLACK : C_WHITE;
        for (i = 0; i < 4u; i++) {
            int32_t lv = *trk_level_p(i) & 127;
            lab[i] = L[i];
            fmt_int(v[i], lv * 100 / 127);
            ratio[i] = lv * 1000 / 127;
        }
    }
    else if (layer == LY_STEP) {                        /* the 16 steps of the page */
        static char pg[16];
        track_t *t = TSEL;
        uint32_t len = trk_len(t);
        for (i = 0; i < 16u; i++) {
            uint32_t idx = ly.page * 16u + i;
            const step_t *st = &t->step[idx];
            int on = step_on(st);
            if (idx >= len) {
                tl[i].bg = C_BG;
                continue;
            }
            if (on)
                note_name(tl[i].lab, st->note[0]);
            else if (st->time == ST_TIE && st->n)
                str_cpy(tl[i].lab, "--", 8);
            else
                fmt_int(tl[i].lab, (int32_t)idx + 1);
            tl[i].bg = on ? ((st->flags & SF_ACCENT) ? C_HI : C_AMB) : C_SURF;
            tl[i].fg = on ? C_BLACK : C_DIM;
            tl[i].marks = (uint8_t)(on ? (st->flags & SF_RATCH) >> SF_RATCH_SH : 0u);
            if (on && (st->flags & SF_CHANCE))
                tl[i].top = C_DIM;                      /* not every time */
            tl[i].dots = (uint8_t)((step_marked(t, idx) ? TD_LOCK : 0u) |
                                   (step_cond(t, idx) == FC_FILL ? TD_FILL : step_cond(t, idx) == FC_NOFILL ? TD_NOFILL : 0u));
            if (song.playing && idx == t->seq_idx)
                tl[i].top = C_WHITE;                    /* the playhead */
            if ((ly.held >> i) & 1u) {
                tl[i].bg = C_WHITE;
                tl[i].fg = C_BLACK;
            }
        }
        str_cpy(pg, "TRACK 1", sizeof pg);
        pg[6] = (char)('1' + song.sel);
        if (len > 16u) {
            str_cpy(pg + 7, "  1/1", 6);
            pg[9] = (char)('1' + ly.page);
            pg[11] = (char)('0' + (len + 15u) / 16u);
        }
        sub = pg;
        if (ly.held) {
            const step_t *st = 0;
            for (i = 0; i < 16u; i++)
                if (((ly.held >> i) & 1u) && step_on(&t->step[ly.page * 16u + i])) {
                    st = &t->step[ly.page * 16u + i];
                    break;
                }
            lab[0] = "NOTE", lab[1] = "RTCH", lab[2] = "CHNC", lab[3] = "FLAG";
            {   /* the title: the picked parameter's lock on the first held step, its nudge, its condition */
                static char hs[48];
                int32_t f = held_first();
                uint32_t id = lock_cur(t);
                hs[0] = 0;
                if (f >= 0 && id < P_COUNT) {
                    int k = lock_find(t, (uint32_t)f, id, 0);
                    const char *u;
                    char vb[12];
                    lock_label(t, id, hs);
                    str_cpy(hs + str_len(hs), " ", 2);
                    if (k >= 0) {
                        param_format(lock_desc(t, id), t->x.lock[k].val, vb, &u);
                        str_cpy(hs + str_len(hs), vb, 10);
                    } else {
                        str_cpy(hs + str_len(hs), "--", 3);
                    }
                }
                if (f >= 0 && step_micro(t, (uint32_t)f)) {
                    int32_t m = step_micro(t, (uint32_t)f);
                    str_cpy(hs + str_len(hs), m > 0 ? " >+" : " <", 4);
                    fmt_int(hs + str_len(hs), m);
                }
                if (f >= 0 && step_cond(t, (uint32_t)f))
                    str_cpy(hs + str_len(hs), step_cond(t, (uint32_t)f) == FC_FILL ? " FILL" : " NOFILL", 8);
                if (hs[0])
                    sub = hs;
            }
            if (st) {
                static const char *const FL[4] = {"-", "ACC", "SLD", "A+S"};
                uint32_t r = (st->flags & SF_RATCH) >> SF_RATCH_SH, c = (st->flags & SF_CHANCE) >> SF_CHANCE_SH;
                note_name(v[0], st->note[0]);
                str_cpy(v[1], "X1", 10);
                v[1][1] = (char)('1' + r);
                fmt_int(v[2], 100 - 25 * (int32_t)c);
                str_cpy(v[3], FL[st->flags & 3u], 10);
                ratio[0] = st->note[0] * 1000 / 127;
                ratio[1] = (int32_t)r * 333;
                ratio[2] = 1000 - (int32_t)c * 333;
            }
        } else {
            const char *u;
            lab[0] = "NOTE", lab[1] = "DIV", lab[2] = "SWG", lab[3] = "LEN";
            note_name(v[0], last_note);
            param_format(track_desc(t, P_SDIV), t->p[P_SDIV], v[1], &u);
            param_format(track_desc(t, P_SSWING), t->p[P_SSWING], v[2], &u);
            fmt_int(v[3], (int32_t)len);
            ratio[0] = last_note * 1000 / 127;
            ratio[1] = (t->p[P_SDIV] - TP[P_SDIV].min) * 1000 / (TP[P_SDIV].max - TP[P_SDIV].min);
            ratio[2] = t->p[P_SSWING] * 10;
            ratio[3] = ((int32_t)len - 1) * 1000 / 63;
        }
    }
    else if (layer == LY_ENGINE) {                      /* the engines; the selected one lit */
        static char tr[24];
        track_t *t = TSEL;
        int drum = is_drum(t);
        for (i = 0; i < 16u; i++) {
            tl[i].bg = C_BG;
            tl[i].fg = C_DIM;
            if (i < NENGINES) {
                int on = !drum && i == t->eng_req;
                str_cpy(tl[i].lab, ENGINES[i]->name, 8);
                tl[i].bg = on ? C_WHITE : C_SURF;
                tl[i].fg = on ? C_BLACK : drum ? C_DIM : C_AMB;
            }
        }
        str_cpy(tl[15].lab, song.g[G_T4] ? "T4 SYN" : "T4 DRM", 8);
        tl[15].bg = C_SURF;
        tl[15].fg = C_GRAY;
        tl[15].top = C_DIM;
        str_cpy(tr, "T1 ", sizeof tr);
        tr[1] = (char)('1' + song.sel);
        if (!drum) {                                    /* the preset's whole name (the PRST dial cuts it) */
            const engine_t *e0 = ENGINES[t->eng_req % NENGINES];
            if (!fm6_bank_sound(t, tr + 3))             /* (Jangada 0.6: an FM6 bank voice, by its name) */
                str_cpy(tr + 3, e0->npresets ? e0->presets[t->preset % e0->npresets].name : "", sizeof tr - 3);
        }
        sub = tr;
        if (drum) {
            lab[3] = "LVL";
            fmt_int(v[3], song.g[G_DRLVL] * 100 / 127);
            ratio[3] = song.g[G_DRLVL] * 1000 / 127;
            sub = "DRUM TRACK";
        } else {
            const engine_t *e = ENGINES[t->eng_req % NENGINES];
            const char *u;
            char bn[11];
            static const char *const VM[4] = {"POLY", "MONO", "LEG", "UNI"};
            lab[0] = "PRST", lab[1] = "VOICE", lab[2] = "GLIDE", lab[3] = "LVL";
            str_cpy(v[0], fm6_bank_sound(t, bn) ? bn : e->npresets ? e->presets[t->preset % e->npresets].name : "-", 10);
            str_cpy(v[1], VM[t->p[P_VOICE] & 3], 10);
            param_format(&TP[P_GLIDE], t->p[P_GLIDE], v[2], &u);
            fmt_int(v[3], t->p[P_LEVEL] * 100 / 127);
            ratio[0] = e->npresets > 1u ? (int32_t)(t->preset % e->npresets) * 1000 / (e->npresets - 1) : 0;
            ratio[1] = (t->p[P_VOICE] & 3) * 333;
            ratio[2] = (t->p[P_GLIDE] - TP[P_GLIDE].min) * 1000 / (TP[P_GLIDE].max - TP[P_GLIDE].min ? TP[P_GLIDE].max - TP[P_GLIDE].min : 1);
            ratio[3] = t->p[P_LEVEL] * 1000 / 127;
        }
    }
    else if (layer == LY_SCALE) {                       /* the white keys' notes / chords; the root lit */
        static char kb[24];
        track_t *t = TSEL;
        uint32_t root = (uint32_t)t->p[P_ROOT] % 12u;
        uint32_t mask = t->p[P_CHORD] && !t->p[P_SCALE] ? SCALE_MINOR : scale_mask(t);
        str_cpy(kb, N_NOTE[root], 4);
        str_cpy(kb + str_len(kb), " ", 2);
        str_cpy(kb + str_len(kb), N_SCALE[clamp(t->p[P_SCALE], 0, TP[P_SCALE].max)], 8);
        sub = kb;
        for (i = 0; i < 16u; i++) {
            uint32_t k = key_of_white(i), pc = (53u + k) % 12u, in = (mask >> ((pc + 12u - root) % 12u)) & 1u;
            if (!is_drum(t) && t->p[P_CHORD]) {         /* CHORD: the chord this key plays */
                uint8_t c[4];
                uint32_t n = kb_map(t, k), m;
                if (n != KB_SILENT && (m = chord_notes(t, n, 0u, c)) != 0u) {
                    uint32_t third = m > 1u ? (uint32_t)(c[1] - c[0]) : 4u;
                    str_cpy(tl[i].lab, N_NOTE[c[0] % 12u], 8);
                    if (t->p[P_CHORD] == 5)
                        str_cpy(tl[i].lab + str_len(tl[i].lab), "5", 2);
                    else if (third == 3u)
                        str_cpy(tl[i].lab + str_len(tl[i].lab), "m", 2);
                    pc = c[0] % 12u;
                    in = 1;
                }
            } else {
                str_cpy(tl[i].lab, N_NOTE[pc], 8);
            }
            tl[i].bg = pc == root ? C_HI : C_SURF;
            tl[i].fg = pc == root ? C_BLACK : in ? C_AMB : C_DIM;
        }
        lab[0] = "CHRD", lab[1] = "SCL", lab[2] = "QNT", lab[3] = "TRN";
        str_cpy(v[0], N_CHORD[clamp(t->p[P_CHORD], 0, 5)], 10);
        str_cpy(v[1], N_SCALE[clamp(t->p[P_SCALE], 0, TP[P_SCALE].max)], 10);
        str_cpy(v[2], N_QUANT[clamp(t->p[P_QUANT], 0, 2)], 10);
        fmt_int(v[3], t->p[P_TRANS]);
        if (is_drum(t))
            v[0][0] = v[2][0] = v[3][0] = 0;
        ratio[0] = t->p[P_CHORD] * 200;
        ratio[1] = t->p[P_SCALE] * 1000 / (TP[P_SCALE].max ? TP[P_SCALE].max : 1);
        ratio[2] = t->p[P_QUANT] * 500;
        ratio[3] = (t->p[P_TRANS] + 24) * 1000 / 48;
    }
    layer_title(LAYER_NAME[layer % LY_COUNT], sub);
    tiles_draw(tl);
    layer_dials(lab, val, ratio, layer * 7919u);
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.hot_t)
        ui.hot_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    ui.force = 0;
}

/* ------------------------------------------------------------ TRACKS --- */
/* Jangada (after SLOOP's live view): one row per track: its number (the selected one lit), the sound
 * and its engine, the steps of the page the playhead is on, the level, MUTE / SOLO / REC; above, the
 * tempo and bar.beat; below, KNOB 1..4 as on the page (TYPE LEVEL LEN PAN, ui_input.c tracks_edit) */
static uint32_t tr_sig[NTRK + 2];

static void tracks_head(void)
{
    char b[16], p[12];
    uint32_t sig, any_rec = song.rec != 0u;
    fmt_int(b, song.g[G_BPM]);
    if (song.playing) {
        fmt_int(p, (int32_t)(clk_beat / 4u) + 1);
        str_cpy(p + str_len(p), ".", 2);
        fmt_int(p + str_len(p), (int32_t)(clk_beat % 4u) + 1);
    } else {
        str_cpy(p, "STOP", sizeof p);
    }
    sig = ly_hash(ly_hash(ly_hash(any_rec * 3u + song.playing * 7u + (ui.bpm_t != 0) * 11u, b), p),
                  ui.msg_t ? ui.msg : "");
    if (!ui.force && sig == tr_sig[NTRK])
        return;
    tr_sig[NTRK] = sig;
    cv_begin(240, 40, C_BG);
    {
        int32_t x = cv_text(4, 2, &FONT_L, b, ui.bpm_t ? C_WHITE : C_HI);
        cv_text(x + 4, 18, &FONT_S, "BPM", C_GRAY);
    }
    if (ui.msg_t) {
        cv_text(116, 12, &FONT_S, ui.msg, C_WHITE);
    } else {
        int32_t i;
        if (song.playing)
            for (i = 0; i < 10; i++)
                cv_rect(116 + i, 12 + i / 2, 1, 12 - i, C_WHITE);   /* a triangle */
        else
            cv_rect(116, 13, 10, 10, C_DIM);
        cv_text(132, 9, &FONT_S, p, song.playing ? C_WHITE : C_DIM);
        if (any_rec) {
            int32_t w = text_w(&FONT_S, "REC") + 12;
            cv_rrect(236 - w, 8, w, 18, 9, C_WHITE, C_BG);
            cv_text(242 - w, 9, &FONT_S, "REC", C_BLACK);
        }
    }
    cv_blit(0, 0);
}

static void tracks_row(uint32_t c)
{
    track_t *t = &trk[c];
    uint32_t sel = c == song.sel, len = trk_len(t), lvl = trk_level(c), i, sig;
    uint32_t page = song.playing ? t->seq_idx / 16u : (sel ? ui.bank : 0u), steps = 0;
    uint32_t mute = t->p[P_MUTE] != 0 || !lvl, solo = (song.solo >> c) & 1u, rec = (song.rec >> c) & 1u;
    uint32_t ph = song.playing && t->seq_idx / 16u == page ? t->seq_idx % 16u : 99u;
    int32_t y0 = 42 + (int32_t)c * 35;
    char name[16];
    const char *eng = is_drum(t) ? DRUM_KIT_STYLES[drum_kit()] : ENGINES[t->eng_req % NENGINES]->name;
    trk_short_name(c, name);
    for (i = 0; i < 16u; i++)
        if (page * 16u + i < len && step_on(&t->step[page * 16u + i]))
            steps |= 1u << i;
    sig = ly_hash(ly_hash(steps * 31u + ph * 7u + len * 131u + page * 17u + lvl * 1031u + sel * 3u + mute * 5u +
                          solo * 13u + rec * 19u, name), eng);
    if (!ui.force && sig == tr_sig[c])
        return;
    tr_sig[c] = sig;
    cv_begin(240, 34, C_BG);
    cv_card(2, 0, 236, 33);                             /* a card per track (Felucca 1.0's look) */
    {   /* the number: lit when selected */
        char b[2] = {(char)('1' + c), 0};
        cv_rrect(5, 3, 23, 27, 4, sel ? C_HI : C_RAISE, C_SURF);
        cv_text(17 - text_w(&FONT_M, b) / 2, 8, &FONT_M, b, sel ? C_BLACK : C_GRAY);
    }
    {   /* the sound and its engine; the badges on the right */
        int32_t x = cv_text(34, 1, &FONT_S, name, mute ? C_DIM : sel ? C_HI : C_AMB), bx = 234;
        const char *badge[3] = {rec ? "REC" : 0, solo ? "SOLO" : 0, t->p[P_MUTE] ? "MUTE" : 0};
        uint32_t k;
        for (k = 0; k < 3u; k++) {
            int32_t w;
            if (!badge[k])
                continue;
            w = text_w(&FONT_S, badge[k]) + 10;
            bx -= w;
            cv_rrect(bx, 3, w, 15, 7, k == 2u ? C_GRAY : C_WHITE, C_SURF);
            cv_text(bx + 5, 2, &FONT_S, badge[k], C_BLACK);
            bx -= 3;
        }
        if (x + 8 + text_w(&FONT_S, eng) < bx)
            cv_text(x + 8, 1, &FONT_S, eng, C_DIM);
    }
    for (i = 0; i < 16u; i++) {                         /* the steps of the page; the playhead under */
        int32_t x = 34 + (int32_t)i * 11;
        uint16_t col = page * 16u + i >= len ? C_SURF : (steps >> i) & 1u ? (sel ? C_HI : C_AMB) : C_RAISE;
        cv_rrect(x, 19, 9, 8, 2, col, C_SURF);
        if (i == ph)
            cv_rect(x, 28, 9, 2, C_WHITE);
    }
    cv_rect(212, 22, 22, 3, C_RAISE);                   /* the level */
    if (!mute)
        cv_rect(212, 22, (int32_t)(lvl * 22u / 127u), 3, sel ? C_HI : C_GRAY);
    cv_blit(0, (uint32_t)y0);
}

static void tracks_screen_draw(void)
{
    static char v[4][12];
    const char *lab[4] = {"TYPE", "LEVEL", "LEN", "PAN"}, *val[4] = {v[0], v[1], v[2], v[3]}, *u;
    int32_t ratio[4];
    const track_t *t = TSEL;
    uint32_t c, lvl = trk_level(song.sel);
    if (!ly.shown) {
        lcd_fill(0, 0, 240, 240, C_BG);
        ui.force = 1;
        ly.shown = 1;
    }
    tracks_head();
    for (c = 0; c < NTRK; c++)
        tracks_row(c);
    str_cpy(v[0], is_drum(t) ? "DRUM" : "SYNTH", 12);
    ratio[0] = song.sel == TRK_DRUM ? (is_drum(t) ? 0 : 1000) : -1;
    if (!lvl || t->p[P_MUTE])
        str_cpy(v[1], "MUTE", 12);
    else
        fmt_int(v[1], (int32_t)lvl * 100 / 127);
    ratio[1] = (int32_t)lvl * 1000 / 127;
    fmt_int(v[2], t->p[P_SLEN]);
    ratio[2] = (t->p[P_SLEN] - 1) * 1000 / (NSTEP - 1);
    param_format(&TP[P_PAN], t->p[P_PAN], v[3], &u);
    ratio[3] = (t->p[P_PAN] + 64) * 1000 / 127;
    layer_dials(lab, val, ratio, 0x7A11u);
    if (ui.msg_t)
        ui.msg_t--;
    if (ui.hot_t)
        ui.hot_t--;
    if (ui.bpm_t)
        ui.bpm_t--;
    ui.force = 0;
}

/* ui_draw: the layer's screen when one shows, else TRACKS (1); else back to the page once */
static int layers_draw(void)
{
    static uint8_t which;                               /* the full screen shown: 1 a layer, 2 TRACKS, 3 the visualiser */
    uint32_t want = layer_visible() ? 1u : vis_shown() ? 3u : (!ui.home && !ui.confirm && cur_page()->scope == SC_TRK) ? 2u : 0u;
    if (want != which && ly.shown) {
        ly.shown = 0;                                   /* another screen: from black, everything */
        lcd_fill(0, 0, 240, 240, C_BG);
        ui.force = 1;
    }
    which = (uint8_t)want;
    if (want == 1u) {
        layer_screen_draw();
        return 1;
    }
    if (want == 3u) {                                   /* (0.7: over the TRACKS screen, ui_vis.c) */
        vis_draw();
        return 1;
    }
    if (want == 2u) {
        tracks_screen_draw();
        return 1;
    }
    return 0;
}
