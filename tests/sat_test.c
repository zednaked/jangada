/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada SAT on the host (eng_analog.c analog_sat, ANALOG SAT / SDRV): the shaper after the filter.
 * It stays bounded at every type x SDRV x CUT x RES x FTYP corner, adds harmonics as SDRV goes up (a sine
 * in, overtones out), keeps WARM near the dry level (made up), leaves no DC worth the name, gives a held
 * chord its own shape per voice (no sum-and-difference tones as one shaper on the mix would), and SAT OFF
 * is the render there was (golden.txt).
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "instr.h"

#define SEC (FS / CTL)                      /* blocks in a second */
#define NB (SEC / 2u)                       /* half a second */
static int32_t out[2 * CTL];
static int16_t buf[NB * CTL];
static uint32_t fails;

static void ok(int c, const char *what)
{
    printf("%-58s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

/* held notes (A2 110 Hz, or more) on ANALOG with the filter and SAT set so; no sends, no envelopes */
static int32_t render_n(int ftyp, int cut, int res, int sat, int sdrv, int wave, const int *notes, int nn)
{
    uint32_t b, i;
    int32_t peak = 0, k;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(&trk[0], 0, 0);
    trk[0].p[P_E0] = (int16_t)wave;
    trk[0].p[P_E1] = 0;                      /* one oscillator */
    trk[0].p[P_E2] = 0;
    trk[0].p[P_E4] = (int16_t)cut;
    trk[0].p[P_E5] = (int16_t)res;
    trk[0].p[P_E6] = 0;
    trk[0].p[P_E7] = 0;
    trk[0].p[P_E12] = (int16_t)ftyp;
    trk[0].p[P_E13] = (int16_t)sat;
    trk[0].p[P_E14] = (int16_t)sdrv;
    trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    for (i = 0; i < 4u; i++)
        trk[0].p[P_DIST + i] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    for (k = 0; k < nn; k++)
        input_on(&trk[0], (uint8_t)notes[k], 100);
    for (b = 0; b < NB; b++) {
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = out[2 * i], a = x < 0 ? -x : x;
            peak = a > peak ? a : peak;
            buf[b * CTL + i] = (int16_t)clamp(x, -32768, 32767);
        }
    }
    return peak;
}
static int32_t render(int ftyp, int cut, int res, int sat, int sdrv, int wave)
{
    static const int A2[] = {45};
    return render_n(ftyp, cut, res, sat, sdrv, wave, A2, 1);
}

/* amplitude of frequency hz in buf over the last 0.4 s (Goertzel) */
static double tone(double hz)
{
    uint32_t from = (NB / 5u) * CTL, to = NB * CTL, i;
    double w = 2 * M_PI * hz / FS, c = 2 * cos(w), s1 = 0, s2 = 0;
    for (i = from; i < to; i++) {
        double s0 = buf[i] + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return sqrt(s1 * s1 + s2 * s2 - c * s1 * s2) * 2 / (double)(to - from);
}
static double rms(void)
{
    uint32_t from = (NB / 5u) * CTL, to = NB * CTL, i;
    double s = 0;
    for (i = from; i < to; i++)
        s += (double)buf[i] * buf[i];
    return sqrt(s / (to - from));
}
static double mean(void)
{
    uint32_t from = (NB / 5u) * CTL, to = NB * CTL, i;
    double s = 0;
    for (i = from; i < to; i++)
        s += buf[i];
    return s / (to - from);
}
static double overtones(void)                /* harmonics 2..12 of 110 Hz against the fundamental */
{
    double h = 0;
    uint32_t k;
    for (k = 2; k <= 12u; k++)
        h += tone(110.0 * k);
    return h / tone(110);
}

int main(void)
{
    static const int CUTS[] = {0, 64, 127}, RESS[] = {0, 127}, SDRVS[] = {0, 64, 127}, FTYPS[] = {0, 1, 4};
    static const char *const NAME[] = {"OFF", "WARM", "HARD", "FOLD"};
    uint32_t s, a, b, c, f, w;
    int32_t worst = 0;
    char what[96];

    for (s = 1; s < 4u; s++)
        for (w = 0; w < 5u; w++)
            for (a = 0; a < 3u; a++)
                for (b = 0; b < 2u; b++)
                    for (c = 0; c < 3u; c++)
                        for (f = 0; f < 3u; f++) {
                            int32_t p = render(FTYPS[f], CUTS[a], RESS[b], (int)s, SDRVS[c], (int)w);
                            worst = p > worst ? p : worst;
                        }
    snprintf(what, sizeof what, "bounded at every SAT x SDRV x CUT x RES x FTYP x WAVE (peak %d)", (int)worst);
    ok(worst < 32767, what);

    /* a sine through the open LP24 (FTYP set: the Jangada render, SAT OFF as reference) */
    {
        double r0, o0;
        render(1, 127, 0, 0, 64, 3);
        r0 = rms(), o0 = overtones();
        printf("   OFF: rms %.0f, overtones %.3f\n", r0, o0);
        for (s = 1; s < 4u; s++) {
            double rl, ol, rh, oh, dc;
            render(1, 127, 0, (int)s, 0, 3);
            rl = rms(), ol = overtones();
            render(1, 127, 0, (int)s, 127, 3);
            rh = rms(), oh = overtones(), dc = mean();
            printf("   %s: SDRV 0 rms %.0f overtones %.3f; SDRV 127 rms %.0f overtones %.3f, DC %.0f\n",
                   NAME[s], rl, ol, rh, oh, dc);
            snprintf(what, sizeof what, "%s: SDRV 127 adds overtones (> 3x SDRV 0, > 0.1)", NAME[s]);
            ok(oh > ol * 3 && oh > 0.1, what);
            snprintf(what, sizeof what, "%s: the level stays within -9 .. +4 dB of OFF", NAME[s]);
            ok(rh > r0 * 0.35 && rh < r0 * 1.6 && rl > r0 * 0.35 && rl < r0 * 1.6, what);
            snprintf(what, sizeof what, "%s: DC under 3 %% of the rms", NAME[s]);
            ok(fabs(dc) < rh * 0.03, what);
        }
    }

    /* per voice: A2 + E3 (110 + 164.8 Hz) through HARD makes no 54.8 Hz difference tone */
    {
        static const int FIFTH[] = {45, 52};
        double d, d0, f1, f0;
        render_n(1, 127, 0, 0, 0, 3, FIFTH, 2);
        d0 = tone(164.81 - 110.0), f0 = tone(110);
        render_n(1, 127, 0, 2, 127, 3, FIFTH, 2);
        d = tone(164.81 - 110.0), f1 = tone(110);
        printf("   fifth: OFF 110 Hz %.0f, difference %.1f; HARD 110 Hz %.0f, difference %.1f\n", f0, d0, f1, d);
        ok(d / f1 < d0 / f0 * 1.5 + 0.002, "a chord: each voice shaped alone (no new difference tone)");
    }

    printf(fails ? "SAT: %u FAILED\n" : "SAT: all ok\n", fails);
    return fails != 0;
}
