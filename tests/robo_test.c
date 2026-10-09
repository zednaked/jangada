/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada 0.9.1: the ROBO engine (eng_robo.c) on the host. Each mode sounds, stays bounded, has no offset and
 * plays the key's pitch (GENDY too, its wave walking); a fixed SEED walks the same way twice, SEED 0 not; a
 * high WALSH note and a MODE change under a held note stay bounded.
 * Build with the same generated headers and flags as hostsim.c. */
#define main hostsim_main
#include "hostsim.c"
#undef main

#define NB 400u                             /* blocks: 0.29 s */
static int32_t out[2 * CTL];
static int16_t buf[NB * CTL];
static uint32_t fails;

static void ok(int c, const char *what)
{
    printf("%-62s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

/* track 1 as ROBO with {MODE, A, B, C, D, DRFT, CRSH, TONE}, dry, no pitch movement */
static void setup(const int16_t *e)
{
    uint32_t k;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(&trk[0], ENGI_ROBO, 0);
    for (k = 0; k < 8u; k++)
        trk[0].p[P_E0 + k] = e[k];
    trk[0].p[P_TRANS] = trk[0].p[P_LD_PIT] = trk[0].p[P_GLIDE] = 0;
    trk[0].p[P_ATK] = 0, trk[0].p[P_SUS] = 127, trk[0].p[P_REL] = 30;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = trk[0].p[P_DIST] = 0;
    punch.req = -1;
    memset(&fx, 0, sizeof fx);
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(&rev_u, 0, sizeof rev_u);
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    memset(&duck, 0, sizeof duck);
    duck.t = 0xFFFFFFFFu, duck.g0 = duck.g1 = 32767;
}

/* NB blocks of one held note into buf; the peak and the mean of the second half */
static void render(uint32_t note, int32_t *peak, int32_t *mean)
{
    uint32_t b, i;
    int32_t p = 0;
    int64_t sum = 0;
    input_on(&trk[0], note, 100);
    for (b = 0; b < NB; b++) {
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = out[2 * i], a = x < 0 ? -x : x;
            p = a > p ? a : p;
            if (b >= NB / 2u)
                sum += x;
            buf[b * CTL + i] = (int16_t)clamp(x, -32768, 32767);
        }
    }
    input_off(&trk[0], note);
    *peak = p;
    *mean = (int32_t)(sum / (int64_t)(NB / 2u * CTL));
}

/* the period of buf's second half, samples: the lag of the autocorrelation's peak in lo..hi */
static uint32_t period(uint32_t lo, uint32_t hi)
{
    uint32_t lag, best = 0, i, s0 = NB * CTL / 2u;
    double bv = -1e30;
    for (lag = lo; lag <= hi; lag++) {
        double c = 0;
        for (i = s0; i + lag < NB * CTL; i++)
            c += (double)buf[i] * buf[i + lag];
        c /= (double)(NB * CTL - s0 - lag);
        if (c > bv)
            bv = c, best = lag;
    }
    return best;
}

int main(void)
{
    static const int16_t MODES[4][8] = {
        {RB_VOSIM, 60, 30, 50, 64, 0, 0, 120},
        {RB_GENDY, 40, 50, 40, 0, 0, 0, 120},
        {RB_WALSH, 40, 40, 64, 0, 0, 0, 120},
        {RB_SCAN, 60, 20, 30, 64, 40, 0, 120},
    };
    static const char *const NAME[4] = {"VOSIM", "GENDY", "WALSH", "SCAN"};
    char what[96];
    int32_t pk, mn;
    uint32_t md, per;

    for (md = 0; md < 4u; md++) {
        setup(MODES[md]);
        render(57, &pk, &mn);                       /* A3, 220 Hz: 200.45 samples */
        snprintf(what, sizeof what, "%s: sounds, bounded, no offset (peak %d, mean %d)", NAME[md], pk, mn);
        ok(pk > 3000 && pk < 32767 && (mn < 0 ? -mn : mn) < 300, what);
        per = period(150, 260);
        snprintf(what, sizeof what, "%s: the key's pitch (period %u, want 200)", NAME[md], per);
        ok(per >= 199u && per <= 202u, what);
    }

    {                                               /* GENDY walking hard (STEP, TIME at the top): the pitch holds */
        static const int16_t e[8] = {RB_GENDY, 127, 30, 127, 0, 0, 0, 127};
        setup(e);
        render(57, &pk, &mn);
        per = period(150, 260);
        snprintf(what, sizeof what, "GENDY, STEP and TIME 127: the key's pitch (period %u)", per);
        ok(per >= 199u && per <= 202u, what);
    }

    {                                               /* GENDY: the walk moves it, a fixed SEED repeats */
        static int16_t a[NB * CTL];
        int16_t e[8] = {RB_GENDY, 90, 60, 80, 7, 0, 0, 120};
        uint32_t i, same = 0, n = NB * CTL;
        setup(e);
        render(57, &pk, &mn);
        memcpy(a, buf, sizeof a);
        setup(e);
        render(57, &pk, &mn);
        for (i = 0; i < n; i++)
            same += a[i] == buf[i];
        ok(same == n, "GENDY SEED 7: the same walk twice");
        e[4] = 0;
        setup(e);
        render(57, &pk, &mn);
        setup(e);
        render(57, &pk, &mn);
        for (same = 0, i = n / 2u; i < n; i++)
            same += a[i] == buf[i];
        ok(same < n / 4u, "GENDY SEED 0: another walk each note");
    }

    {                                               /* WALSH up high: the terms past Nyquist left out */
        int16_t e[8] = {RB_WALSH, 127, 127, 64, 0, 0, 0, 127};
        setup(e);
        render(108, &pk, &mn);
        snprintf(what, sizeof what, "WALSH at C8, SEQ 127: sounds, bounded (peak %d, mean %d)", pk, mn);
        ok(pk > 1000 && pk < 32767 && (mn < 0 ? -mn : mn) < 300, what);
    }

    {                                               /* SCAN: the shape moves (the timbre changes), DAMP 0 rings on */
        int16_t e[8] = {RB_SCAN, 80, 0, 20, 90, 0, 0, 127};
        int64_t d1 = 0, d2 = 0, e1 = 0, e2 = 0;
        uint32_t i, n = NB * CTL, q = 200u;           /* (one period at A3: 200 samples) */
        setup(e);
        render(57, &pk, &mn);
        for (i = n / 2u; i + q < n * 3u / 4u; i++)    /* a period against the next: shape held? */
            d1 += (int64_t)(buf[i] - buf[i + q]) * (buf[i] - buf[i + q]), e1 += (int64_t)buf[i] * buf[i];
        for (i = n / 2u; i + 20u * q < n; i++)        /* ... against 20 periods on: moved */
            d2 += (int64_t)(buf[i] - buf[i + 20u * q]) * (buf[i] - buf[i + 20u * q]), e2 += (int64_t)buf[i] * buf[i];
        snprintf(what, sizeof what, "SCAN: the shape moves (period to period %.2f, 20 periods %.2f)",
                 (double)d1 / (double)(e1 | 1), (double)d2 / (double)(e2 | 1));
        ok(d1 * 4 < e1 && d2 > d1 * 4, what);
        snprintf(what, sizeof what, "SCAN, DAMP 0: still sounding at the end (peak %d)", pk);
        {
            int32_t p2 = 0;
            for (i = n - 2000u; i < n; i++)
                p2 = buf[i] > p2 ? buf[i] : -buf[i] > p2 ? -buf[i] : p2;
            ok(p2 > 2000, what);
        }
    }

    {                                               /* MODE moved under a held note: nothing blows up */
        uint32_t b, i;
        int32_t p = 0;
        setup(MODES[1]);
        input_on(&trk[0], 45, 100);
        for (b = 0; b < 3u * NB; b++) {
            if (b % 50u == 49u)
                trk[0].p[P_E0] = (int16_t)((trk[0].p[P_E0] + 1) % RB_COUNT);
            mix_block(out, CTL);
            for (i = 0; i < CTL; i++)
                p = out[2 * i] > p ? out[2 * i] : -out[2 * i] > p ? -out[2 * i] : p;
        }
        snprintf(what, sizeof what, "MODE changed under a held note: bounded (peak %d)", p);
        ok(p < 32767, what);
    }

    {                                               /* SCAN pushed hard with no loss: it settles, bounded */
        int16_t e[8] = {RB_SCAN, 127, 0, 0, 127, 127, 0, 127};
        uint32_t b;
        setup(e);
        input_on(&trk[0], 45, 100);
        for (b = 0; b < 20u * NB; b++)              /* 5.8 s */
            mix_block(out, CTL);
        render(45, &pk, &mn);
        snprintf(what, sizeof what, "SCAN, STIF / HIT / DRFT 127, DAMP 0: bounded after 6 s (peak %d)", pk);
        ok(pk < 32767 && (mn < 0 ? -mn : mn) < 300, what);
    }

    if (fails)
        printf("ROBO: %u FAILED\n", fails);
    else
        puts("ROBO: all ok");
    return fails != 0;
}
