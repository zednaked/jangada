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
    {   /* Jangada 0.7 (after SLOOP 2.4): the track's FILT (djf_block / djf_run, P_TFLT): LP closed keeps a low
         * sine and takes a high one down, HP the other way, 0 glides open and then bypasses */
        static int32_t b[CTL];
        djf_t f;
        tsvf_t c;
        double lo, hi;
        uint32_t k, i, ph;
        for (k = 0; k < 4u; k++) {
            int32_t v = k < 2u ? -60 : 60;
            uint32_t hz = k & 1u ? 8000u : 100u;
            double e = 0, e0 = 0;
            memset(&f, 0, sizeof f);
            for (ph = 0, i = 0; i < 400u; i++) {        /* 400 blocks: the glide done, then measured */
                uint32_t j;
                int bypass = !djf_block(&f, v, &c);
                for (j = 0; j < CTL; j++, ph++)
                    b[j] = (int32_t)(20000.0 * sin(2.0 * M_PI * hz * ph / FS)) << 2;
                if (i >= 300u)
                    for (j = 0; j < CTL; j++)
                        e0 += (double)(b[j] >> 2) * (b[j] >> 2);
                if (!bypass)
                    djf_run(&f, &c, b, CTL, 0, 2);
                if (i >= 300u)
                    for (j = 0; j < CTL; j++)
                        e += (double)(b[j] >> 2) * (b[j] >> 2);
            }
            if (k == 0) lo = e / e0;
            if (k == 1) hi = e / e0;
            if (k == 1u)
                ok(lo > 0.7 && hi < 0.01, "FILT LP: 100 Hz through, 8 kHz down");
            if (k == 2u) lo = e / e0;
            if (k == 3u)
                ok(lo < 0.01 && e / e0 > 0.7, "FILT HP: 100 Hz down, 8 kHz through");
        }
        for (i = 0; i < 200u && djf_block(&f, 0, &c); i++)
            ;
        ok(i < 200u && !f.mode, "FILT back to 0: glides open, then bypassed");
        ok(p_lockable(&trk[0], P_TFLT), "FILT: lockable");
    }
    {   /* the delay's dotted TIMEs (Jangada 0.7): 1/8D = 3/4 beat, 1/16D = 3/8 beat */
        int16_t keep = song.g[G_DTIME];
        uint32_t q, e8, e16;
        song.g[G_BPM] = 120;
        song.g[G_DTIME] = 0; q = delay_samples();
        song.g[G_DTIME] = 6; e8 = delay_samples();
        song.g[G_DTIME] = 7; e16 = delay_samples();
        ok(e8 == q * 3u / 4u && e16 == q * 3u / 8u && GP[G_DTIME].max == 7, "delay TIME 1/8D, 1/16D");
        song.g[G_DTIME] = keep;
    }
    if (fails)
        printf("REVERB: %u FAILED\n", fails);
    return fails != 0;
}
