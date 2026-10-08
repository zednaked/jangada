/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the modulation matrix (firmware/src/mod.c) on the host.
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok)
{
    printf("%-58s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

static track_t *setup(void)
{
    track_t *t = &trk[0];
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(t, 0, 0);                           /* ANALOG: E5 is CUT (0..127), E6 RES */
    return t;
}

static void slot(track_t *t, uint32_t k, uint32_t src, uint32_t dst, int32_t amt)
{
    t->p[P_M1SRC + 3u * k] = (int16_t)src;
    t->p[P_M1DST + 3u * k] = (int16_t)dst;
    t->p[P_M1AMT + 3u * k] = (int16_t)amt;
}

int main(void)
{
    const engine_t *e = ENGINES[0];
    track_t *t = setup();
    voice_t v;
    int32_t d[3];
    int16_t keep[NEDIT], base_cut, base_res;
    uint32_t moved;

    check("matrix off by default (every SRC OFF)", !mod_active(t));
    slot(t, 0, MS_VEL, MD_E0 + 4, 0);
    check("a slot with AMT 0 stays off", !mod_active(t));
    check("DST names follow the engine (E5 of ANALOG = CUT)",
          !strcmp(mod_dst_desc(e)->names[MD_E0 + 4], "CUT") && !strcmp(mod_dst_desc(e)->names[MD_FLT], "FLT"));

    memset(&v, 0, sizeof v);
    v.vel = 127;
    v.pitch16 = 60 * 16;
    base_cut = t->p[P_E0 + 4];
    base_res = t->p[P_E0 + 5];
    slot(t, 0, MS_VEL, MD_E0 + 4, -63);             /* velocity closes the cutoff */
    moved = mod_voice(t, e, &v, 0, 0, d, keep);
    check("VEL -> CUT: moves the engine parameter for the render",
          moved == 1u << 4 && t->p[P_E0 + 4] < base_cut && t->p[P_E0 + 4] >= e->edit[4].min);
    mod_restore(t, moved, keep);
    check("... and puts it back after", t->p[P_E0 + 4] == base_cut);

    slot(t, 1, MS_VEL, MD_E0 + 4, 63);              /* a second slot on the same target: they add up */
    moved = mod_voice(t, e, &v, 0, 0, d, keep);
    check("two slots on one target add up (-63 + 63 = 0)", t->p[P_E0 + 4] == base_cut);
    mod_restore(t, moved, keep);
    slot(t, 1, MS_OFF, 0, 0);

    v.vel = 0;
    moved = mod_voice(t, e, &v, 0, 0, d, keep);
    check("VEL 0: no change", t->p[P_E0 + 4] == base_cut);
    mod_restore(t, moved, keep);

    v.vel = 127;
    slot(t, 0, MS_LFO, MD_E0 + 5, 63);
    moved = mod_voice(t, e, &v, 32767, 0, d, keep);
    check("LFO +1 x 63 -> RES: clamped to its max", t->p[P_E0 + 5] == e->edit[5].max || t->p[P_E0 + 5] > base_res);
    mod_restore(t, moved, keep);
    check("... restored", t->p[P_E0 + 5] == base_res);

    slot(t, 0, MS_KEY, MD_PIT, 63);                 /* KEY: 0 at C4, + above */
    mod_voice(t, e, &v, 0, 0, d, keep);
    check("KEY at C4: no pitch change", d[1] == 0);
    v.pitch16 = 72 * 16;
    mod_voice(t, e, &v, 0, 0, d, keep);
    check("KEY an octave up -> PIT: up", d[1] > 0 && d[0] == 0 && d[2] == 0);

    slot(t, 0, MS_ENV, MD_FLT, 63);                 /* ENV -> FLT, the scale of ENV DEST */
    mod_voice(t, e, &v, 0, 32767, d, keep);
    check("ENV -> FLT: as ENV DEST FLT 63 would", d[0] == (32767 * 63) >> 7);

    slot(t, 0, MS_RND, MD_SHP, 63);
    v.rnd = 12345;
    mod_voice(t, e, &v, 0, 0, d, keep);
    {
        int32_t a = d[2];
        mod_voice(t, e, &v, 0, 0, d, keep);
        check("RND: fixed for the note (same value twice)", a == d[2] && a != 0);
    }

    {   /* a whole render: the matrix changes the sound, and leaves the parameters as they were */
        static int32_t a[2][CTL], b[2][CTL];
        int16_t before[P_COUNT];
        uint32_t i, k, diff = 0;
        for (k = 0; k < 2u; k++) {
            t = setup();
            if (k)
                slot(t, 0, MS_VEL, MD_E0 + 4, -63);
            trk_note_on(t, 60, 127);
            memcpy(before, t->p, sizeof before);
            for (i = 0; i < 40u; i++)                   /* past the attack */
                track_render(t, k ? b[0] : a[0], CTL);
            for (i = 0; i < P_COUNT; i++)
                diff += k && t->p[i] != before[i];
        }
        check("render: the parameters are as before after the blocks", diff == 0);
        diff = 0;
        for (i = 0; i < CTL; i++)
            diff += a[0][i] != b[0][i];
        check("render: VEL -> CUT -63 changes the sound", diff > 0);
    }

    {   /* Jangada 0.8: the macro knobs (song.g[G_MAC1..]) as sources: at 0 nothing, up it moves every slot on it */
        int16_t b4, b5;
        t = setup();
        b4 = t->p[P_E0 + 4], b5 = t->p[P_E0 + 5];
        slot(t, 0, MS_MAC1, MD_E0 + 4, 40);            /* MAC1 opens CUT and RES at once */
        slot(t, 1, MS_MAC1, MD_E0 + 5, 40);
        song.g[G_MAC1] = 0;
        moved = mod_voice(t, e, &v, 0, 0, d, keep);
        check("MAC1 at 0: no change", t->p[P_E0 + 4] == b4 && t->p[P_E0 + 5] == b5);
        mod_restore(t, moved, keep);
        song.g[G_MAC1] = 127;
        moved = mod_voice(t, e, &v, 0, 0, d, keep);
        check("MAC1 up: one knob moves both slots (CUT and RES)", t->p[P_E0 + 4] > b4 && t->p[P_E0 + 5] > b5);
        mod_restore(t, moved, keep);
        song.g[G_MAC2] = 127;
        slot(t, 0, MS_MAC2, MD_E0 + 4, 40);
        slot(t, 1, MS_OFF, 0, 0);
        song.g[G_MAC1] = 0;
        moved = mod_voice(t, e, &v, 0, 0, d, keep);
        check("MAC2 is its own knob", t->p[P_E0 + 4] > b4 && t->p[P_E0 + 5] == b5);
        mod_restore(t, moved, keep);
        song.g[G_MAC2] = 0;
        check("the macros are not stored in a project (G_STORED)", G_STORED == G_MAC1 && G_MAC4 + 1 == G_COUNT);
    }
    puts(fails ? "MOD TEST FAILED" : "mod: all ok");
    return fails != 0;
}
