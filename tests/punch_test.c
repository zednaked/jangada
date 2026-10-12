/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the master bus after SLOOP (fx.c, punch.c) on the host: the 16 punch-in effects, DUST,
 * DUCK and the DJ filter. Each one changes the mix, stays bounded, lets go cleanly, and costs what
 * it should; FX held turns the white keys into the effects and plays no note.
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "instr.h"

#define BLK CTL
#define SEC (FS / BLK)                       /* blocks in a second */
static int32_t out[2 * BLK];

static void setup(void)
{
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.master_q12 = 4096;
    host_preset(&trk[0], 0, 0);              /* ANALOG SAW LEAD on track 1 */
    trk[0].p[P_REL] = 60;
    punch.req = -1;
    kb_layer = 0;
    punch.keybit = 0;
}

/* blocks of the mix: energy and the largest sample step (a click) */
static void run(uint32_t blocks, double *energy, int32_t *step)
{
    static int32_t last;
    uint32_t b, i;
    double e = 0;
    int32_t s = 0;
    for (b = 0; b < blocks; b++) {
        mix_block(out, BLK);
        for (i = 0; i < 2u * BLK; i += 2) {
            int32_t d = out[i] - last;
            d = d < 0 ? -d : d;
            if (d > s)
                s = d;
            last = out[i];
            e += (double)out[i] * out[i];
        }
    }
    if (energy)
        *energy = e / (blocks * BLK);
    if (step)
        *step = s;
}

/* a held chord, playing transport: the dry reference, then the same with effect fx */
static void with_chord(void)
{
    input_on(&trk[0], 60, 100);
    input_on(&trk[0], 64, 100);
    input_on(&trk[0], 67, 100);
    seq_start();
    run(SEC / 2u, 0, 0);                     /* fill the ring */
}

int main(void)
{
    double dry, wet, after;
    int32_t step_dry, step_wet;
    uint32_t fx, fails = 0;

    for (fx = 0; fx < PUNCH_NFX; fx++) {
        setup();
        with_chord();
        run(SEC / 2u, &dry, &step_dry);
        punch.req = (int8_t)fx;
        run(SEC / 2u, &wet, &step_wet);
        punch.req = -1;
        run(SEC / 8u, 0, 0);                 /* the 64-sample fade out and the filters' tails */
        assert(punch.cur < 0);
        run(SEC / 4u, &after, 0);
        /* changed the mix (a loop of a steady chord can be close to it: 1 %); came back */
        if (fabs(wet - dry) < dry * 0.01 && fx != PX_LOOP4 && fx != PX_LOOP8 && fx != PX_LOOP16 &&
            fx != PX_LOOP32 && fx != PX_STUT && fx != PX_HALF && fx != PX_REV) {
            printf("%-8s did not change the mix (%.0f vs %.0f)\n", PUNCH_NAME[fx], wet, dry);
            fails++;
        }
        if (wet > dry * 4.0) {
            printf("%-8s %.1f x the energy of the dry mix\n", PUNCH_NAME[fx], wet / dry);
            fails++;
        }
        if (fabs(after - dry) > dry * 0.25) {
            printf("%-8s did not let the mix go (%.2f x)\n", PUNCH_NAME[fx], after / dry);
            fails++;
        }
        seq_stop();
    }
    printf("%-46s %s\n", "punch: 16 effects change the mix, let it go", fails ? "FAIL" : "ok");

    {   /* every effect switched on and off at random times: no click above the music's own */
        int32_t s, worst = 0;
        uint32_t k;
        setup();
        with_chord();
        run(SEC / 4u, 0, &step_dry);
        for (k = 0; k < 64u; k++) {
            punch.req = (k & 1u) ? -1 : (int8_t)(rand() % PUNCH_NFX);
            run(1u + (uint32_t)rand() % 200u, 0, &s);
            if (s > worst)
                worst = s;
        }
        seq_stop();
        printf("%-46s %s (step %d vs dry %d)\n", "punch: switching declicks", worst < 3 * step_dry ? "ok" : "FAIL",
               worst, step_dry);
        fails += worst >= 3 * step_dry;
    }

    {   /* FX held: a white key starts its effect, plays no note; its key-up ends it */
        uint32_t i, sounding = 0;
        setup();
        kb_layer = LY_FX;
        fm1_in.notes = 1u << 2;              /* G3: the 2nd white key, LOOP 8 */
        run(4, 0, 0);
        for (i = 0; i < NVOICE; i++)
            sounding += trk[0].v[i].active;
        assert(punch.req == PX_LOOP8 && !sounding);
        fm1_in.notes = 1u << 1;              /* F#3: black, nothing */
        run(4, 0, 0);
        assert(punch.req == -1);
        fm1_in.notes = 0;
        kb_layer = 0;
        run(4, 0, 0);
        fm1_in.notes = 1u << 2;              /* FX up: the key plays its note again */
        run(4, 0, 0);
        for (i = 0, sounding = 0; i < NVOICE; i++)
            sounding += trk[0].v[i].active;
        assert(punch.req == -1 && sounding);
        fm1_in.notes = 0;
        run(4, 0, 0);
        printf("%-46s ok\n", "punch: FX held = white keys pick, no notes");
    }

    {   /* 0.9.9 FX LATCH: two quick taps keep it on, out of the layer too; a tap of it again lets it go */
        setup();
        kb_layer = LY_FX;
        fm1_ms = 1000;
        fm1_in.notes = 1u << 2;              /* one tap: on while held */
        run(4, 0, 0);
        fm1_in.notes = 0;
        run(4, 0, 0);
        assert(punch.req == -1 && !px_latch);
        fm1_ms = 1200;                       /* again within PX_TAP_MS: latched */
        fm1_in.notes = 1u << 2;
        run(4, 0, 0);
        fm1_in.notes = 0;
        run(4, 0, 0);
        assert(punch.req == PX_LOOP8 && px_latch);
        kb_layer = 0;                        /* out of the FX layer: still on */
        run(4, 0, 0);
        assert(punch.req == PX_LOOP8);
        kb_layer = LY_FX;
        fm1_ms = 5000;
        fm1_in.notes = 1u << 2;              /* tapped again: off */
        run(4, 0, 0);
        fm1_in.notes = 0;
        run(4, 0, 0);
        assert(punch.req == -1 && !px_latch);
        fm1_ms = 6000;                       /* slow taps: never latched */
        fm1_in.notes = 1u << 4;
        run(4, 0, 0);
        fm1_in.notes = 0;
        run(4, 0, 0);
        fm1_ms = 7000;
        fm1_in.notes = 1u << 4;
        run(4, 0, 0);
        fm1_in.notes = 0;
        run(4, 0, 0);
        assert(punch.req == -1 && !px_latch);
        kb_layer = 0;
        printf("%-46s ok\n", "punch: FX LATCH (2 taps), out of the layer too");
    }

    {   /* DUST and the DJ filter change the mix, bounded; at 0 they are bypassed */
        static const struct { uint32_t g; int16_t v; const char *name; } M[] = {
            {G_DUST, 127, "DUST 100 %"}, {G_DUST, 40, "DUST 31 %"}, {G_FILT, -50, "FILT LP"}, {G_FILT, 50, "FILT HP"}};
        uint32_t m;
        for (m = 0; m < sizeof M / sizeof M[0]; m++) {
            setup();
            with_chord();
            run(SEC / 2u, &dry, 0);
            song.g[M[m].g] = M[m].v;
            run(SEC / 2u, &wet, &step_wet);
            song.g[M[m].g] = 0;
            run(SEC, 0, 0);                  /* the filter glides open, then bypasses */
            run(SEC / 4u, &after, 0);
            seq_stop();
            if (fabs(wet - dry) < dry * 0.02 || wet > dry * 4.0 || fabs(after - dry) > dry * 0.25) {
                printf("%-10s dry %.0f wet %.0f after %.0f\n", M[m].name, dry, wet, after);
                fails++;
            } else {
                printf("%-10s %.2f x the energy, back to %.2f x                ok\n", M[m].name, wet / dry,
                       after / dry);
            }
        }
        assert(djf.mode == 0);
    }

    {   /* DUCK: a kick dips the synth parts, which are back an eighth note later (against the same run
         * without DUCK: the chord decays on its own) */
        double dip[2], back[2];
        uint32_t d;
        for (d = 0; d < 2u; d++) {
            setup();
            with_chord();
            song.g[G_DUCK] = d ? 127 : 0;
            run(SEC / 4u, 0, 0);
            drums.kick = 1;
            run(SEC / 64u, &dip[d], 0);      /* 16 ms after the kick */
            run(SEC / 4u, 0, 0);             /* an eighth at 120 BPM is 250 ms */
            run(SEC / 8u, &back[d], 0);
            assert(duck.g1 == 32767);        /* the gain itself is back to 1 */
            seq_stop();
        }
        if (dip[1] > dip[0] * 0.25 || fabs(back[1] - back[0]) > back[0] * 0.1)   /* (tails of the last run) */ {
            printf("DUCK: dip %.2f x, back %.2f x\n", dip[1] / dip[0], back[1] / back[0]);
            fails++;
        } else {
            printf("%-46s ok (%.2f x)\n", "DUCK: the kick dips the parts, they come back", dip[1] / dip[0]);
        }
    }

    {   /* cost of the master bus on the host: instructions a sample, all on at once vs off */
        uint64_t i0, i1, i2;
        setup();
        with_chord();
        i0 = instr_now();
        run(SEC, 0, 0);
        i1 = instr_now();
        song.g[G_DUST] = 90;
        song.g[G_FILT] = -40;
        punch.req = PX_WOBBLE;
        run(SEC, 0, 0);
        i2 = instr_now();
        seq_stop();
        if (i0)
            printf("%-46s %.0f / sample (mix %.0f)\n", "cost: DUST + FILT + WOBBLE on the host", (double)(i2 - i1 - (i1 - i0)) / FS,
                   (double)(i1 - i0) / FS);
    }

    if (fails)
        printf("PUNCH / MASTER: %u FAILED\n", fails);
    return fails != 0;
}
