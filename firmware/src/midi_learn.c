/* SPDX-License-Identifier: GPL-3.0-only */
/* MIDI LEARN (Jangada 0.9, after Felucca 1.5 by Leo Kuroshita, Discussion #170; GPL-3.0): a controller's CC set
 * to a parameter of a track, on the device. GLO held + the LEARN key (ui_layers.c) turns it on and off. On: a
 * knob turned on a track's page (edit_param) picks that parameter of the selected track; the next CC that comes
 * (seq.c midi_learned, any channel) is set to it, and stays until something else is learned on it. Then turn the
 * next knob. OCT- clears the picked parameter's CC; the LEARN key again is done. The footer's first row says what
 * is picked and its CC (ml_line). One CC sets one parameter, a parameter has one CC: learning one again replaces
 * both. MENU > MIDI LEARN shows how many and clears them all. The map: seq.c ml_tab, kept with the settings. */
static struct {
    uint8_t on, pick;            /* learning; a parameter is picked */
    uint8_t trk, id;             /* the picked one */
} ml_ui;

/* the entry of parameter id on track k, ML_N none */
static uint32_t ml_find(uint32_t k, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < ML_N; i++)
        if (ml_tab[3u * i + 2u] == P_KEY[id] + 1u && ml_tab[3u * i + 1u] == k)
            return i;
    return ML_N;
}
static uint32_t ml_count(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < ML_N; i++)
        n += ml_tab[3u * i + 2u] != 0u;
    return n;
}
static void ml_put(uint32_t i, uint32_t cc, uint32_t k, uint32_t key1)   /* (the ISR reads it: all at once) */
{
    fm1_irq_off();
    ml_tab[3u * i] = (uint8_t)cc;
    ml_tab[3u * i + 1u] = (uint8_t)k;
    ml_tab[3u * i + 2u] = (uint8_t)key1;
    fm1_irq_on();
}
/* the CC's entry and parameter id of track k's go (either: none with cc 0x80 / id P_COUNT); 1 = one went */
static int ml_drop(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i, gone = 0;
    for (i = 0; i < ML_N; i++) {
        const uint8_t *e = &ml_tab[3u * i];
        if (e[2] && (e[0] == cc || (id < P_COUNT && e[2] == P_KEY[id] + 1u && e[1] == k))) {
            ml_put(i, 0, 0, 0);
            gone = 1;
        }
    }
    return gone;
}
/* cc -> parameter id of track k, in place of what either had; 1 ok, 0 no room, 2 a CC never learned */
static uint32_t ml_learn(uint32_t cc, uint32_t k, uint32_t id)
{
    uint32_t i;
    if (!ml_free_cc(cc) || k >= NTRK || id >= P_COUNT)
        return 2;
    (void)ml_drop(cc, k, id);
    for (i = 0; i < ML_N; i++)
        if (!ml_tab[3u * i + 2u]) {
            ml_put(i, cc, k, P_KEY[id] + 1u);
            return 1;
        }
    return 0;
}
static int ml_clear_all(void)                    /* MENU > MIDI LEARN: every CC; 1 = there were */
{
    uint32_t n = ml_count();
    fm1_irq_off();
    memset(ml_tab, 0, sizeof ml_tab);
    fm1_irq_on();
    if (n)
        settings_save();
    return n != 0u;
}
static const char *ml_menu_value(void)          /* MENU > MIDI LEARN: "3 CC", "NONE" */
{
    static char b[8];
    uint32_t n = ml_count();
    if (!n)
        return "NONE";
    fmt_int(b, (int32_t)n);
    str_cpy(b + str_len(b), " CC", 4);
    return b;
}
static void ml_off(void)
{
    ml_ui.on = ml_ui.pick = 0;
    ml_arm = 0;
    ml_heard = 0;
    ui.foot_sig = 0;
}
static void ml_toggle(void)
{
    if (ml_ui.on) {
        ml_off();
        ui_message("LEARN DONE");
        return;
    }
    ml_ui.on = 1;
    ml_ui.pick = 0;
    ml_heard = 0;
    ui.foot_sig = 0;
    ui_message("MIDI LEARN: TURN A KNOB");
}
/* a knob turned parameter id of the selected track (edit_param): picked while learning */
static void ml_knob(uint32_t id)
{
    if (!ml_ui.on || id >= P_COUNT)
        return;
    if (ml_ui.pick && ml_ui.trk == song.sel && ml_ui.id == id)
        return;
    ml_ui.trk = song.sel;
    ml_ui.id = (uint8_t)id;
    ml_ui.pick = 1;
    ml_heard = 0;
    ml_arm = 1;
    ui.foot_sig = 0;
}
static void ml_name(char *b)                     /* "T2 CUT" (b: 12 bytes) */
{
    b[0] = 'T';
    b[1] = (char)('1' + ml_ui.trk);
    b[2] = ' ';
    str_cpy(b + 3, track_desc(&trk[ml_ui.trk % NTRK], ml_ui.id)->label, 9);
}
/* each frame: a CC heard for the picked parameter is learned */
static void ml_poll(void)
{
    uint32_t cc = ml_heard, r;
    char a[12], b[12];
    if (!cc)
        return;
    ml_heard = 0;
    if (!ml_ui.on || !ml_ui.pick)
        return;
    cc--;
    r = ml_learn(cc, ml_ui.trk, ml_ui.id);
    if (r != 1u) {
        ui_message(r ? "CC NOT LEARNABLE" : "LEARN FULL (16)");
        return;
    }
    str_cpy(a, "CC", 4);
    fmt_int(a + 2, (int32_t)cc);
    str_cpy(a + str_len(a), " > ", 4);
    ml_name(b);
    ui_say(a, b);
    ml_ui.pick = 0;
    ml_arm = 0;
    ui.foot_sig = 0;
    settings_save();
}
/* OCT- while learning: the picked parameter's CC goes. 1 = taken (not an octave) */
static int ml_oct_down(void)
{
    char b[12];
    if (!ml_ui.on)
        return 0;
    if (!ml_ui.pick || !ml_drop(0x80u, ml_ui.trk, ml_ui.id)) {
        ui_message(ml_ui.pick ? "NO CC TO CLEAR" : "TURN A KNOB");
        return 1;
    }
    ml_name(b);
    ui_say(b, " CC CLEARED");
    ui.foot_sig = 0;
    settings_save();
    return 1;
}
/* the footer's first row while learning: "LEARN: TURN A KNOB", "T2 CUT: SEND A CC", "T2 CUT = CC74" */
static void ml_line(char *b)
{
    uint32_t i;
    if (!ml_ui.pick) {
        str_cpy(b, "LEARN: TURN A KNOB", 24);
        return;
    }
    ml_name(b);
    i = ml_find(ml_ui.trk, ml_ui.id);
    if (i == ML_N) {
        str_cpy(b + str_len(b), ": SEND A CC", 12);
        return;
    }
    str_cpy(b + str_len(b), " = CC", 6);
    fmt_int(b + str_len(b), ml_tab[3u * i]);
}
