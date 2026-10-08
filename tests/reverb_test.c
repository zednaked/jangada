/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the three reverb models (fx.c: ROOM, SPRING, PLATE) on the host: each answers a burst, stays
 * bounded, dies away to silence (no offset left in a loop), PLATE is stereo, the levels are near ROOM's,
 * a model change does not click, rev_clear leaves nothing. ROOM itself is covered bit for bit by the golden renders.
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t sc[CTL], sd[CTL], sr[CTL], wl[CTL], wr[CTL];
static uint32_t fails;

static void ok(int c, const char *what)
{
    printf("%-50s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

/* blocks of the reverb bus alone: a burst of noise into the send for `burst` blocks, then silence */
static void run(uint32_t blocks, uint32_t burst, double *e_l, double *e_r, double *diff, int32_t *peak, int32_t *step)
{
    static int32_t rnd = 12345, last;
    uint32_t b, i;
    *e_l = *e_r = *diff = 0;
    *peak = *step = 0;
    for (b = 0; b < blocks; b++) {
        for (i = 0; i < CTL; i++) {
            sc[i] = sd[i] = 0;
            sr[i] = b < burst ? ((int32_t)noise32(&rnd) >> 17) * 2 : 0;
        }
        fx_buses(sc, sd, sr, wl, wr, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t a = wl[i] < 0 ? -wl[i] : wl[i], d = wl[i] - last;
            *e_l += (double)wl[i] * wl[i];
            *e_r += (double)wr[i] * wr[i];
            *diff += (double)(wl[i] - wr[i]) * (wl[i] - wr[i]);
            if (a > *peak)
                *peak = a;
            d = d < 0 ? -d : d;
            if (d > *step)
                *step = d;
            last = wl[i];
        }
    }
}

int main(void)
{
    static const char *const NAME[3] = {"ROOM", "SPRING", "PLATE"};
    double el, er, df, room = 0;
    int32_t pk, st;
    uint32_t t, k;
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    for (t = 0; t < 3u; t++) {
        song.g[G_RTYPE] = (int16_t)t;
        song.g[G_RSIZE] = 90;
        song.g[G_RDAMP] = 60;
        run(8, 0, &el, &er, &df, &pk, &st);             /* (a model change: from silence) */
        run(FS / CTL, 40, &el, &er, &df, &pk, &st);       /* a burst, one second */
        if (!t)
            room = el;
        {
            char b[64];
            snprintf(b, sizeof b, "%s: answers, bounded (%+.1f dB from ROOM)", NAME[t], 10 * log10(el / room));
            ok(el > 0 && pk < 32767 * 4 && fabs(10 * log10(el / room)) < 9.0, b);
        }
        if (t == 2u)
            ok(df > el * 0.2, "PLATE: left and right differ (stereo)");
        else
            ok(df == 0, "ROOM / SPRING: mono, left == right");
        for (k = 0; k < 12u; k++)                       /* 12 s of silence: the tail is gone, no offset */
            run(FS / CTL, 0, &el, &er, &df, &pk, &st);
        {
            char b[64];
            snprintf(b, sizeof b, "%s: dies away to silence (peak %d)", NAME[t], pk);
            ok(t ? pk <= 2 : pk <= 64, b);              /* (ROOM: Felucca's, -58 dBFS of rounding left) */
        }
    }
    {   /* a model change with a tail ringing: no step above the music's own */
        int32_t burst_step;
        song.g[G_RTYPE] = 0;
        run(8, 0, &el, &er, &df, &pk, &st);
        run(FS / CTL / 2, 40, &el, &er, &df, &pk, &burst_step);
        song.g[G_RTYPE] = 2;
        run(4, 0, &el, &er, &df, &pk, &st);
        ok(st <= burst_step, "model change while ringing: no click");
    }
    {   /* rev_clear: every sample silent (rev_u's int16 view has an odd count, 556 + 441: clearing it through its
         * int32 view left the last one, which ROOM played back as a click after a model change; Felucca 1.0.5.2) */
        uint32_t i, left = 0;
        for (i = 0; i < sizeof rev_u.ap / 2u; i++)
            rev_u.ap[i] = 1000;
        for (i = 0; i < sizeof rev_comb / 2u; i++)
            rev_comb[i] = 1000;
        rev_clear();
        for (i = 0; i < sizeof rev_u.ap / 2u; i++)
            left += rev_u.ap[i] != 0;
        for (i = 0; i < sizeof rev_comb / 2u; i++)
            left += rev_comb[i] != 0;
        ok(!left, "rev_clear: combs and allpasses all silent");
    }
    if (fails)
        printf("REVERB: %u FAILED\n", fails);
    return fails != 0;
}
