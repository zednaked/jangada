/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Keyboard, scale, arpeggiator, sequencer and transport. Runs in the audio
 * ISR, once per CTL-sample block, and ends in trk_note_on / trk_note_off:
 * engines never see where a note came from.
 * Four tracks, one transport: every track's pattern loops on its own LEN / DIV /
 * SWING / GATE (polymeter). The keys play the selected track; MIDI channels 1..3
 * play parts 1..3, the DRUMS channel (GLO > DRUMS, default 10) the drum track, any
 * other channel the selected track. A note into an armed track (song.rec) while
 * the transport runs is recorded into its pattern, quantised to its (swung) steps, with its
 * held length as TIE steps (rec_note, rec_hold, rec_release). */
#define SCALE_MINOR SCALE_MASK[2]          /* (CHORD on CHR: the chords of the minor scale) */
static const uint16_t SCALE_MASK[] = {
    0xFFF,                                   /* CHR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MAJ */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* MIN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* DOR */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 10),   /* MIX */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 7) | (1 << 9),                          /* PEN */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 10),                         /* MPEN */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 11),   /* HARM */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10),   /* PHRY */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 11),   /* LYD */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 10),   /* LOC */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 9) | (1 << 11),   /* MEL (ascending) */
    (1 << 0) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 10),              /* BLUES (minor) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 8) | (1 << 10),              /* WHOLE */
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10), /* DIMHW */
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11), /* DIMWH */
    /* Jangada (appended: a saved SCL keeps its scale) */
    (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 9) | (1 << 10),   /* NORD: the nordestino
                                     * mode, mixolydian with the raised 4th (lydian b7): the sanfona of the
                                     * baiao; MIX for the plain baiao, DOR for the minor (xote, toada) */
};

#define KB_SILENT 255u
static uint32_t kb_prev;
static uint8_t kb_note[27], kb_trk[27];  /* per key: the note it started and on which track */
static uint8_t kb_nt[27][4], kb_n[27];    /* Jangada: per key, the chord it started (CHORD on), 0 = none */
static uint8_t last_note = 60;
static volatile uint8_t transport_req;   /* 1 start, 2 stop (from the UI) */
static volatile uint8_t panic_req;       /* bit per track: release every sounding note (preset / engine change) */
static volatile uint8_t latch_off_req;   /* Jangada: bit per track: drop the latched (HOLD) chord: ARP held */
static volatile uint8_t hush_req;        /* Jangada: bit per track: fade every voice now: ARP held again */

/* the drum track on the keys: 27 useful GM notes, lowest key first */
static const uint8_t DRUM_KEYS[27] = {
    36, 35, 38, 40, 37, 39, 42, 44, 46,      /* kicks, snares, rim, clap, hi-hats */
    41, 43, 45, 47, 48, 50,                  /* toms, low to high */
    49, 57, 51, 53, 54, 56,                  /* crashes, ride, ride bell, tambourine, cowbell */
    62, 63, 64, 70, 75, 76,                  /* congas, maracas, claves, wood block */
};

static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }

static uint32_t trk_midi_ch(uint32_t i)    /* MIDI channel 0..15 of track i (keys -> MIDI out) */
{
    if (trk_synth(i))
        return i;                                      /* track 4 as a synth: channel 4 */
    return song.g[G_DRCH] ? (uint32_t)song.g[G_DRCH] - 1u : 9u;
}

static uint32_t scale_mask(const track_t *t)
{
    return SCALE_MASK[clamp(t->p[P_SCALE], 0, sizeof SCALE_MASK / sizeof SCALE_MASK[0] - 1)];
}

static uint32_t kb_map(const track_t *t, uint32_t k)
{
    static const int8_t DEGREE[12] = {0, -1, 1, -1, 2, 3, -1, 4, -1, 5, -1, 6};
    int32_t n = 53 + (int32_t)k;
    if (is_drum(t))
        return DRUM_KEYS[k % 27u];
    if (ENGINES[t->eng_req % NENGINES] == &ENG_SAMPLE && drum_set() >= 0 &&   /* (the engine it switches to) */
        (uint32_t)t->p[P_E0] % SMP_NSETS == (uint32_t)drum_set())   /* GM KIT: lowest key = kick (C2), no scale */
        return (uint32_t)clamp(36 + 12 * song.octave + (int32_t)k, 0, 127);
#if FELUCCA_SLICE
    if (ENGINES[t->eng_req % NENGINES] == &ENG_SLICE)   /* SLICE: lowest key = slice 0 (C4 + ROOT), no scale */
        return (uint32_t)clamp(SLC_BASE + t->p[P_ROOT] + 12 * song.octave + (int32_t)k, 0, 127);
#endif
    if (t->p[P_QUANT] == 1 && !t->p[P_CHORD]) {  /* SNAP: every key, rounded down to the scale (the old ON) */
        uint32_t mask = scale_mask(t), guard = 12;
        n += 12 * song.octave + t->p[P_TRANS];
        while (guard-- && !((mask >> (uint32_t)((n - t->p[P_ROOT] + 120) % 12)) & 1u))
            n--;
        return (uint32_t)clamp(n, 0, 127);
    }
    if (t->p[P_QUANT] == 2 || t->p[P_CHORD]) {   /* WHITE: white keys walk the scale, black keys are silent */
        uint32_t mask = t->p[P_CHORD] && !t->p[P_SCALE] ? SCALE_MINOR : scale_mask(t), i;   /* (CHORD: as WHITE) */
        int32_t count = 0, degree = DEGREE[n % 12], oct;
        if (degree < 0)
            return KB_SILENT;
        /* C4 is the root. Walk scale degrees on successive white keys, including
         * below C4; scales with 5, 6, 8 or 12 notes still have no duplicated degrees. */
        degree += (n / 12 - 5) * 7;
        for (i = 0; i < 12u; i++)
            count += (mask >> i) & 1u;
        oct = degree / count;
        degree %= count;
        if (degree < 0) {
            degree += count;
            oct--;
        }
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (!degree)
                    break;
                degree--;
            }
        n = 60 + t->p[P_ROOT] + 12 * oct + (int32_t)i;
    }
    return (uint32_t)clamp(n + 12 * song.octave + t->p[P_TRANS], 0, 127);
}

/* Jangada (after SLOOP): CHORD mode, the chord of the scale built on note n (in the scale; CHR: the
 * minor scale), into c[]; the notes it holds (<= 4, the most a step keeps) */
static const int8_t CHORD_DEG[6][4] = {
    {0, -1, -1, -1},                     /* OFF */
    {0, 2, 4, -1},                       /* TRIAD: 1 3 5 */
    {0, 2, 4, 6},                        /* 7TH: 1 3 5 7 */
    {0, 2, 6, 8},                        /* 9TH: 1 3 7 9 (the lo-fi / R&B voicing) */
    {0, 3, 4, -1},                       /* SUS4: 1 4 5 */
    {0, -1, -1, -1},                     /* POWER: 1 5 8 (semitones, below) */
};
static uint32_t chord_notes(const track_t *t, uint32_t n, uint8_t *c)
{
    uint32_t type = (uint32_t)clamp(t->p[P_CHORD], 0, 5), mask = t->p[P_SCALE] ? scale_mask(t) : SCALE_MINOR;
    uint32_t k = 0, j;
    if (type == 5u) {
        static const uint8_t PW[3] = {0, 7, 12};
        for (j = 0; j < 3u; j++)
            if (n + PW[j] < 128u)
                c[k++] = (uint8_t)(n + PW[j]);
        return k;
    }
    for (j = 0; j < 4u && CHORD_DEG[type][j] >= 0; j++) {
        int32_t m = (int32_t)n, d = CHORD_DEG[type][j], guard = 48;
        while (d > 0 && guard--) {                       /* d scale degrees up */
            m++;
            if ((mask >> (uint32_t)((m - t->p[P_ROOT] + 120) % 12)) & 1u)
                d--;
        }
        if (m < 128)
            c[k++] = (uint8_t)m;
    }
    return k;
}

/* ------------------------------------------------------------- arp --- */
static void arp_add(track_t *t, uint32_t note)
{
    uint32_t i;
    if (t->p[P_AHOLD] && t->arp_phys == 0u)
        t->nheld = 0;                               /* new chord replaces the latched one */
    t->arp_phys++;                                  /* every key-down: arp_remove counts every key-up */
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] == note)
            return;                                 /* repeated note-on: not a new note */
    if (t->nheld < 16u)
        t->held[t->nheld++] = (uint8_t)note;
    if (t->nheld == 1u) {
        t->arp_pos = 0xFFFFFFF;                     /* fire on this block */
        t->arp_idx = 0xFFFFFFFFu;
    }
}

static void arp_remove(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    if (t->arp_phys)
        t->arp_phys--;
    if (t->p[P_AHOLD])
        return;
    for (i = 0; i < t->nheld; i++)
        if (t->held[i] != note)
            t->held[k++] = t->held[i];
    t->nheld = (uint8_t)k;
}

/* the sounding arp note or RPT chord off */
static void arp_silence(track_t *t)
{
    uint32_t i;
    if (t->arp_note)
        trk_note_off(t, t->arp_note);
    t->arp_note = 0;
    for (i = 0; i < t->arp_nch; i++)
        trk_note_off(t, t->arp_chord[i]);
    t->arp_nch = 0;
}

/* n samples (the gate counts them); adv: the units the steps move, as seq_tick's (Jangada 0.7: exact at
 * any tempo, and a MIDI clock moves the arp too) */
static void arp_tick(track_t *t, uint32_t n, uint32_t adv)
{
    uint32_t period = div_samples((uint32_t)t->p[P_ARATE]), cnt, list[64], len = 0, i, j, o;
    uint32_t q = DIV_Q24[(uint32_t)t->p[P_ARATE] % 10u];
    int32_t sw = t->p[P_ASWING] * (int32_t)(441u * q);   /* (units: BEAT_U / 24 / 250 = 441) */
    if (t->arp_note || t->arp_nch) {
        if (t->arp_off <= n)
            arp_silence(t);
        else
            t->arp_off -= n;
    }
    if (!t->p[P_AMODE] || !t->nheld) {
        if (!t->nheld)
            arp_silence(t);
        return;
    }
    t->arp_pos += adv;
    {
        uint32_t len = BEAT_U / 24u * q + (uint32_t)((t->arp_idx & 1u) ? sw : -sw);
        if (t->arp_pos < len && t->arp_pos != 0xFFFFFFF + adv)
            return;
        /* the remainder carries on, as seq_tick does (setting 0 drifted: each step rounded up to a block) */
        t->arp_pos = t->arp_pos == 0xFFFFFFF + adv || t->arp_pos - len >= len ? 0 : t->arp_pos - len;
    }
    /* build the note list: held notes (sorted or as played) over OCT octaves */
    for (i = 0; i < t->nheld; i++)
        list[i] = t->held[i];
    cnt = t->nheld;
    if (!t->p[P_AORDER])
        for (i = 1; i < cnt; i++)
            for (j = i; j > 0 && list[j - 1] > list[j]; j--) {
                uint32_t x = list[j];
                list[j] = list[j - 1];
                list[j - 1] = x;
            }
    for (o = 0; o < (uint32_t)t->p[P_AOCT]; o++)
        for (i = 0; i < cnt && len < 64u; i++)
            list[len++] = clamp((int32_t)list[i] + 12 * (int32_t)o, 0, 127);
    t->arp_idx++;
    switch (t->p[P_AMODE]) {
    case 2:
        j = len - 1u - t->arp_idx % len;
        break;
    case 3: {
        uint32_t cyc = len > 1u ? 2u * len - 2u : 1u, k = t->arp_idx % cyc;
        j = k < len ? k : cyc - k;
        break;
    }
    case 4:
        j = rng() % len;
        break;
    case 6: {                                       /* UDI: up and down, both ends played twice */
        uint32_t k = t->arp_idx % (2u * len);
        j = k < len ? k : 2u * len - 1u - k;
        break;
    }
    case 7:                                         /* RPT: the whole chord on every step (drones) */
        j = 0;
        break;
    default:
        j = t->arp_idx % len;
        break;
    }
    arp_silence(t);
    if ((uint32_t)(rng() & 127u) <= (uint32_t)t->p[P_APROB]) {
        t->arp_off = period * (uint32_t)t->p[P_AGATE] / 128u;
        if (t->p[P_AMODE] == 7) {
            for (i = 0; i < len && t->arp_nch < NVOICE; i++) {
                t->arp_chord[t->arp_nch++] = (uint8_t)list[i];
                trk_note_on(t, list[i], 100);
            }
        } else {
            t->arp_note = (uint8_t)list[j];
            trk_note_on(t, t->arp_note, 100);
        }
    }
}

/* -------------------------------------------------------- note input --- */
/* the length of step idx in samples: SWING (the track's + the global) makes the even steps longer
 * and the odd ones shorter, so every odd step starts late */
static uint32_t step_samples(const track_t *t, uint32_t period, uint32_t idx)
{
    int32_t sw = (t->p[P_SSWING] + song.g[G_SWING]) * (int32_t)period / 250;
    return period + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* seq_pos counts units (a sample at 1 BPM, fx.c BEAT_U a beat), as the beat clock does: a step is
 * DIV in 1/24 beat x BEAT_U / 24 exactly, at any tempo, the swing as for samples (BEAT_U / 24 / 250 = 441
 * exactly). Jangada 0.7: with the internal clock too (it counted whole samples, div_samples rounded down:
 * the steps ran ahead of the beat clock, 8 samples a bar of 1/16 at 120 BPM, and tracks on different
 * DIVs drifted apart); a MIDI clock (after SLOOP 2.3, seq_u) moves it by its pulses (mclk_adv) */
static uint8_t seq_u;                               /* a MIDI clock drives the sequencer (fx.c DIV_Q24, BEAT_U) */
static uint32_t seq_len(const track_t *t, uint32_t idx)   /* step idx as seq_pos counts it */
{
    uint32_t div = (uint32_t)t->p[P_SDIV] % 10u;
    int32_t sw;
    sw = (t->p[P_SSWING] + song.g[G_SWING]) * (int32_t)(441u * DIV_Q24[div]);
    return BEAT_U / 24u * DIV_Q24[div] + (uint32_t)((idx & 1u) ? -sw : sw);
}

/* live recording: the note goes into the nearest step, as swung (the one playing, or
 * the next one when it is past the middle of the playing one). Overdub: a step that
 * holds notes gets this one added (a chord of up to 4; when full, the last note is
 * replaced); MONO / LEGATO / UNISON parts keep one note per step, as step entry does.
 * Held on (synth parts): each further step the sequencer enters while the note is
 * held becomes a TIE (rec_hold), up to the pattern length; a release before the middle
 * of the last one puts that step back (rec_release), so a short note stays one step.
 * A note recorded into another step ends the hold before (the step model ties the
 * notes of one step only). */
static void rec_note(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t len = t->p[P_SLEN] > 0 ? (uint32_t)t->p[P_SLEN] : 1u, idx = t->seq_idx % len, k;
    uint32_t next = t->seq_pos > seq_len(t, t->seq_idx) / 2u;
    step_t *s;
    if (next)
        idx = (idx + 1u) % len;
    s = &t->step[idx];
    if (s->time != ST_NOTE || !s->n || (!is_drum(t) && t->p[P_VOICE] != V_POLY)) {
        s->n = 0;                                   /* a fresh step */
        s->flags = 0;
        s->vel = 0;
    }
    for (k = 0; k < s->n && s->note[k] != note; k++)
        ;
    if (k == s->n) {
        if (s->n < 4u)
            s->n++;
        s->note[s->n - 1u] = (uint8_t)note;
    }
    s->time = ST_NOTE;
    if (vel > 110)
        s->flags |= SF_ACCENT;
    if (vel > s->vel)
        s->vel = (uint8_t)vel;
    t->seq_active = 1;
    if (next) {                                     /* it sounds now: the step must not trigger it again */
        if (t->rskip_idx != idx)
            t->rskip_n = 0;
        t->rskip_idx = (uint8_t)idx;
        if (t->rskip_n < 4u)
            t->rskip[t->rskip_n++] = (uint8_t)note;
    }
    if (is_drum(t))
        return;                                     /* hits: no length */
    if (!t->rh_n || t->rh_start != idx) {           /* a new hold (one in another step ends) */
        t->rh_n = 0;
        t->rh_start = (uint8_t)idx;
        t->rh_ties = 0;
    }
    for (k = 0; k < t->rh_n && t->rh_note[k] != note; k++)
        ;
    if (k == t->rh_n && t->rh_n < 4u)
        t->rh_note[t->rh_n++] = (uint8_t)note;      /* a chord: held until its last key is up */
}

/* the sequencer enters step idx (before playing it): a recorded note still held ties into it */
static void rec_hold(track_t *t, uint32_t idx, uint32_t len)
{
    step_t *s;
    uint32_t k;
    if (!t->rh_n)
        return;
    if (!((song.rec >> trk_index(t)) & 1u) || t->rh_ties + 1u >= len) {
        t->rh_n = 0;                                /* disarmed, or the whole pattern is this note */
        return;
    }
    if (idx == t->rh_start)
        return;                                     /* (recorded ahead into the step now starting) */
    s = &t->step[idx];
    t->rh_bak = *s;
    t->rh_last = (uint8_t)idx;
    t->rh_ties++;
    for (k = 0; k < 4u; k++)
        s->note[k] = 0;
    s->n = 0;
    s->time = ST_TIE;
    s->flags = 0;
    s->vel = 0;
}

/* a key of a recorded note is up: the hold ends with the last one */
static void rec_release(track_t *t, uint32_t note)
{
    uint32_t i, k = 0;
    for (i = 0; i < t->rh_n; i++)
        if (t->rh_note[i] != note)
            t->rh_note[k++] = t->rh_note[i];
    if (k == t->rh_n || (t->rh_n = (uint8_t)k))
        return;                                     /* not one of them, or others still held */
    if (t->rh_ties && t->seq_idx == t->rh_last &&
        t->seq_pos < seq_len(t, t->seq_idx) / 2u)
        t->step[t->rh_last] = t->rh_bak;            /* released early in it: not held into this step */
}

static void input_on(track_t *t, uint32_t note, uint32_t vel)
{
    t->sus_held[(note >> 5) & 3u] &= ~(1u << (note & 31u));   /* (pressed again: its own key-up ends it) */
    last_note = (uint8_t)note;
    if (((song.rec >> trk_index(t)) & 1u) && song.playing)
        rec_note(t, note, vel);
    if (t->p[P_AMODE] && !is_drum(t))
        arp_add(t, note);
    else
        trk_note_on(t, note, vel);
}

static void input_off(track_t *t, uint32_t note)
{
    if (t->sus && !is_drum(t)) {                    /* Jangada: the sustain pedal holds it (midi_cc) */
        t->sus_held[(note >> 5) & 3u] |= 1u << (note & 31u);
        return;
    }
    rec_release(t, note);
    arp_remove(t, note);                            /* both: the note may have started in the */
    trk_note_off(t, note);                          /* other mode (ARP switched while held) */
}

/* Jangada: the layer the keys belong to (ui_layers.c sets it: a layer button held or locked open).
 * LY_FX runs here (punch.c); the other layers' key-downs go to the UI through lk_q */
enum { LY_NONE, LY_FX, LY_MIX, LY_STEP, LY_SCALE, LY_ENGINE, LY_COUNT };
static volatile uint8_t kb_layer;
#define LKQ 32u
#define LK_UP 0x80u                                   /* lk_q: key index | LK_UP (a key-up) | layer << 8 */
static volatile uint16_t lk_q[LKQ];
static volatile uint8_t lk_w;
static volatile uint32_t lk_t[LKQ];
static uint8_t lk_r;
static uint32_t kb_lkeys;                             /* keys down that belong to a UI layer (their key-up goes too) */
static void lk_put(uint32_t v)
{
    if ((uint8_t)(lk_w - lk_r) < LKQ) {
        lk_t[lk_w % LKQ] = fm1_ms;
        lk_q[lk_w % LKQ] = (uint16_t)v;
        lk_w++;
    }
}

static void keyboard_block(void)
{
    uint32_t cur = fm1_in.notes, ch, k;
    ch = cur ^ kb_prev;                           /* keys also sound while entering steps */
    if (!ch)
        return;
    for (k = 0; k < 27u; k++) {
        uint32_t mc;
        if (!((ch >> k) & 1u))
            continue;
        if ((cur >> k) & 1u) {                    /* the selected track; the key-up goes to the same one */
            if (kb_layer) {                       /* Jangada: a layer is held (ui_layers.c): no note, */
                kb_note[k] = KB_SILENT;           /* and none to release */
                if (kb_layer == LY_FX) {          /* FX: a white key = a punch-in effect (punch.c) */
                    int32_t fx = punch_key(k);
                    if (fx >= 0) {
                        punch.req = (int8_t)fx;
                        punch.keybit = 1u << k;
                    }
                } else {                          /* the others: to the UI, with the time */
                    kb_lkeys |= 1u << k;
                    lk_put(k | (uint32_t)kb_layer << 8);
                }
                continue;
            }
            kb_trk[k] = song.sel;
            kb_note[k] = (uint8_t)kb_map(&trk[kb_trk[k]], k);
            kb_n[k] = 0;
            if (kb_note[k] == KB_SILENT)
                continue;
            mc = trk_midi_ch(kb_trk[k]);
            if (trk[kb_trk[k]].p[P_CHORD] && !is_drum(&trk[kb_trk[k]])) {   /* Jangada: CHORD, the whole chord */
                uint32_t i;
                kb_n[k] = (uint8_t)chord_notes(&trk[kb_trk[k]], kb_note[k], kb_nt[k]);
                for (i = 0; i < kb_n[k]; i++) {
                    input_on(&trk[kb_trk[k]], kb_nt[k][i], 100);
                    midi_out_event(0x09u | (0x90u | mc) << 8 | (uint32_t)kb_nt[k][i] << 16 | 100u << 24);
                }
                continue;
            }
            input_on(&trk[kb_trk[k]], kb_note[k], 100);
            midi_out_event(0x09u | (0x90u | mc) << 8 | (uint32_t)kb_note[k] << 16 | 100u << 24);
        } else {
            if (punch.keybit == 1u << k) {        /* the punch-in key is up: the mix comes back */
                punch.keybit = 0;
                punch.req = -1;
            }
            if (kb_lkeys & 1u << k) {             /* a UI layer's key is up (its layer may be gone) */
                kb_lkeys &= ~(1u << k);
                lk_put(k | LK_UP);
            }
            if (kb_note[k] == KB_SILENT)
                continue;
            if (kb_n[k]) {                        /* a chord: all of its notes */
                uint32_t i;
                mc = trk_midi_ch(kb_trk[k] % NTRK);
                for (i = 0; i < kb_n[k]; i++) {
                    input_off(&trk[kb_trk[k] % NTRK], kb_nt[k][i]);
                    midi_out_event(0x08u | (0x80u | mc) << 8 | (uint32_t)kb_nt[k][i] << 16);
                }
                kb_n[k] = 0;
                continue;
            }
            input_off(&trk[kb_trk[k] % NTRK], kb_note[k]);
            mc = trk_midi_ch(kb_trk[k] % NTRK);
            midi_out_event(0x08u | (0x80u | mc) << 8 | (uint32_t)kb_note[k] << 16);
        }
    }
    kb_prev = cur;
}

static void midi_rt_out(uint32_t b);                /* Jangada: MIDI clock out (below) */
static uint32_t mclk_out;
static void mclk_restart(void);

/* ---------------------------------- nudges, fills and parameter locks --- */
/* Jangada, after SLOOP 2.4 (isod89/sloop-fm1, GPL-3.0). core.h seqx_t: a byte a step (nudge, condition)
 * and NLOCK locks a track. A lock (Elektron style): on its step the track's p[param] takes its value and
 * goes back to what it was at the next step without a lock on it (notes still ringing follow: the engines
 * read p[] every block). The locks in force are listed in lk_* (param, the base to go back to, the value
 * set); a knob turned meanwhile wins: what it found is the new base. Only the sound locks (p_lockable):
 * not the sequencer, the arp, the key, the voice mode, FM6's PTCH (a patch load). */
static int32_t step_micro(const track_t *t, uint32_t idx)
{
    int32_t m = (int32_t)(t->x.sx[idx % NSTEP] & SX_MICRO);
    return m > MICRO_MAX ? m - 64 : m;
}
static uint32_t step_cond(const track_t *t, uint32_t idx) { return (uint32_t)t->x.sx[idx % NSTEP] >> SX_COND_SH; }
static void step_micro_set(track_t *t, uint32_t idx, int32_t m)
{
    uint8_t *b = &t->x.sx[idx % NSTEP];
    *b = (uint8_t)((*b & ~SX_MICRO) | ((uint32_t)clamp(m, MICRO_MIN, MICRO_MAX) & SX_MICRO));
}
static void step_cond_set(track_t *t, uint32_t idx, uint32_t c)
{
    uint8_t *b = &t->x.sx[idx % NSTEP];
    *b = (uint8_t)((*b & SX_MICRO) | (c % 3u) << SX_COND_SH);
}
/* the range of p[id] on track t (eng_req: the engine p[] holds the values of, also while the old one fades) */
static const param_desc_t *lock_desc(const track_t *t, uint32_t id)
{
    if (id >= P_E0 && id < P_E0 + NEDIT)
        return &ENGINES[t->eng_req % NENGINES]->edit[id - P_E0];
    return &TP[id % P_COUNT];
}
static int p_lockable(const track_t *t, uint32_t id)
{
    const param_desc_t *d;
    if (is_drum(t) || id >= P_COUNT)
        return 0;
    if (id >= P_E0 && id < P_E0 + NEDIT) {
        d = lock_desc(t, id);
        if (!d->label || !d->label[0] || d->label[0] == '-' || (ENGINES[t->eng_req % NENGINES] == &ENG_FM6 && id == P_E7))
            return 0;
    } else if (!(id <= P_LD_AMP || id == P_SGATE || (id >= P_DIST && id <= P_REV) || id == P_GLIDE ||
                 id == P_PAN || id == P_DETUNE || id == P_SLDEPTH || id == P_M1AMT || id == P_M2AMT ||
                 id == P_M3AMT || id == P_M4AMT || id == P_DRING || id == P_TENS || id == P_TFLT) || id == P_LWAVE) {
        return 0;
    }
    d = lock_desc(t, id);
    return d->min >= -128 && d->max <= 127;              /* (plock_t.val) */
}
static void lock_write(track_t *t, uint32_t id, int32_t v)
{
    const param_desc_t *d = lock_desc(t, id);
    t->p[id % P_COUNT] = (int16_t)clamp(v, d->min, d->max);
}
static void locks_restore(track_t *t)               /* every lock in force let go (STOP, a load, a cleared pattern) */
{
    uint32_t i;
    for (i = 0; i < t->lk_n && i < NLOCK; i++)
        if (t->p[t->lk_param[i] % P_COUNT] == t->lk_set[i])
            lock_write(t, t->lk_param[i], t->lk_base[i]);
    t->lk_n = 0;
}
/* p[id] without the lock in force on it (what a save keeps) */
static int16_t p_unlocked(const track_t *t, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < t->lk_n && i < NLOCK; i++)
        if (t->lk_param[i] == id && t->p[id] == t->lk_set[i])
            return t->lk_base[i];
    return t->p[id % P_COUNT];
}
static void seqx_clear(track_t *t)                  /* no nudge, no condition, no lock */
{
    uint32_t i;
    locks_restore(t);
    memset(t->x.sx, 0, sizeof t->x.sx);
    for (i = 0; i < NLOCK; i++) {
        t->x.lock[i].step = LOCK_FREE;
        t->x.lock[i].param = 0;
        t->x.lock[i].val = 0;
    }
}
static void lock_del(track_t *t, uint32_t step, uint32_t param)   /* param P_COUNT: every lock of the step */
{
    uint32_t k;
    for (k = 0; k < NLOCK; k++)
        if (t->x.lock[k].step == step + 1u && (param >= P_COUNT || t->x.lock[k].param == param))
            t->x.lock[k].step = LOCK_FREE;
}
static void stepx_clear(track_t *t, uint32_t idx)   /* a step cleared: its nudge, condition and locks too */
{
    t->x.sx[idx % NSTEP] = 0;
    lock_del(t, idx % NSTEP, P_COUNT);
}
/* the sequencer plays step idx (NSTEP: none): its locks take hold, the last step's it does not share let go */
static void lock_step(track_t *t, uint32_t idx)
{
    uint32_t i, k, n = 0;
    for (i = 0; i < t->lk_n && i < NLOCK; i++) {      /* in force, no lock here: back to the base (or the knob) */
        uint32_t p = t->lk_param[i] % P_COUNT, has = 0;
        for (k = 0; k < NLOCK; k++)
            if (t->x.lock[k].step == idx + 1u && t->x.lock[k].param == p)
                has = 1;
        if (has) {
            t->lk_param[n] = (uint8_t)p;
            t->lk_base[n] = t->lk_base[i];
            t->lk_set[n] = t->lk_set[i];
            n++;
        } else if (t->p[p] == t->lk_set[i]) {
            lock_write(t, p, t->lk_base[i]);
        }
    }
    t->lk_n = (uint8_t)n;
    for (k = 0; k < NLOCK; k++) {                     /* this step's locks */
        const plock_t *l = &t->x.lock[k];
        uint32_t p = l->param;
        if (l->step != idx + 1u || !p_lockable(t, p))
            continue;
        for (i = 0; i < t->lk_n && t->lk_param[i] != p; i++)
            ;
        if (i == t->lk_n) {
            if (i >= NLOCK)
                continue;
            t->lk_param[i] = (uint8_t)p;
            t->lk_base[i] = t->p[p];
            t->lk_n++;
        } else if (t->p[p] != t->lk_set[i]) {
            t->lk_base[i] = t->p[p];                  /* turned meanwhile: that is the new base */
        }
        lock_write(t, p, l->val);
        t->lk_set[i] = t->p[p];
    }
}
/* the lock of (step, param): its slot, or with make a free one for it (-1: none) */
static int lock_find(const track_t *t, uint32_t step, uint32_t param, int make)
{
    uint32_t k;
    int fr = -1;
    for (k = 0; k < NLOCK; k++) {
        if (t->x.lock[k].step == step + 1u && t->x.lock[k].param == param)
            return (int)k;
        if (fr < 0 && t->x.lock[k].step == LOCK_FREE)
            fr = (int)k;
    }
    return make ? fr : -1;
}
/* set (or make) the lock of (step, param) at v, clamped; 0 = no slot left or not lockable. IRQ off */
static int lock_set(track_t *t, uint32_t step, uint32_t param, int32_t v)
{
    int k;
    const param_desc_t *d;
    if (step >= NSTEP || !p_lockable(t, param) || (k = lock_find(t, step, param, 1)) < 0)
        return 0;
    d = lock_desc(t, param);
    t->x.lock[k].step = (uint8_t)(step + 1u);
    t->x.lock[k].param = (uint8_t)param;
    t->x.lock[k].val = (int8_t)clamp(v, d->min, d->max);
    return 1;
}
static int step_marked(const track_t *t, uint32_t step)   /* a nudge or a lock (the UI's mark) */
{
    uint32_t k;
    if (step_micro(t, step))
        return 1;
    for (k = 0; k < NLOCK; k++)
        if (t->x.lock[k].step == step + 1u)
            return 1;
    return 0;
}
/* the fill: GLO + key 9 held, or key 10 for the next bar. FC_FILL steps play only then, FC_NOFILL never */
static volatile uint8_t fill_held;                   /* (the UI) */
static volatile uint8_t fill_arm;                    /* the next bar is a fill (the UI; the ISR takes it) */
static uint8_t fill_bar_on;                          /* that bar, while it plays (the ISR) */
static uint8_t fill_now;                             /* this block is a fill (events_block) */
static uint32_t fill_bar = 0xFFFFFFFFu;              /* the bar fill_bar_on was decided for */
static uint32_t step_plays(const track_t *t, uint32_t idx)
{
    uint32_t c = step_cond(t, idx);
    return c == FC_FILL ? fill_now : c == FC_NOFILL ? !fill_now : 1u;
}

/* -------------------------------------------------------- sequencer --- */
static void seq_start(void)
{
    uint32_t i;
    for (i = 0; i < NTRK; i++) {                   /* every track from its step 0, together */
        track_t *t = &trk[i];
        t->seq_idx = (uint16_t)(t->p[P_SLEN] - 1);
        t->seq_pos = 0x7FFFFFFF;                   /* step 0 fires on the first block */
        t->rskip_n = 0;
        t->rh_n = 0;
        t->mx_due = t->mx_early = 0;
    }
    song.tick = 0;
    fill_bar = 0xFFFFFFFFu;
    clk_pos = 0;                                   /* fx.c: the beat clock, step 0 on the beat */
    clk_beat = 0;
    mclk_out = 0;
    mclk_restart();                                /* following a clock: from its next pulse */
    if (song.g[G_SYNC] == 1 && !song.g[G_CLOCK])
        midi_rt_out(0xFAu);                        /* Jangada: MIDI START (SYNC OUT), not while following */
    song.playing = 1;
    slicer_start();                                /* slicer.c: its step 0 with the sequencer's */
}

static void seq_release(track_t *t)
{
    uint32_t i;
    for (i = 0; i < t->seq_n; i++)
        trk_note_off(t, t->seq_notes[i]);
    t->seq_n = 0;
    t->seq_hold = 0;
    t->slide_glide = 0;                             /* live MONO / LEG keys must not glide after it */
}

static void seq_stop(void)
{
    uint32_t i;
    if (song.playing && song.g[G_SYNC] == 1 && !song.g[G_CLOCK])
        midi_rt_out(0xFCu);                        /* Jangada: MIDI STOP (SYNC OUT) */
    song.playing = 0;
    for (i = 0; i < NTRK; i++) {
        seq_release(&trk[i]);
        trk[i].rat_left = 0;
        trk[i].rh_n = 0;                           /* a recorded note held over the stop: as far as it got */
        locks_restore(&trk[i]);                    /* the parameters back to their base */
    }
    fill_held = fill_arm = fill_bar_on = 0;        /* STOP ends a fill, held or armed */
}

/* play one step: TIE extends, REST releases, NOTE (re)triggers; a SLIDE on
 * the previous step makes this one legato with a glide (acid style). skip: bit k =
 * note k already sounds from live recording (not triggered, not released here).
 * The drum track: each note is a hit (drum_on through trk_note_on), nothing else. */
static void seq_step(track_t *t, uint32_t idx, uint32_t period, uint32_t skip)
{
    const step_t *s = &t->step[idx % NSTEP];
    uint32_t i, j, gate = period * (uint32_t)t->p[P_SGATE] / 128u;
    uint32_t vel = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    uint32_t slide_in = t->seq_hold && t->seq_n;
    uint32_t len = t->p[P_SLEN] ? (uint32_t)t->p[P_SLEN] : 1u;
    uint32_t next_tie = t->step[(idx + 1u) % len].time == ST_TIE;
    uint32_t hits = ((s->flags & SF_RATCH) >> SF_RATCH_SH) + 1u, chance = (s->flags & SF_CHANCE) >> SF_CHANCE_SH;
    t->rat_left = 0;
    if (s->time == ST_NOTE && s->n && chance && (rng() & 3u) < chance) {   /* CHNC: this pass rests */
        if (!is_drum(t))
            seq_release(t);
        return;
    }
    if (s->time == ST_NOTE && s->n && hits > 1u) {  /* RTCH: the step in equal hits, no slide out */
        t->rat_left = (uint8_t)(hits - 1u);
        t->rat_idx = (uint8_t)idx;
        t->rat_pos = 0;
        t->rat_sub = step_samples(t, period, idx) / hits;   /* the swung length of this step */
        t->rat_gate = gate = t->rat_sub * (uint32_t)t->p[P_SGATE] / 128u;
        next_tie = 0;
    }
    if (s->time == ST_TIE) {
        if (t->seq_n) {
            t->seq_off = gate + period / 2u;
            t->seq_hold = (s->flags & SF_SLIDE) != 0 || next_tie;   /* chains hold at any GATE / swing */
        }
        return;
    }
    if (is_drum(t)) {
        if (s->time == ST_NOTE)
            for (i = 0; i < s->n; i++)
                if (!((skip >> i) & 1u))
                    trk_note_on(t, s->note[i], vel);
        return;
    }
    if (s->time == ST_REST || !s->n) {
        seq_release(t);
        return;
    }
    t->slide_glide = (uint8_t)slide_in;
    if (!slide_in)
        seq_release(t);
    for (i = 0; i < s->n; i++)
        if (!((skip >> i) & 1u))
            trk_note_on(t, s->note[i], vel);
    if (slide_in)                                   /* release what is not held over */
        for (i = 0; i < t->seq_n; i++) {
            for (j = 0; j < s->n && s->note[j] != t->seq_notes[i]; j++)
                ;
            if (j == s->n)
                trk_note_off(t, t->seq_notes[i]);
        }
    t->seq_n = 0;
    for (i = 0; i < s->n; i++)
        if (!((skip >> i) & 1u))
            t->seq_notes[t->seq_n++] = s->note[i];
    t->seq_off = gate;
    t->seq_hold = !t->rat_left && ((s->flags & SF_SLIDE) != 0 || next_tie);   /* next step a TIE: keep the notes to it */
}

/* RTCH: the next hit of the playing step (Jangada) */
static void seq_ratchet(track_t *t, uint32_t n)
{
    const step_t *s = &t->step[t->rat_idx];
    uint32_t i, vel = (s->flags & SF_ACCENT) ? 127u : (s->vel ? s->vel : 96u);
    t->rat_pos += n;
    if (t->rat_pos < t->rat_sub)
        return;
    t->rat_pos -= t->rat_sub;
    t->rat_left--;
    if (!is_drum(t)) {
        seq_release(t);
        for (i = 0; i < s->n; i++)
            t->seq_notes[t->seq_n++] = s->note[i];
        t->seq_off = t->rat_gate;
    }
    for (i = 0; i < s->n; i++)
        trk_note_on(t, s->note[i], vel);
}

/* step idx fires (its nudged time): its condition, its locks, then the step itself */
static void seq_fire(track_t *t, uint32_t idx, uint32_t period)
{
    const step_t *s = &t->step[idx % NSTEP];
    uint32_t skip = 0, i, k;
    if (!step_plays(t, idx)) {                      /* its fill condition fails: as a REST, no lock */
        lock_step(t, NSTEP);
        t->rat_left = 0;
        t->rskip_n = 0;
        if (!is_drum(t))
            seq_release(t);
        return;
    }
    lock_step(t, idx);                              /* its parameter locks, before the block renders */
    if (t->rskip_n && t->rskip_idx == idx) {
        for (i = 0; i < s->n; i++)
            for (k = 0; k < t->rskip_n; k++)
                if (s->note[i] == t->rskip[k])
                    skip |= 1u << i;
        t->rskip_n = 0;
    }
    seq_step(t, idx, period, skip);
}

/* n samples; adv: the units seq_pos moves (n x BPM, or the MIDI clock's: seq_u). The gates and the
 * ratchets count samples. Jangada (after SLOOP 2.4): a step fires at its nudge, 1/64 of its
 * length a unit: late, once the grid is that far into it (mx_due until then); early, that far before
 * its grid step begins (mx_early: the grid then enters it fired already). The steps keep their order
 * whatever the nudges (the next one fires early only after the one before has fired); the recording,
 * the playhead and the page stay on the grid (seq_idx) */
static void seq_tick(track_t *t, uint32_t n, uint32_t adv)
{
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]), len = (uint32_t)t->p[P_SLEN];
    if (t->seq_n && !t->seq_hold) {
        if (t->seq_off <= n)
            seq_release(t);
        else
            t->seq_off -= n;
    }
    if (!song.playing)
        return;
    if (t->rat_left)
        seq_ratchet(t, n);
    if (!adv)
        return;                                    /* (the clock's START: waiting for its first pulse) */
    if (!len)
        len = 1;
    t->seq_pos += adv;
    for (;;) {
        uint32_t cur_len = seq_len(t, t->seq_idx), nidx = (t->seq_idx + 1u) % len;
        int32_t m;
        if (t->mx_due && (int32_t)t->seq_pos >= (int32_t)(cur_len / 64u) * step_micro(t, t->seq_idx)) {
            t->mx_due = 0;                          /* (nudged early but not fired yet, as after PLAY: now) */
            seq_fire(t, t->seq_idx, period);
        }
        if (!t->mx_due && !t->mx_early && (m = step_micro(t, nidx)) < 0 &&
            t->seq_pos + seq_len(t, nidx) / 64u * (uint32_t)-m >= cur_len) {
            t->mx_early = 1;                        /* the next step, nudged early into this one */
            seq_fire(t, nidx, period);
        }
        if (t->seq_pos < cur_len && t->seq_pos != 0x7FFFFFFFu + adv)
            break;
        if (t->mx_due) {                            /* (a late step the grid left behind: before the next) */
            t->mx_due = 0;
            seq_fire(t, t->seq_idx, period);
        }
        t->seq_pos = t->seq_pos >= 0x7FFFFFFFu ? (seq_u ? adv : 0u) : t->seq_pos - cur_len;   /* (step 0 at the
                                                    * block start; the clock counts the block from there) */
        t->seq_idx = (uint16_t)nidx;
        rec_hold(t, t->seq_idx, len);
        t->mx_due = (uint8_t)!t->mx_early;          /* fired early: nothing more of it */
        t->mx_early = 0;
    }
}

/* MIDI in: the track a channel plays (0..15) */
static track_t *midi_track(uint32_t ch)
{
    if (song.g[G_DRCH] && ch + 1u == (uint32_t)song.g[G_DRCH] && is_drum(TDRUM))
        return TDRUM;
    return ch < NTRK && trk_synth(ch) ? &trk[ch] : TSEL;
}

/* a channel that plays the selected track: its note-off goes to the track its note-on went to,
 * even when another track was selected in between (else that note would hang) */
static uint8_t midi_sel_on[16][128];                  /* per channel and note: track + 1, 0 = none */
static track_t *midi_route(uint32_t ch, uint32_t note, int on)
{
    track_t *t = midi_track(ch);
    if ((ch < NTRK && trk_synth(ch)) || (song.g[G_DRCH] && ch + 1u == (uint32_t)song.g[G_DRCH] && is_drum(TDRUM)))
        return t;                                     /* a part's own channel, or the drum channel */
    if (on)
        midi_sel_on[ch & 15u][note & 127u] = (uint8_t)(song.sel + 1u);
    else if (midi_sel_on[ch & 15u][note & 127u]) {
        t = &trk[(midi_sel_on[ch & 15u][note & 127u] - 1u) % NTRK];
        midi_sel_on[ch & 15u][note & 127u] = 0;
    }
    return t;
}

/* ---- Jangada: MIDI beyond notes. Controllers of a channel go to the track it plays (midi_track) */
static void sustain_release(track_t *t)             /* the pedal up: the notes it held are let go */
{
    uint32_t w, b;
    t->sus = 0;
    for (w = 0; w < 4u; w++)
        for (b = 0; b < 32u; b++)
            if ((t->sus_held[w] >> b) & 1u)
                input_off(t, w * 32u + b);
    t->sus_held[0] = t->sus_held[1] = t->sus_held[2] = t->sus_held[3] = 0;
}

static void midi_cc(track_t *t, uint32_t cc, uint32_t v)
{
    switch (cc) {
    case 1:                                         /* MOD WHEEL (mod.c MODW) */
        t->mw = (uint8_t)v;
        break;
    case 11:                                        /* EXPRESSION (mod.c EXPR) */
        t->ex = (uint8_t)v;
        break;
    case 64:                                        /* SUSTAIN */
        if (v >= 64u)
            t->sus = 1;
        else if (t->sus)
            sustain_release(t);
        break;
    case 121:                                       /* RESET ALL CONTROLLERS */
        t->bend16 = 0;
        t->mw = t->at = t->ex = 0;
        if (t->sus)
            sustain_release(t);
        break;
    case 120:                                       /* ALL SOUND OFF, ALL NOTES OFF: the track goes quiet */
    case 123:
        t->sus = 0;
        t->sus_held[0] = t->sus_held[1] = t->sus_held[2] = t->sus_held[3] = 0;
        panic_req |= (uint8_t)(1u << trk_index(t));
        break;
    default:
        break;
    }
}

/* MIDI clock in (GLO > GLOBAL CLK USB or TRS; Jangada, after SLOOP 2.3 / Felucca 1.0's midi_clock.c,
 * from contributions by ChanceTheMaker and keremimo): 24 pulses a beat. While the clock runs, the
 * sequencer and the beat clock (fx.c clk_pos: punch-in FX, DUCK, the TRACKS screen) advance by the
 * pulses (a pulse = BEAT_U / 24 units), interpolated up to the next one from the last interval but never
 * past it, so they follow the master's tempo changes and cannot drift (0.2 measured the tempo every 6
 * pulses and ran on its own clock: it drifted). BPM shows the master's tempo, every 24 pulses (the arp,
 * delay and SLICER follow it). START restarts from the top, CONTINUE carries on where it stopped, STOP
 * stops. With no pulse for 0.5 s the internal clock takes over (PLAY works as ever). Time is counted in
 * samples (midi_now): the queue is read at block starts, so arrivals and the interpolation share it.
 * Out (GLO > SYSTEM SYNC OUT): 24 clocks a beat from the beat clock, START / STOP with the transport;
 * never while CLK is USB or TRS. */
static uint32_t midi_now;                           /* samples since boot (block resolution) */
#define MCLK_PULSE_U (BEAT_U / 24u)
#define MCLK_GONE (FS / 2u)                         /* no pulse for this long: the internal clock */
static struct {
    uint32_t pos, done;          /* units: the master's position (pulses since START), ours */
    uint32_t last, iv;           /* samples: the last pulse, the interval between pulses (smoothed) */
    uint32_t beat;               /* when pulse 0 of the last 24 came: the tempo */
    uint8_t have, n24;           /* a pulse since START; pulses towards the next tempo reading */
    uint8_t alive;               /* pulses are coming (from the CLK source) */
    uint8_t wait;                /* START / CONTINUE seen, its first pulse not yet (since `start`) */
    uint32_t start;
} mclk;

static int mclk_on(void)                            /* the clock drives the sequencer */
{
    return song.g[G_CLOCK] && ((mclk.alive && midi_now - mclk.last < MCLK_GONE) ||
                               (mclk.wait && midi_now - mclk.start < MCLK_GONE));   /* (a START waits for its pulse) */
}

static void mclk_restart(void)                      /* START, or PLAY: the next pulse is the downbeat */
{
    mclk.pos = mclk.done = 0;
    mclk.have = 0;
}

static __attribute__((noinline)) void midi_clock_in(uint32_t b, uint32_t src)   /* a realtime byte; src 1 USB, 2 TRS */
{
    uint32_t now = midi_now;
    if (!song.g[G_CLOCK] || src != (uint32_t)song.g[G_CLOCK])
        return;
    if (b == 0xFAu || b == 0xFBu) {                 /* START, CONTINUE: the clock drives from now on */
        mclk.wait = 1;
        mclk.start = now;
    }
    if (b == 0xFAu) {                               /* START: from the top (seq_start: mclk_restart) */
        transport_req = 1;
        return;
    }
    if (b == 0xFBu) {                               /* CONTINUE: on from where it stopped, the next pulse one */
        if (!song.playing) {                        /* pulse on from the last one (what was played past it */
            uint32_t past = mclk.have ? mclk.done - mclk.pos : 0u;   /* counts) */
            mclk.pos = 0;
            mclk.done = past < MCLK_PULSE_U ? past : 0u;
            mclk.have = 1;
            song.playing = 1;                       /* (no seq_start: the steps stay where they are) */
        }
        return;
    }
    if (b == 0xFCu) {                               /* STOP */
        transport_req = 2;
        return;
    }
    if (b != 0xF8u)
        return;
    if (mclk.alive && now - mclk.last < FS / 5u) {  /* the interval, smoothed (a gap is not a tempo) */
        uint32_t iv = now - mclk.last;
        mclk.iv = mclk.iv ? (mclk.iv * 3u + iv + 2u) / 4u : iv;
    }
    if (!mclk.alive || now - mclk.last >= MCLK_GONE) {   /* (re)started: count a fresh beat */
        mclk.n24 = 0;
        mclk.beat = now;
    } else if (++mclk.n24 == 24u) {                 /* a beat: the tempo */
        uint32_t dt = now - mclk.beat;
        mclk.n24 = 0;
        mclk.beat = now;
        if (dt)
            song.g[G_BPM] = (int16_t)clamp((int32_t)((60u * FS + dt / 2u) / dt), GP[G_BPM].min, GP[G_BPM].max);
    }
    mclk.alive = 1;
    mclk.last = now;
    mclk.wait = 0;
    if (song.playing || transport_req == 1u) {      /* (a START queued with it: the next block starts) */
        if (mclk.have)
            mclk.pos += MCLK_PULSE_U;
        mclk.have = 1;                              /* the first pulse after START is the downbeat */
    }
}

static __attribute__((noinline)) uint32_t mclk_adv(uint32_t n)   /* units to advance this block (mclk_on) */
{
    uint32_t el, off = 0, tgt, adv, cap = n * 2u * (uint32_t)song.g[G_BPM];
    if (!mclk.have)
        return 0;                                   /* START seen: wait for the downbeat */
    el = midi_now - mclk.last;
    if (mclk.iv) {
        if (el > mclk.iv)
            el = mclk.iv;
        off = el * MCLK_PULSE_U / mclk.iv;          /* (iv < FS / 5: el x 110250 fits 32 bits) */
        if (off >= MCLK_PULSE_U)
            off = MCLK_PULSE_U - 1u;
    }
    tgt = mclk.pos + off;
    adv = (int32_t)(tgt - mclk.done) > 0 ? tgt - mclk.done : 0u;
    if (adv > cap)
        adv = cap;                                  /* behind: catch up at twice the tempo, no burst */
    mclk.done += adv;
    return adv;
}

/* a clock comes or goes (seq_pos counts units either way, seq_len) */
static void seq_units(uint32_t on) { seq_u = (uint8_t)on; }

static void midi_rt_out(uint32_t b) { midi_out_event(0x0Fu | b << 8); }

static void midi_clock_out(void)                    /* after the beat clock moved */
{
    uint32_t want;
    if (song.g[G_SYNC] != 1 || song.g[G_CLOCK] || !song.playing)
        return;
    want = clk_beat * 24u + clk_pos / (BEAT_U / 24u) + 1u;   /* the clocks due, the first one at step 0 */
    while (mclk_out < want && want - mclk_out < 8u) {
        midi_rt_out(0xF8u);
        mclk_out++;
    }
    mclk_out = want;
}

/* everything that happens between two rendered blocks */
static uint32_t clk_adv;                            /* units the beat clock moved this block (fx.c DUCK) */
static void events_block(uint32_t n)
{
    uint32_t i, pr, adv;
    if (transport_req == 1u) {
        seq_start();
        transport_req = 0;
    } else if (transport_req == 2u) {
        seq_stop();
        transport_req = 0;
    }
    pr = panic_req;
    panic_req = 0;
    {
        uint32_t lo = latch_off_req;
        latch_off_req = 0;
        for (i = 0; i < NTRK; i++)
            if ((lo >> i) & 1u && !trk[i].arp_phys) {   /* keys still down: their own chord, kept */
                trk[i].nheld = 0;
                arp_silence(&trk[i]);
            }
        lo = hush_req;
        hush_req = 0;
        for (i = 0; i < NTRK; i++)
            if ((lo >> i) & 1u) {
                uint32_t j;
                for (j = 0; j < NVOICE; j++)
                    if (trk[i].v[j].active && !trk[i].v[j].gate)
                        voice_kill(&trk[i].v[j]);       /* the release tails only: held keys keep sounding */
            }
    }
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        if ((pr >> i) & 1u) {
            trk_all_off(t);
            t->nheld = 0;
            t->arp_phys = 0;
            t->arp_note = 0;
            t->arp_nch = 0;
        }
        if (trk_synth(i))
            engine_block(t);                          /* engine switch: fade, then switch (voice.c) */
        /* ARP turned off, or HOLD released with no key down: drop the latched chord */
        if ((t->armp && !t->p[P_AMODE]) || (t->aholdp && !t->p[P_AHOLD] && !t->arp_phys)) {
            t->nheld = 0;
            if (!t->p[P_AMODE])
                t->arp_phys = 0;
            arp_silence(t);
        }
        t->armp = t->p[P_AMODE];
        t->aholdp = t->p[P_AHOLD];
    }
    keyboard_block();
    while (mi_r != mi_w) {                            /* USB-MIDI (and TRS) in */
        uint32_t pkt, st, ch, d1, d2;
        RING_PUBLISH();                               /* Jangada: the slot after the index, as ep1_tx */
        pkt = midi_in_q[mi_r % MQ];
        st = (pkt >> 8) & 0xF0u;
        ch = (pkt >> 8) & 0x0Fu;
        d1 = (pkt >> 16) & 0x7Fu;
        d2 = (pkt >> 24) & 0x7Fu;
        mi_r++;
        if ((pkt & 0x0Fu) == 0x0Fu) {                  /* (Jangada: real time, the byte is the status; */
            midi_clock_in((pkt >> 8) & 0xFFu, (pkt & 0xF0u) ? 2u : 1u);   /* cable 0 USB, 1 the TRS jack) */
            continue;
        }
        if (st == 0x90u && d2)
            input_on(midi_route(ch, d1, 1), d1, d2);
        else if (st == 0x80u || st == 0x90u)
            input_off(midi_route(ch, d1, 0), d1);
        else if (st == 0xE0u)                         /* Jangada: PITCH BEND, +-2 semitones */
            midi_track(ch)->bend16 = (int16_t)((((int32_t)(d2 << 7 | d1) - 8192) * 32) / 8192);
        else if (st == 0xB0u)
            midi_cc(midi_track(ch), d1, d2);
        else if (st == 0xD0u)                         /* channel AFTERTOUCH (mod.c AT) */
            midi_track(ch)->at = (uint8_t)d1;
    }
    midi_now += n;
    if ((uint32_t)mclk_on() != seq_u)
        seq_units((uint32_t)mclk_on());             /* a clock came, or stopped coming */
    adv = seq_u && song.playing ? mclk_adv(n) : n * (uint32_t)song.g[G_BPM];   /* units */
    clk_adv = adv;
    if (song.playing) {                               /* the fill: the bar this block reaches (its downbeat step
                                                       * fires in it, before clk_beat moves), 1/16 beat ahead: a
                                                       * downbeat nudged a little early is the new bar's */
        uint32_t bar = (clk_beat + (clk_pos + adv + BEAT_U / 16u) / BEAT_U) / 4u;
        if (bar != fill_bar) {
            fill_bar = bar;
            fill_bar_on = fill_arm;                   /* key 10: this whole bar */
            fill_arm = 0;
        }
    }
    fill_now = (uint8_t)(fill_held || fill_bar_on);
    for (i = 0; i < NTRK; i++)
        seq_tick(&trk[i], n, adv);
    for (i = 0; i < NTRK; i++)
        if (trk_synth(i))
            arp_tick(&trk[i], n, adv);
    if (song.playing) {
        song.tick++;
        clk_pos += adv;                               /* fx.c: the beat clock (the MIDI clock's pulses) */
        while (clk_pos >= BEAT_U) {
            clk_pos -= BEAT_U;
            clk_beat++;
        }
        midi_clock_out();
    }
}
