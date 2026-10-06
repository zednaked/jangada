/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the FM6 engine's AMS (an operator's LFO amplitude modulation, firmware/src/fm6_core.c fm6_ams_pt /
 * fm6_note_compute) on the host: the share pt against Dexed's double math for every modulation (pt = exp(sensamp /
 * 262144 * 0.07 + 12.2), sensamp 0 .. 2^24), and a voice played with AMS 3 and a light LFO AMD: a mild tremolo
 * (a few per cent), the level near the same voice without AMS (not pinned at the top, not lost), the voice free
 * after the note-off. (Jangada 0.5 shifted pt up by 2^14 and truncated the share to 32 bits: any AMS > 0 pinned
 * the level, lost it, or never let the voice end.)
 * Build with the same generated headers and flags as hostsim.c:
 *   cc -O2 -w -Ibuild/gen -Ifirmware/src tests/fm6_ams_test.c -lm */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails;
static void check(const char *what, int ok)
{
    printf("%-72s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

/* track 1 on FM6, dry, playing the init voice with its carrier (OP1) at AMS ams and the LFO's AMD amd
 * (a triangle at speed 40, no delay, no pitch modulation) */
static track_t *setup(uint32_t ams, uint32_t amd)
{
    track_t *t = &trk[0];
    uint8_t v[FP_SIZE + 1u];
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.g[G_DMIX] = 0;
    host_preset(t, ENGI_FM6, 0);
    t->p[P_DIST] = t->p[P_CHOR] = t->p[P_DLY] = t->p[P_REV] = 0;
    fm6_unpack(FM6_INIT, v);
    v[5u * FP_OP + FP_AMS] = (uint8_t)ams;              /* OP1: the init voice's carrier */
    v[FP_LAMD] = (uint8_t)amd;
    v[FP_LFS] = 40;
    v[FP_LFD] = v[FP_LPMD] = 0;
    v[FP_LFW] = 0;
    v[FP_LKS] = 1;
    fm6_set_patch(0, v);
    fm6_slot[0] = (uint8_t)t->p[P_E7];                  /* (fm6_poll would reload PTCH's patch) */
    return t;
}

#define WIN 14u                                          /* blocks per window: ~10 ms, 2.6 cycles of the C4 sine */
static int32_t out[2 * CTL];
static double win_peak(void)                             /* one window of the mix: its peak (a sine: its amplitude) */
{
    int32_t pk = 0;
    uint32_t b, i;
    for (b = 0; b < WIN; b++) {
        mix_block(out, CTL);
        for (i = 0; i < 2u * CTL; i++)
            pk = out[i] > pk ? out[i] : -out[i] > pk ? -out[i] : pk;
    }
    return pk;
}
static uint32_t wins(double s) { return (uint32_t)(s * FS / (WIN * CTL)); }

typedef struct { double mean, depth; int free; } play_t;
/* a note held 2.3 s, measured from 0.3 s: the mean peak and the tremolo depth (max - min) / (max + min) of the
 * windows; then let go, free within 4 s? */
static play_t play(uint32_t ams, uint32_t amd)
{
    track_t *t = setup(ams, amd);
    play_t r = {0, 0, 0};
    double lo = 1e9, hi = 0;
    uint32_t i, n = wins(2.0);
    input_on(t, 60, 100);
    for (i = 0; i < wins(0.3); i++)
        win_peak();
    for (i = 0; i < n; i++) {
        double x = win_peak();
        r.mean += x / n;
        lo = x < lo ? x : lo;
        hi = x > hi ? x : hi;
    }
    r.depth = hi + lo > 0 ? (hi - lo) / (hi + lo) : 1;
    input_off(t, 60);
    for (i = 0; i < wins(4.0) && busy_now(); i++)
        win_peak();
    r.free = !busy_now();
    return r;
}

int main(void)
{
    uint32_t sa, bad = 0, worst_sa = 0;
    double worst = 0;
    play_t dry, ams3, ams1, deep;

    /* the share against Dexed, every modulation: within 0.1 % (the 2^x table, the slope rounded) */
    for (sa = 0; sa <= 1u << 24; sa++) {
        double want = exp((float)sa / 262144 * 0.07 + 12.2), got = fm6_ams_pt(sa), e = fabs(got - want) / want;
        if (e > worst) {
            worst = e;
            worst_sa = sa;
        }
        bad += e > 1e-3;
    }
    printf("AMS: pt against Dexed's exp(): worst %.2e at sensamp %u (pt %u)\n", worst, worst_sa, fm6_ams_pt(worst_sa));
    check("AMS: pt = exp(sensamp / 262144 * 0.07 + 12.2) for every sensamp, within 0.1 %", !bad);
    check("AMS: pt at rest = e^12.2 (2^17.6), at the deepest modulation 2^24.1",
          fm6_ams_pt(0) > 198000u && fm6_ams_pt(0) < 199500u && fm6_ams_pt(1u << 24) > (1u << 24) && fm6_ams_pt(1u << 24) < (1u << 25));

    dry = play(0, 10);
    ams3 = play(3, 10);
    ams1 = play(1, 10);
    deep = play(3, 70);
    printf("AMS 0: mean %.0f depth %.3f  AMS 3 / AMD 10: mean %.0f depth %.3f  AMS 1: mean %.0f depth %.3f  AMS 3 / AMD 70: mean %.0f depth %.3f\n",
           dry.mean, dry.depth, ams3.mean, ams3.depth, ams1.mean, ams1.depth, deep.mean, deep.depth);
    check("no AMS: a steady sustain (depth below 1 %)", dry.mean > 1000 && dry.depth < 0.01);
    check("AMS 3, AMD 10: a mild tremolo (2 .. 25 %)", ams3.depth > 0.02 && ams3.depth < 0.25);
    check("AMS 3, AMD 10: the level near the dry voice's (-3 dB .. 0 dB: not pinned, not lost)",
          ams3.mean > dry.mean * 0.7 && ams3.mean <= dry.mean * 1.02);
    check("AMS 1: a lighter tremolo than AMS 3", ams1.depth > 0.002 && ams1.depth < ams3.depth);
    check("AMS 3, AMD 70: a deep tremolo (more than AMD 10, under 100 %)", deep.depth > ams3.depth * 2 && deep.depth < 0.99);
    check("the voice is free after the note-off (every case)", dry.free && ams3.free && ams1.free && deep.free);
    printf("%s\n", fails ? "FM6 AMS TEST FAILED" : "FM6 AMS test passed");
    return fails != 0;
}
