/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada 0.9: the INSERT of a track (fx.c track_insert, after Felucca 1.5) on the host. TYPE OFF and MIX 0
 * leave the track bit for bit as before; each type changes the sound, stays bounded, has no offset; a TYPE
 * change and TYPE OFF fade (no click); OFF while silent rests it; it costs nothing while OFF.
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

#define SEC (FS / CTL)                      /* blocks in a second */
#define NB (SEC / 2u)
static int32_t out[2 * CTL];
static int16_t ref[NB * CTL], cur[NB * CTL];
static uint32_t fails;

static void ok(int c, const char *what)
{
    printf("%-58s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

static void setup(void)
{
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    host_preset(&trk[0], 0, 0);              /* ANALOG SAW LEAD on track 1 */
    trk[0].p[P_REL] = 60;
    trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;   /* dry: the track's own sound only */
    punch.req = -1;
    memset(&fx, 0, sizeof fx);
    memset(dly_buf, 0, sizeof dly_buf);
    memset(cho_buf, 0, sizeof cho_buf);
    memset(rev_comb, 0, sizeof rev_comb);
    memset(&rev_u, 0, sizeof rev_u);
    memset(ins, 0, sizeof ins);
    lim_env = LIM_T;
    dc_l = dc_r = dce_l = dce_r = 0;
    memset(&duck, 0, sizeof duck);
    duck.t = 0xFFFFFFFFu, duck.g0 = duck.g1 = 32767;
}

/* NB blocks of a two-note chord into buf (left); the largest sample step, the peak, the mean */
static void render(int16_t *buf, int32_t *step, int32_t *peak, int32_t *mean)
{
    uint32_t b, i;
    int32_t last = 0, s = 0, p = 0;
    int64_t sum = 0;
    input_on(&trk[0], 45, 100);
    input_on(&trk[0], 52, 100);
    for (b = 0; b < NB; b++) {
        mix_block(out, CTL);
        for (i = 0; i < CTL; i++) {
            int32_t x = out[2 * i], d = x - last, a = x < 0 ? -x : x;
            d = d < 0 ? -d : d;
            if (b > 4u)
                s = d > s ? d : s;
            p = a > p ? a : p;
            sum += x;
            last = x;
            buf[b * CTL + i] = (int16_t)x;
        }
    }
    input_off(&trk[0], 45);
    input_off(&trk[0], 52);
    *step = s;
    *peak = p;
    *mean = (int32_t)(sum / (int64_t)(NB * CTL));
}

static uint32_t differ(void)
{
    uint32_t i, n = 0;
    for (i = 0; i < NB * CTL; i++)
        n += ref[i] != cur[i];
    return n;
}

int main(void)
{
    static const char *const NAME[] = {"", "SOFT", "HARD", "FOLD", "FUZZ", "CRUSH", "PHASR", "FLANG", "CHOR"};
    int32_t s0, p0, m0, s, p, m;
    uint32_t ty, b;
    char what[80];

    setup();
    render(ref, &s0, &p0, &m0);
    setup();
    trk[0].p[P_ITYPE] = IT_FOLD;
    trk[0].p[P_IMIX] = 0;
    render(cur, &s, &p, &m);
    ok(differ() == 0 && !trk[0].ins_run, "MIX 0: the track bit for bit, the INSERT asleep");

    for (ty = IT_SOFT; ty < IT_N; ty++) {
        setup();
        trk[0].p[P_ITYPE] = (int16_t)ty;
        trk[0].p[P_IA] = 90;
        trk[0].p[P_IB] = 100;
        trk[0].p[P_IC] = 90;
        trk[0].p[P_IMIX] = 127;
        render(cur, &s, &p, &m);
        snprintf(what, sizeof what, "%-5s: changes the sound, bounded, no offset", NAME[ty]);
        ok(differ() > NB * CTL / 4u && p < 32767 && (m < 0 ? -m : m) < 300, what);
    }

    setup();                                 /* a TYPE change fades: no step past the dry ones' size */
    trk[0].p[P_ITYPE] = IT_FLANGER;
    trk[0].p[P_IMIX] = 127;
    input_on(&trk[0], 45, 100);
    for (b = 0; b < 40u; b++)
        mix_block(out, CTL);
    {
        int32_t last = out[2 * (CTL - 1)], big = 0;
        uint32_t i;
        trk[0].p[P_ITYPE] = IT_FOLD;
        for (b = 0; b < 40u; b++) {
            mix_block(out, CTL);
            for (i = 0; i < CTL; i++) {
                int32_t d = out[2 * i] - last;
                d = d < 0 ? -d : d;
                big = d > big ? d : big;
                last = out[2 * i];
            }
        }
        ok(big < 20000 && ins[0].type == IT_FOLD, "FLANG -> FOLD: faded across, no click");
    }
    trk[0].p[P_ITYPE] = IT_OFF;
    input_off(&trk[0], 45);
    for (b = 0; b < 3u * SEC; b++)
        mix_block(out, CTL);
    ok(!trk[0].ins_run && ins[0].w == 0, "TYPE OFF: it fades out and rests");

    if (fails)
        printf("INSERT: %u FAILED\n", fails);
    else
        puts("INSERT: all ok");
    return fails != 0;
}
