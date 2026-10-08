/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada (after SLOOP 2.4): micro timing, parameter locks and fills of the sequencer, on the host.
 *   nudge   a step nudged late fires that far into its step, one nudged early that far before it; the
 *           extremes crossing (a step +31, the next -32) still fire in order; no step lost or doubled
 *           over many passes, at 1/16 and with SWING
 *   locks   a lock sets p[] on its step, the base comes back at the next one; a knob turned meanwhile is
 *           the new base; STOP lets every lock go; p_unlocked (what a save keeps)
 *   fill    FILL ONLY steps play only in a fill, NO FILL steps only outside one; GLO + key 10 (fill_arm)
 *           makes the next whole bar a fill (events_block), STOP ends it
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

#define BLK 8u

static uint32_t fails;
static void ok(int c, const char *what)
{
    printf("%-62s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

typedef struct { uint32_t t; uint8_t note; } hit_t;
static hit_t hits[4096];
static uint32_t nhits, now;

static track_t *setup(uint32_t len)
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
    for (i = 0; i < len; i++)
        t->step[i] = (step_t){{(uint8_t)(60 + i), 0, 0, 0}, 1, ST_NOTE, 0, 100};
    t->seq_idx = (uint16_t)(len - 1u);
    t->seq_pos = 0x7FFFFFFFu;                /* step 0 on the first block (seq_start) */
    t->mx_due = t->mx_early = 0;
    fill_now = fill_held = fill_arm = fill_bar_on = 0;
    song.playing = 1;
    nhits = now = 0;
    return t;
}

static void tick(track_t *t)                 /* one block; the note-ons it made, at the block's time */
{
    uint32_t a = vage, k;
    seq_tick(t, BLK, BLK * (uint32_t)song.g[G_BPM]);   /* (seq_pos counts units) */
    if (vage != a)
        for (k = 0; k < NVOICE; k++)
            if (t->v[k].age > a && nhits < 4096u) {
                hits[nhits].t = now;
                hits[nhits].note = t->v[k].note;
                nhits++;
            }
    now += BLK;
}

static void run(track_t *t, uint32_t samples)
{
    uint32_t end = now + samples;
    while (now < end)
        tick(t);
}

static int near(uint32_t a, uint32_t b) { return a + BLK >= b && b + BLK >= a; }   /* within a block */

int main(void)
{
    track_t *t;
    uint32_t P, i;

    /* ---- nudge */
    t = setup(4);
    P = div_samples(2);
    step_micro_set(t, 1, 16);                /* a quarter of a step late */
    step_micro_set(t, 2, -16);               /* a quarter early */
    run(t, 4 * P);
    ok(nhits == 4 && hits[0].note == 60 && near(hits[0].t, 0) && hits[1].note == 61 && near(hits[1].t, P + P / 4u) &&
           hits[2].note == 62 && near(hits[2].t, 2 * P - P / 4u) && hits[3].note == 63 && near(hits[3].t, 3 * P),
       "nudge: +16 a quarter step late, -16 a quarter early, the others on the grid");

    t = setup(4);
    step_micro_set(t, 1, MICRO_MAX);         /* the extremes, crossing: the late one first, then the early one */
    step_micro_set(t, 2, MICRO_MIN);
    run(t, 4 * P);
    ok(nhits == 4 && hits[1].note == 61 && hits[2].note == 62 && hits[1].t <= hits[2].t &&
           near(hits[1].t, P + P / 64u * 31u) && hits[2].t + P / 2u + BLK >= 2 * P,
       "nudge: +31 then -32 (crossing): in order, each near its time");

    t = setup(16);
    t->p[P_SSWING] = 60;
    for (i = 0; i < 16u; i++)
        step_micro_set(t, i, (int32_t)((i * 37u) % 64u) - 32);
    run(t, 40 * 16 * P);
    {
        uint32_t good = nhits >= 16u * 39u;
        for (i = 1; i < nhits && good; i++)
            good = hits[i].note == 60 + (hits[i - 1].note - 60 + 1) % 16;
        ok(good, "nudge: every step nudged, SWING 60: 40 passes, none lost, none twice");
    }
    t = setup(1);                            /* one step that loops, nudged early: once a pass */
    step_micro_set(t, 0, -20);
    run(t, 10 * P - P / 2u);                 /* (0, then 20/64 of a step before each next pass) */
    ok(nhits == 10, "nudge: a single looping step nudged early: once a pass");

    {   /* no drift (Jangada 0.7: seq_pos counts units): 100 bars of 1/16 at 133 BPM, the last downbeat within a
         * block of its exact time; a 1/8 track and a 1/16 one stay together */
        uint32_t last16 = 0, last8 = 0, n16 = 0, n8 = 0, a;
        double exact;
        t = setup(16);
        song.g[G_BPM] = 133;
        trk[1].p[P_SLEN] = 8; trk[1].p[P_SDIV] = 1; trk[1].p[P_SGATE] = 32;   /* (DIV 1: an 1/8) */
        for (i = 0; i < 8u; i++)
            trk[1].step[i] = (step_t){{(uint8_t)(70 + i), 0, 0, 0}, 1, ST_NOTE, 0, 100};
        trk[1].seq_idx = 7; trk[1].seq_pos = 0x7FFFFFFFu;
        while (n16 <= 1600u) {
            a = vage;
            seq_tick(&trk[1], BLK, BLK * 133u);
            if (vage != a) { last8 = now; n8++; }
            a = vage;
            seq_tick(t, BLK, BLK * 133u);
            if (vage != a) { if (n16 % 16u == 0) last16 = now; n16++; }
            now += BLK;
        }
        exact = 100.0 * 4.0 * 60.0 * FS / 133.0;
        ok(fabs((double)last16 - exact) <= BLK && n8 == 801u && last8 == last16,
           "no drift: 100 bars at 133 BPM within a block; 1/8 and 1/16 together");
        song.g[G_BPM] = 120;
    }

    /* ---- locks */
    t = setup(4);
    assert(lock_set(t, 1, P_LEVEL, 20));
    assert(lock_set(t, 1, P_REV, 90));
    assert(!lock_set(t, 1, P_SDIV, 0) && !lock_set(t, 1, P_ROOT, 3));   /* the sequencer, the key: not lockable */
    run(t, P / 2u);
    ok(t->p[P_LEVEL] == 100 && t->p[P_REV] == 0, "locks: step 0 has none, the base");
    run(t, P);
    ok(t->p[P_LEVEL] == 20 && t->p[P_REV] == 90, "locks: step 1 sets LEVEL 20 and REV 90");
    ok(p_unlocked(t, P_LEVEL) == 100 && p_unlocked(t, P_REV) == 0, "locks: a save keeps the base (p_unlocked)");
    run(t, P);
    ok(t->p[P_LEVEL] == 100 && t->p[P_REV] == 0, "locks: step 2, without them: back to the base");
    run(t, 3 * P);                           /* step 1 again; a knob turned while it is locked */
    ok(t->p[P_LEVEL] == 20, "locks: next pass, step 1 again");
    t->p[P_LEVEL] = 70;
    run(t, P);
    ok(t->p[P_LEVEL] == 70, "locks: a knob turned on a locked step: the new base");
    run(t, 3 * P);                           /* stopped while locked */
    ok(t->p[P_REV] == 90, "locks: step 1 once more");
    seq_stop();
    ok(t->p[P_REV] == 0 && t->p[P_LEVEL] == 70 && !t->lk_n, "locks: STOP lets every lock go");
    {
        uint32_t k, n = 0;
        for (k = 0; k < NSTEP; k++)
            n += (uint32_t)lock_set(t, k, P_PAN, 10);
        ok(n == NLOCK - 2u, "locks: NLOCK a track, then no slot");
    }
    t = setup(4);
    ok(lock_find(t, 0, P_LEVEL, 0) < 0, "locks: a zeroed track has no lock (step + 1 stored)");
    run(t, P / 2u);
    ok(t->p[P_LEVEL] == 100, "locks: a zeroed track plays its base on step 0");
    lock_set(t, 3, P_LEVEL, 5);
    stepx_clear(t, 3);
    ok(lock_find(t, 3, P_LEVEL, 0) < 0, "locks: a cleared step loses its locks");

    /* ---- fill */
    t = setup(4);
    step_cond_set(t, 0, FC_NOFILL);
    step_cond_set(t, 3, FC_FILL);
    run(t, 4 * P - BLK);
    ok(nhits == 3 && hits[0].note == 60 && hits[2].note == 62, "fill: none: NO FILL plays, FILL ONLY does not");
    fill_now = 1;
    run(t, 4 * P);
    ok(nhits == 6 && hits[3].note == 61 && hits[5].note == 63, "fill: on: FILL ONLY plays, NO FILL does not");
    fill_now = 0;

    {   /* GLO + key 10: the next bar (events_block), the whole of it, then back */
        uint32_t bar = div_samples(0) * 4u, b0;   /* (DIV 0: a quarter) */
        t = setup(16);
        for (i = 0; i < 16u; i++)
            step_cond_set(t, i, FC_FILL);
        transport_req = 1;
        events_block(BLK);
        now = BLK;
        nhits = 0;
        fill_arm = 1;
        {
            uint32_t on_in_bar[3] = {0, 0, 0};
            while (now < 3u * bar) {
                uint32_t a = vage;
                events_block(BLK);
                if (vage != a)                      /* (by its step: the steps run a little ahead of the bars) */
                    on_in_bar[(now + BLK + bar / 64u) / bar % 3u]++;
                now += BLK;
            }
            b0 = on_in_bar[0];
            ok(b0 == 0 && on_in_bar[1] == 16 && on_in_bar[2] == 0 && !fill_arm,
               "fill: key 10 armed in bar 1: bar 2 is a fill (16 FILL ONLY steps), bar 3 not");
        }
        fill_arm = 1;
        transport_req = 2;
        events_block(BLK);
        ok(!fill_arm && !fill_bar_on, "fill: STOP ends an armed fill");
    }

    if (fails)
        printf("SEQX: %u FAILED\n", fails);
    return fails != 0;
}
