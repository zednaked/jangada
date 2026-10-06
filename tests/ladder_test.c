/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada LADR on the host (eng_analog.c ladder_*, ANALOG FTYP 4): the four-pole ladder low-pass. It
 * stays bounded at every CUT x RES x DRV corner, closes the highs as CUT goes down (24 dB / octave, more
 * than LP12), rings at the cutoff as RES goes up, thins the lows with the resonance as the hardware does
 * (but not to nothing), and does not leak into the other filter types (LP24 renders as before: golden.txt).
 * Prints what it costs on the host against LP24.
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

/* one held A2 (110 Hz) on ANALOG with the filter set so; no sends, no envelope on the filter */
static int32_t render(int ftyp, int cut, int res, int drv, int wave)
{
    uint32_t b, i;
    int32_t peak = 0;
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
    trk[0].p[P_E6] = (int16_t)drv;
    trk[0].p[P_E7] = 0;
    trk[0].p[P_E12] = (int16_t)ftyp;
    trk[0].p[P_ED_FLT] = 0;
    trk[0].p[P_ATK] = 0;
    trk[0].p[P_SUS] = 127;
    for (i = 0; i < 4u; i++)
        trk[0].p[P_DIST + i] = 0;
    trk[0].p[P_VOICE] = V_POLY;
    input_on(&trk[0], 45, 100);
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

int main(void)
{
    static const int CUTS[] = {0, 30, 64, 100, 127}, RESS[] = {0, 64, 100, 127}, DRVS[] = {0, 127};
    uint32_t a, b, c, w;
    int32_t worst = 0;
    double f_open, h_open, h_mid, h_mid24, f_r0, f_r127, r_r0, r_r127;
    char what[96];

    /* bounded: every corner, every wave (the output stays within 16 bits, the states never run away) */
    for (w = 0; w < 5u; w++)
        for (a = 0; a < sizeof CUTS / sizeof *CUTS; a++)
            for (b = 0; b < sizeof RESS / sizeof *RESS; b++)
                for (c = 0; c < sizeof DRVS / sizeof *DRVS; c++) {
                    int32_t p = render(4, CUTS[a], RESS[b], DRVS[c], (int)w);
                    worst = p > worst ? p : worst;
                }
    snprintf(what, sizeof what, "bounded at every CUT x RES x DRV x WAVE (peak %d)", (int)worst);
    ok(worst < 32767, what);

    /* the highs close as CUT goes down; steeper than LP12 at the same CUT */
    render(4, 127, 0, 0, 0);
    f_open = tone(110), h_open = tone(880);
    render(4, 40, 0, 0, 0);
    h_mid = tone(880);
    render(0, 40, 0, 0, 0);
    h_mid24 = tone(880);
    printf("   880 Hz: CUT 127 %.0f, CUT 40 LADR %.0f, CUT 40 LP12 %.0f\n", h_open, h_mid, h_mid24);
    ok(f_open > 1000, "a saw comes through the open ladder");
    ok(h_mid < h_open * 0.2, "CUT 40 takes the 8th harmonic down (> 14 dB)");
    ok(h_mid < h_mid24, "the ladder is steeper than LP12");

    /* resonance: rings near the cutoff, the fundamental thins but stays */
    render(4, 64, 0, 0, 0);
    f_r0 = tone(110);
    {   /* the loudest harmonic above the 4th: where the cutoff is */
        uint32_t h;
        r_r0 = 0;
        for (h = 4; h < 40; h++) {
            double t = tone(110.0 * h);
            r_r0 = t > r_r0 ? t : r_r0;
        }
    }
    render(4, 64, 127, 0, 0);
    f_r127 = tone(110);
    {
        uint32_t h;
        r_r127 = 0;
        for (h = 4; h < 40; h++) {
            double t = tone(110.0 * h);
            r_r127 = t > r_r127 ? t : r_r127;
        }
    }
    printf("   RES 0 -> 127: fundamental %.0f -> %.0f, peak harmonic %.0f -> %.0f\n", f_r0, f_r127, r_r0, r_r127);
    ok(r_r127 > r_r0 * 3, "RES 127 rings at the cutoff (> 9 dB)");
    ok(f_r127 < f_r0 && f_r127 > f_r0 * 0.25, "the lows thin with the resonance, not to nothing");

    /* the cost on the host, against LP24 (instructions a sample, as regress.c counts them) */
    {
        uint64_t t0, t1, t2;
        render(1, 64, 64, 64, 0);
        t0 = now_ns();
        for (a = 0; a < 20u; a++)
            render(1, 64, 64, 64, 0);
        t1 = now_ns();
        for (a = 0; a < 20u; a++)
            render(4, 64, 64, 64, 0);
        t2 = now_ns();
        printf("   host time, one voice: LP24 %.1f ms, LADR %.1f ms (20 x 0.5 s)\n", (t1 - t0) / 1e6, (t2 - t1) / 1e6);
    }

    printf(fails ? "LADDER: %u FAILED\n" : "LADDER: all ok\n", fails);
    return fails != 0;
}
