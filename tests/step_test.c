/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: sequencer steps with RTCH (ratchet) and CHNC (chance) on the host.
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

#define BLK 32u

static track_t *setup(uint32_t flags)
{
    track_t *t = &trk[0];
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    t->p[P_SLEN] = 1;                        /* one step that loops */
    t->p[P_SGATE] = 64;
    t->step[0] = (step_t){{60, 0, 0, 0}, 1, ST_NOTE, (uint8_t)flags, 100};
    t->seq_idx = 0;
    t->seq_pos = 0x7FFFFFFFu;                /* fire on the first block */
    song.playing = 1;
    return t;
}

/* note-ons in `steps` passes of the step */
static uint32_t run(track_t *t, uint32_t steps)
{
    uint32_t period = div_samples((uint32_t)t->p[P_SDIV]), s0 = vage, done = 0;
    while (done < steps * period - BLK) {    /* stop just short of the next pass */
        seq_tick(t, BLK, BLK * (uint32_t)song.g[G_BPM]);   /* (seq_pos counts units) */
        done += BLK;
    }
    return vage - s0;
}

int main(void)
{
    track_t *t;
    uint32_t r, n;

    for (r = 0; r < 4u; r++) {
        t = setup(r << SF_RATCH_SH);
        n = run(t, 1);
        if (n != r + 1u) {
            printf("RTCH x%u: %u hits in one step\n", r + 1u, n);
            return 1;
        }
    }
    printf("%-46s ok\n", "RTCH x1..x4: that many hits in the step");

    t = setup(3u << SF_RATCH_SH);
    n = run(t, 8);
    assert(n == 32u);
    printf("%-46s ok\n", "RTCH x4 over 8 passes: 32 hits");

    t = setup((3u << SF_RATCH_SH) | SF_SLIDE);
    run(t, 1);
    assert(!t->seq_hold);
    printf("%-46s ok\n", "RTCH: no slide out of a ratcheted step");

    {   /* CHNC: about 100 / 75 / 50 / 25 % of 800 passes */
        static const uint32_t lo[4] = {800, 520, 330, 130}, hi[4] = {800, 680, 470, 270};
        uint32_t c;
        for (c = 0; c < 4u; c++) {
            t = setup(c << SF_CHANCE_SH);
            n = run(t, 800);
            if (n < lo[c] || n > hi[c]) {
                printf("CHNC level %u: %u of 800 passes\n", c, n);
                return 1;
            }
            printf("CHNC %3u %%: %3u of 800 passes                    ok\n", 100u - 25u * c, n);
        }
    }

    t = setup(0);
    t->seq_pos = 0x7FFFFFFFu;
    seq_tick(t, BLK, BLK * (uint32_t)song.g[G_BPM]);   /* (seq_pos counts units) */
    seq_stop();
    assert(t->rat_left == 0 && t->seq_n == 0);
    t = setup(3u << SF_RATCH_SH);
    seq_tick(t, BLK, BLK * (uint32_t)song.g[G_BPM]);   /* (seq_pos counts units) */
    assert(t->rat_left == 3u);
    seq_stop();
    assert(t->rat_left == 0);
    printf("%-46s ok\n", "stop: no ratchet left");
    puts("steps: all ok");
    return 0;
}
