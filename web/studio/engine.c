/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada Studio: the firmware's DSP (engines, voices, matrix, kits, sequencer, arp, master FX) for
 * WebAssembly, without the FM-1: no USB, flash, LCD or panel. The sources are the firmware's own,
 * #included as the host tests do (tests/hostsim.c); only this glue is the browser's. After the
 * browser engine of Felucca [Salt] (Chance Roth, web/audio/engine.c, GPL-3.0).
 *
 * The keys are the FM-1's: st_keys() sets the 27 keys (bit k = key k, F3 + k on a synth track) and
 * seq.c keyboard_block plays them as on the instrument (octave, scale, CHORD, the drum map, the FX
 * layer's punch-in effects). MIDI goes in as USB-MIDI packets (midi_in_q), as from a computer.
 *
 * Built by tools/build_studio.py (zig cc, wasm32) into web/studio/engine.wasm, and natively for
 * web/test_studio.mjs (the same calls must give the same samples). */
#include <stdint.h>
#ifndef STUDIO_NATIVE
#define API __attribute__((visibility("default")))
#else                                      /* the native build (test): the firmware's memset & co. */
#define API                                /* would collide with the C library's */
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#endif
/* the firmware's big buffers (delay, punch-in, slicer, grains) live in its .pool section; here they are
 * plain zeroed memory (a named section would be written out as data: 270 KB of zeros in the .wasm) */
#define section(name) aligned(4)
#include "felucca_tables.h"
#include "libc.c"
#ifdef STUDIO_NATIVE
#undef memset
#undef memcpy
#undef memcmp
#endif

static struct { volatile uint32_t notes, buttons; } fm1_in;
static const uint8_t studio_no_user_sample[512];
#define SMP_USER_XIP(k) ((void)(k), studio_no_user_sample)   /* no user sample slots: silence */
#include "core.h"
#include "engines.c"
#include "mod.c"
#include "drums.c"
#include "params.c"
#include "voice.c"
#include "slicer.c"
#include "fx.c"
#define MQ 64u                             /* MIDI in, as usb.c's ring (no USB here) */
static uint32_t midi_in_q[MQ], mi_r, mi_w;
static void midi_out_event(uint32_t p) { (void)p; }
#include "seq.c"

/* what ui.c needs and the studio has not: no IRQs, no flash, no user presets, no projects */
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
#define NE 7                               /* panel.c: the encoders (ui.c keeps a time per one) */
static void settings_save(void) {}         /* (MIDI LEARN's map: no flash in the browser) */
#include "ui.c"
static void project_save(uint32_t slot) { (void)slot; }
static void panel_setup(void) {}
static void project_load(uint32_t slot) { (void)slot; }
static int project_used(uint32_t slot) { (void)slot; return 0; }
static int up_used(uint32_t k) { (void)k; return 0; }
static int up_load(uint32_t k) { (void)k; return 0; }
static uint32_t up_count(void) { return 0; }
static uint32_t up_nth(uint32_t n) { return n; }
static uint32_t up_rank(uint32_t slot) { return slot; }
static void up_name(uint32_t k, char *b) { (void)k; b[0] = 0; }
static void up_slot_label(char *b, uint32_t k) { (void)k; b[0] = 0; }
static void up_ui(uint32_t op, uint32_t k) { (void)op; (void)k; }

/* the drum track's pattern for PLAY (the firmware has none: its drum track starts empty) */
static const uint8_t STUDIO_BEAT[16][3] = {
    {36, 42}, {0}, {42}, {0}, {38, 42}, {0}, {42}, {36},
    {36, 42}, {0}, {42}, {46}, {38, 42}, {0}, {42, 39}, {0},
};

static int32_t out_buf[CTL * 2];
static uint64_t frames;

static track_t *trk_at(uint32_t i) { return &trk[i % NTRK]; }

/* power-on as main.c felucca_init, but the parts keep their presets' patterns and the drum track
 * gets a beat: PLAY plays something. Full master level (the page has its own volume). */
API void st_init(void)
{
    uint32_t i, k;
    fm6_init();
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
            t->engine = t->eng_req;
        }
    }
    for (i = 0; i < 16u; i++) {
        step_t *s = &TDRUM->step[i];
        s->n = 0;
        for (k = 0; k < 3u && STUDIO_BEAT[i][k]; k++)
            s->note[s->n++] = STUDIO_BEAT[i][k];
        s->time = s->n ? ST_NOTE : ST_REST;
        s->flags = (i % 4u == 0u) ? SF_ACCENT : 0;
        s->vel = s->n ? 100 : 0;
    }
    TDRUM->p[P_SLEN] = 16;
    song.sel = 0;
    song.master_q12 = 4096;
    frames = 0;
}

/* ---- sound */
API void st_select(uint32_t i) { if (i < NTRK) song.sel = (uint8_t)i; }
API uint32_t st_selected(void) { return song.sel; }
API void st_engine(uint32_t i, uint32_t e)
{
    if (e < NENGINES)
        set_engine_of(trk_at(i), e);           /* its defaults and first preset */
}
API void st_preset(uint32_t i, uint32_t p) { apply_preset_to(trk_at(i), p); }
API uint32_t st_track_engine(uint32_t i) { return trk_at(i)->eng_req; }
API uint32_t st_track_preset(uint32_t i) { return trk_at(i)->preset; }
API int st_is_drum(uint32_t i) { return is_drum(trk_at(i)); }

API void st_param(uint32_t i, uint32_t id, int32_t v)
{
    track_t *t = trk_at(i);
    const param_desc_t *d;
    if (id >= P_COUNT)
        return;
    d = track_desc(t, id);
    t->p[id] = (int16_t)clamp(v, d->min, d->max);
    if (id == P_CHORD)
        chord_poly(t);                         /* as the UI: CHORD needs POLY */
}
API int32_t st_param_get(uint32_t i, uint32_t id) { return id < P_COUNT ? trk_at(i)->p[id] : 0; }
API const char *st_param_label(uint32_t i, uint32_t id) { return id < P_COUNT ? track_desc(trk_at(i), id)->label : 0; }
API int32_t st_param_min(uint32_t i, uint32_t id) { return id < P_COUNT ? track_desc(trk_at(i), id)->min : 0; }
API int32_t st_param_max(uint32_t i, uint32_t id) { return id < P_COUNT ? track_desc(trk_at(i), id)->max : 0; }
API uint32_t st_param_fmt(uint32_t i, uint32_t id) { return id < P_COUNT ? track_desc(trk_at(i), id)->fmt : 0; }
/* the value as the FM-1 shows it (params.c fmt_value) */
API const char *st_param_text(uint32_t i, uint32_t id)
{
    static char b[24];
    const char *unit = "";
    track_t *t = trk_at(i);
    b[0] = 0;
    if (id < P_COUNT) {
        param_format(track_desc(t, id), t->p[id], b, &unit);
        str_cpy(b + str_len(b), unit, sizeof b - str_len(b));
    }
    return b;
}
API const char *st_param_name(uint32_t i, uint32_t id, int32_t v)   /* an F_ENUM value's name */
{
    const param_desc_t *d = id < P_COUNT ? track_desc(trk_at(i), id) : 0;
    return d && d->fmt == F_ENUM && d->names && v >= d->min && v <= d->max ? d->names[v] : 0;
}
API uint32_t st_macro(uint32_t i, uint32_t k) { return ENGINES[trk_at(i)->eng_req % NENGINES]->macro[k & 3u]; }

API void st_global(uint32_t id, int32_t v)
{
    if (id >= G_COUNT || id == G_CLOCK || id == G_SYNC)
        return;                                /* (no MIDI clock here) */
    song.g[id] = (int16_t)clamp(v, GP[id].min, GP[id].max);
    if (id == G_T4)
        t4_follow();
}
API int32_t st_global_get(uint32_t id) { return id < G_COUNT ? song.g[id] : 0; }
API int32_t st_global_max(uint32_t id) { return id < G_COUNT ? GP[id].max : 0; }
API const char *st_global_name(uint32_t id, uint32_t v)
{
    return id < G_COUNT && GP[id].fmt == F_ENUM && GP[id].names && (int32_t)v <= GP[id].max ? GP[id].names[v] : 0;
}
API const char *st_global_text(uint32_t id)
{
    static char b[24];
    const char *unit = "";
    b[0] = 0;
    if (id < G_COUNT) {
        param_format(&GP[id], song.g[id], b, &unit);
        str_cpy(b + str_len(b), unit, sizeof b - str_len(b));
    }
    return b;
}
API int32_t st_global_min(uint32_t id) { return id < G_COUNT ? GP[id].min : 0; }
API void st_master(uint32_t q12) { song.master_q12 = q12 > 4096u ? 4096u : q12; }

/* ---- playing */
API void st_keys(uint32_t bits) { fm1_in.notes = bits & 0x7FFFFFFu; }   /* the 27 keys */
API void st_octave(int32_t o) { song.octave = (int8_t)clamp(o, -3, 3); }
API int32_t st_octave_get(void) { return song.octave; }
API void st_fx_layer(uint32_t on)               /* FX held: the white keys are punch-in effects */
{
    kb_layer = on ? LY_FX : LY_NONE;
    punch.hold = (uint8_t)(on != 0);
}
API const char *st_punch_name(uint32_t k) { return k < PUNCH_NFX ? PUNCH_NAME[k] : 0; }
API int32_t st_punch_now(void) { return punch.req; }

API void st_midi(uint32_t status, uint32_t d1, uint32_t d2)   /* a channel message, as USB-MIDI */
{
    uint32_t cin = (status >> 4) & 0x0Fu;
    if (cin < 8u || cin == 0x0Fu || mi_w - mi_r >= MQ)
        return;
    midi_in_q[mi_w % MQ] = cin | (status & 0xFFu) << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24;
    mi_w++;
}

API void st_play(uint32_t on) { transport_req = on ? 1u : 2u; }
API uint32_t st_playing(void) { return song.playing; }
API uint32_t st_step(uint32_t i) { return trk_at(i)->seq_idx; }
API uint32_t st_step_on(uint32_t i, uint32_t s)  /* step s plays notes (the page draws the pattern) */
{
    return s < NSTEP && trk_at(i)->step[s].time == ST_NOTE && trk_at(i)->step[s].n;
}
API void st_panic(void) { panic_req = 0x0Fu; }

/* the drone: HOLD keeps the chord the keys played (ARP RPT: the whole chord, again and again) */
API void st_drone_off(void)                      /* as holding ARP on the FM-1 (ui_input.c) */
{
    uint32_t i, k, any = 0;
    for (i = 0; i < NTRK; i++)
        if (trk_synth(i) && trk[i].p[P_AHOLD] && trk[i].nheld && !trk[i].arp_phys)
            any |= 1u << i;
    if (any) {
        latch_off_req |= (uint8_t)any;
        return;
    }
    for (i = 0; i < NTRK; i++)
        if (trk_synth(i))
            for (k = 0; k < NVOICE; k++)
                if (trk[i].v[k].active && !trk[i].v[k].gate)
                    any |= 1u << i;
    hush_req |= (uint8_t)any;
}
API uint32_t st_droning(void)                    /* bit per track: a latched chord sounds */
{
    uint32_t i, m = 0;
    for (i = 0; i < NTRK; i++)
        if (trk_synth(i) && trk[i].p[P_AHOLD] && trk[i].nheld)
            m |= 1u << i;
    return m;
}
API uint32_t st_voices(void)
{
    uint32_t i, k, n = 0;
    for (i = 0; i < NTRK; i++)
        for (k = 0; k < NVOICE; k++)
            n += trk[i].v[k].active;
    return n;
}

/* CTL stereo frames (Q15, interleaved) at 44.1 kHz */
API int32_t *st_render(void)
{
    fm1_ms = (uint32_t)(frames * 1000u / FS);
    fm6_poll();                                /* the main loop's part: FM6 PTCH -> its patch */
    mix_block(out_buf, CTL);
    frames += CTL;
    return out_buf;
}
API uint32_t st_block(void) { return CTL; }
API uint32_t st_rate(void) { return FS; }

/* ---- names */
API uint32_t st_engines(void) { return NENGINES; }
API const char *st_engine_name(uint32_t e) { return e < NENGINES ? ENGINES[e]->name : 0; }
API uint32_t st_presets(uint32_t e) { return e < NENGINES ? ENGINES[e]->npresets : 0; }
API const char *st_preset_name(uint32_t e, uint32_t p)
{
    return e < NENGINES && p < ENGINES[e]->npresets ? ENGINES[e]->presets[p].name : 0;
}
API uint32_t st_engine_color(uint32_t e) { return e < NENGINES ? ENGINES[e]->color : 0; }
API uint32_t st_kits(void) { return (uint32_t)GP[G_KIT].max + 1u; }
/* the parameter ids the page uses, by name (the enums of core.h may grow: the page asks) */
#define ID(x) {#x, x}
static const struct { const char *name; uint16_t id; } IDS[] = {
    ID(P_LEVEL), ID(P_AMODE), ID(P_ARATE), ID(P_AOCT), ID(P_AHOLD), ID(P_CHORD), ID(P_SCALE), ID(P_ROOT),
    ID(P_QUANT), ID(P_TRANS), ID(P_DIST), ID(P_CHOR), ID(P_DLY), ID(P_REV), ID(P_VOICE), ID(P_SLEN), ID(P_PAN),
    ID(P_MUTE), ID(P_E0), ID(P_COUNT),
    ID(G_BPM), ID(G_SWING), ID(G_DUST), ID(G_DUCK), ID(G_FILT), ID(G_KIT), ID(G_RTYPE), ID(G_RSIZE),
    ID(G_RDAMP), ID(G_DMIX), ID(G_DTIME), ID(G_DFDBK), ID(G_DRLVL), ID(G_DRREV), ID(G_T4), ID(G_TUNE),
    ID(G_COUNT),
};
API const char *st_id_name(uint32_t k) { return k < sizeof IDS / sizeof IDS[0] ? IDS[k].name : 0; }
API uint32_t st_id(uint32_t k) { return k < sizeof IDS / sizeof IDS[0] ? IDS[k].id : 0; }
