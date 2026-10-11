/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada 0.9.7: VIVO (seq.c vv_*, issue #4), the living layer over a pattern, on the host.
 *   off      every value at its default: the pattern plays as written
 *   PULSE    PULS hits spread over LEN as Euclid's (k hits, step i plays when i * k mod LEN < k), ROT turns
 *            them; each hit plays the next note step in turn; an empty pattern plays the key last played
 *   chance   DENS: about that share plays; the same SEED plays the same, another SEED another
 *   CYCLE    CYC: the chances repeat every CYC steps; FRZ: every pass the same
 *   MEMORY   MEM < 0 alternates (more changes between hit and rest), MEM > 0 bunches them
 *   ACCENT   ACC: velocity 127 every ACC steps
 *   EVOLVE   a rule changes the hits pass by pass (FRZ holds them); R90 from one cell spreads
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

#define BLK 8u

static uint32_t fails;
static void ok(int c, const char *what)
{
    printf("%-66s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

#define NS 512u                                   /* steps recorded */
static int16_t played[NS];                        /* per step: the note that started (0 none) */
static uint8_t vel_of[NS];

static track_t *setup(uint32_t len, int notes)
{
    track_t *t = &trk[0];
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    t->p[P_SLEN] = (int16_t)len;
    t->p[P_SDIV] = 2;
    t->p[P_SGATE] = 32;
    t->p[P_LEVEL] = 100;
    t->p[P_VDENS] = 127;
    for (i = 0; i < NSTEP; i++)
        t->step[i] = notes && i < len ? (step_t){{(uint8_t)(40 + i), 0, 0, 0}, 1, ST_NOTE, 0, 100}
                                      : (step_t){{0, 0, 0, 0}, 0, ST_REST, 0, 0};
    seq_start();
    for (i = 1; i < NTRK; i++)
        trk[i].p[P_SLEN] = 0;
    return t;
}

/* n steps of track 1: what each one started */
static void play(track_t *t, uint32_t n)
{
    uint32_t s = 0, k, last = 0xFFFFu;
    memset(played, 0, sizeof played);
    memset(vel_of, 0, sizeof vel_of);
    while (s < n) {
        uint32_t a = vage;
        seq_tick(t, BLK, BLK * (uint32_t)song.g[G_BPM]);
        if (t->seq_idx != last) {
            if (last != 0xFFFFu)
                s++;
            last = t->seq_idx;
        }
        if (vage != a && s < NS)
            for (k = 0; k < NVOICE; k++)
                if (t->v[k].age > a) {
                    played[s] = t->v[k].note;
                    vel_of[s] = t->v[k].vel;
                }
    }
}
static uint32_t count(uint32_t from, uint32_t n)
{
    uint32_t i, c = 0;
    for (i = from; i < from + n; i++)
        c += played[i] != 0;
    return c;
}

int main(void)
{
    track_t *t;
    uint32_t i, c;
    int good;
    static int16_t a[NS];

    t = setup(16, 1);
    play(t, 32);
    good = 1;
    for (i = 0; i < 32; i++)
        good &= played[i] == 40 + (int)(i % 16u);
    ok(good, "off: every step plays its own note, as written");

    t = setup(16, 1);
    t->p[P_VPULS] = 5;
    play(t, 32);
    good = count(0, 16) == 5 && count(16, 16) == 5;
    for (i = 0; i < 16; i++)
        good &= (played[i] != 0) == ((i * 5u) % 16u < 5u);
    ok(good, "PULS 5 of 16: five hits a pass, Euclid's (0 4 7 10 13)");
    ok(played[0] == 40 && played[4] == 41 && played[7] == 42 && played[10] == 43 && played[13] == 44 && played[16] == 45,
       "PULS: each hit plays the next note step in turn");
    t = setup(16, 1);
    t->p[P_VPULS] = 5;
    t->p[P_VROT] = 2;
    play(t, 16);
    ok(played[2] && played[6] && played[9] && played[12] && played[15] && count(0, 16) == 5, "ROT 2: the hits turned two steps");
    t = setup(16, 0);
    last_note = 57;
    t->p[P_VPULS] = 3;
    play(t, 16);
    ok(count(0, 16) == 3 && played[0] == 57, "an empty pattern: PULS plays the key played last");

    t = setup(16, 1);
    t->p[P_VDENS] = 64;
    t->p[P_VSEED] = 9;
    play(t, 256);
    c = count(0, 256);
    memcpy(a, played, sizeof a);
    ok(c > 90 && c < 166, "DENS 50 %: about half the steps play");
    t = setup(16, 1);
    t->p[P_VDENS] = 64;
    t->p[P_VSEED] = 9;
    play(t, 256);
    ok(!memcmp(a, played, sizeof a), "the same SEED: the same steps, from PLAY");
    t = setup(16, 1);
    t->p[P_VDENS] = 64;
    t->p[P_VSEED] = 10;
    play(t, 256);
    ok(memcmp(a, played, sizeof a) != 0, "another SEED: other steps");

    t = setup(16, 1);
    t->p[P_VDENS] = 64;
    t->p[P_VCYC] = 5;
    play(t, 200);
    good = 1;
    for (i = 5; i < 200; i++)
        good &= (played[i] != 0) == (played[i - 5u] != 0);
    ok(good, "CYC 5: the chances repeat every 5 steps, against LEN 16");
    t = setup(16, 1);
    t->p[P_VDENS] = 64;
    t->p[P_VFRZ] = 1;
    play(t, 160);
    good = 1;
    for (i = 16; i < 160; i++)
        good &= (played[i] != 0) == (played[i - 16u] != 0);
    ok(good, "FRZ: every pass the same");

    {
        uint32_t ch[2];
        int m;
        for (m = 0; m < 2; m++) {
            t = setup(16, 1);
            t->p[P_VDENS] = 64;
            t->p[P_VMEM] = m ? 60 : -60;
            play(t, 400);
            ch[m] = 0;
            for (i = 1; i < 400; i++)
                ch[m] += (played[i] != 0) != (played[i - 1u] != 0);
        }
        ok(ch[0] > 250 && ch[1] < 150, "MEM -: alternates (many changes); MEM +: bunches (few)");
    }

    t = setup(16, 1);
    t->p[P_VACC] = 3;
    play(t, 24);
    good = 1;
    for (i = 0; i < 24; i++)
        good &= (vel_of[i] == 127) == (i % 3u == 0u);
    ok(good, "ACC 3: an accent every 3 steps, on its own cycle");

    t = setup(16, 1);
    t->p[P_VPULS] = 4;
    t->p[P_VEVOL] = 2;                            /* R90 */
    play(t, 64);
    good = 0;
    for (i = 0; i < 16; i++)
        good |= (played[i] != 0) != (played[16 + i] != 0);
    ok(good, "EVOL R90: the hits change from pass to pass");
    t = setup(16, 1);
    t->p[P_VPULS] = 4;
    t->p[P_VEVOL] = 2;
    t->p[P_VFRZ] = 1;
    play(t, 64);
    good = 1;
    for (i = 16; i < 64; i++)
        good &= (played[i] != 0) == (played[i - 16u] != 0);
    ok(good, "EVOL with FRZ: held");
    t = setup(15, 0);
    t->p[P_VEVOL] = 2;
    last_note = 50;
    play(t, 45);
    ok(count(0, 15) == 1 && played[7] && count(15, 15) == 2 && played[21] && played[23], "R90 from one cell: it spreads (1, then 2 either side)");

    printf(fails ? "VIVO TEST FAILED\n" : "VIVO: all ok\n");
    return fails ? 1 : 0;
}
