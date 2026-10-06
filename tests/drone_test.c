/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada DRONES (firmware/src/drone.c) on the host: EVOL (the slow walks), TENS (the tension macro) and
 * its RAMP, the DRIFT source of the matrix, the presets that evolve, and the cost.
 *   build/host/drone_test [DIR]     DIR: also write the demos (WAV, 44.1 kHz stereo) there
 * Build with the same generated headers and flags as hostsim.c. */
#define main hostsim_main
#include "hostsim.c"
#undef main
#include "instr.h"
#include <sys/wait.h>

static int fails;
static void check(const char *what, int ok)
{
    printf("%-66s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}

#define TICK_S ((double)CTL / FS)                        /* one control tick, s */
static uint32_t ticks(double s) { return (uint32_t)(s / TICK_S); }

static track_t *setup(uint32_t e, uint32_t pi)
{
    track_t *t = &trk[0];
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    memset(drn, 0, sizeof drn);
    drn[0].seed = 0x6A09E667u;
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.g[G_DMIX] = 0;                                  /* (no delay feedback in the comparisons) */
    host_preset(t, e, pi);
    return t;
}

/* a chord, struck and let go: with HOLD and ARP RPT the drone holds itself */
static void chord(track_t *t, const uint8_t *n, uint32_t k)
{
    uint32_t i;
    for (i = 0; i < k; i++)
        input_on(t, n[i], 96);
    for (i = 0; i < k; i++)
        input_off(t, n[i]);
}

static int32_t blk_out[2 * CTL];
static uint64_t run_hash;
static void run(uint32_t nt)                             /* nt control ticks of the whole mix */
{
    uint32_t i, k;
    for (i = 0; i < nt; i++) {
        mix_block(blk_out, CTL);
        for (k = 0; k < 2u * CTL; k++) {
            run_hash ^= (uint32_t)blk_out[k];
            run_hash *= 0x100000001B3ull;
        }
    }
}

/* the hash of a render in a fork()ed child: every state (FX lines, limiter, seeds) as at boot */
static uint64_t fork_hash(void (*fn)(void))
{
    int fd[2];
    uint64_t h = 0;
    pid_t p;
    if (pipe(fd))
        return 0;
    p = fork();
    if (!p) {
        close(fd[0]);
        fn();
        if (write(fd[1], &run_hash, sizeof run_hash) != (ssize_t)sizeof run_hash)
            _exit(1);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != (ssize_t)sizeof h)
        h = 0;
    close(fd[0]);
    waitpid(p, 0, 0);
    return h;
}

/* what drone_voice gives voice c: its level (Q15, from full) and its fine tune */
static int32_t voice_gain(const track_t *t, uint32_t c, int32_t *fine)
{
    vmod_t m;
    memset(&m, 0, sizeof m);
    m.amp0 = m.amp1 = 32767;
    m.pitch16 = 60 * 16;
    drone_voice(t, c, &m);
    *fine = m.fine;
    return m.amp1;
}

static int sounding(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            return 1;
    return 0;
}

/* -------------------------------------------------------------- demos --- */
static FILE *wav;
static uint32_t wav_frames;
static void wav_open(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    wav = fopen(path, "wb");
    wav_frames = 0;
    if (wav)
        wav_hdr(wav, 0);
}
static void wav_run(double s)
{
    uint32_t n = ticks(s), i, k;
    for (i = 0; i < n; i++) {
        mix_block(blk_out, CTL);
        for (k = 0; k < CTL; k++)
            wav_put(wav, blk_out[2 * k], blk_out[2 * k + 1]);
        wav_frames += CTL;
    }
}
static void wav_close(void)
{
    if (!wav)
        return;
    fseek(wav, 0, SEEK_SET);
    wav_hdr(wav, wav_frames);
    fclose(wav);
    wav = 0;
}

/* the presets that evolve: engine, preset, the chord, the file */
static const struct {
    uint8_t e;
    const char *name, *file;
    uint8_t n[4];
} DEMO[] = {
    {0, "FERRUGEM", "ferrugem.wav", {38, 45, 50, 53}},  /* D minor, low */
    {1, "ABISMO", "abismo.wav", {33, 40, 45, 48}},      /* A minor, lower */
    {7, "SERTAO", "sertao.wav", {50, 57, 62, 65}},      /* the sanfona's register */
    {8, "CINZA", "cinza.wav", {50, 53, 57, 60}},
};
#define NDEMO (sizeof DEMO / sizeof DEMO[0])

static uint32_t preset_by_name(uint32_t e, const char *name)
{
    uint32_t i;
    for (i = 0; i < ENGINES[e]->npresets; i++)
        if (!strcmp(ENGINES[e]->presets[i].name, name))
            return i;
    return 0xFFu;
}

static void demos(const char *dir)
{
    uint32_t i, pi;
    track_t *t;
    for (i = 0; i < NDEMO; i++) {                        /* each preset, 50 s, then DRONE OFF and the tail */
        pi = preset_by_name(DEMO[i].e, DEMO[i].name);
        t = setup(DEMO[i].e, pi);
        song.g[G_DMIX] = GP[G_DMIX].def;
        wav_open(dir, DEMO[i].file);
        chord(t, DEMO[i].n, 4);
        wav_run(50.0);
        latch_off_req = 1;
        wav_run(8.0);
        wav_close();
    }
    {   /* TENSION from 0 to 100 % in 16 bars (32 s at 120 BPM), on DRONE SAW with a little EVOL */
        static const uint8_t n[4] = {38, 45, 50, 53};
        t = setup(0, preset_by_name(0, "DRONE SAW"));
        song.g[G_DMIX] = GP[G_DMIX].def;
        t->p[P_EVOL] = 40;
        t->p[P_TENS] = 127;
        t->p[P_TRAMP] = 5;                               /* 16BAR */
        wav_open(dir, "tension_ramp_16bar.wav");
        chord(t, n, 4);
        wav_run(40.0);
        latch_off_req = 1;
        wav_run(8.0);
        wav_close();
    }
    {   /* the same drone without and with the evolution: FERRUGEM, EVOL 0 TENS 0, then as it is */
        static const char *const F[2] = {"compare_static.wav", "compare_evolving.wav"};
        uint32_t k;
        for (k = 0; k < 2u; k++) {
            t = setup(0, preset_by_name(0, "FERRUGEM"));
            song.g[G_DMIX] = GP[G_DMIX].def;
            if (!k)
                t->p[P_EVOL] = t->p[P_TENS] = 0;
            wav_open(dir, F[k]);
            chord(t, DEMO[0].n, 4);
            wav_run(36.0);
            latch_off_req = 1;
            wav_run(6.0);
            wav_close();
        }
    }
    printf("drone: demos in %s\n", dir);
}

static const uint8_t CH[4] = {38, 45, 50, 53};
static void today(uint32_t ramp)                         /* DRONE SAW (EVOL 0, TENS 0), 20 s */
{
    track_t *t = setup(0, preset_by_name(0, "DRONE SAW"));
    t->p[P_TRAMP] = (int16_t)ramp;
    chord(t, CH, 4);
    run_hash = 14695981039346656037ull;
    run(ticks(20.0));
}
static void today_off(void) { today(0); }
static void today_ramp(void) { today(5); }

/* ---------------------------------------------------------------- main --- */
int main(int argc, char **argv)
{
    track_t *t;
    uint32_t i, c;

    {   /* EVOL 0, TENS 0: nothing changes, whatever RAMP says (the golden renders check the rest) */
        uint64_t a = fork_hash(today_off), b = fork_hash(today_ramp);
        check("TENS 0, EVOL 0 (RAMP 16BAR): the drone of today, to the sample", a && a == b);
    }

    {   /* the walks: slow, smooth, inside +-1, not the same twice, four of them apart */
        static int16_t w[DR_NW][6000];                   /* every 20 ms over 120 s */
        int32_t lo = 32767, hi = -32767, dmax = 0, cutmax = 0, last[DR_NW] = {0}, x;
        uint32_t zc = 0, n = 0, k, ok_bounds = 1;
        double diff = 0, corr = 0, sa = 0, sb = 0;
        t = setup(0, preset_by_name(0, "DRONE SAW"));
        t->p[P_EVOL] = 127;
        chord(t, CH, 4);
        for (i = 0; i < ticks(120.0); i++) {
            run(1);
            for (c = 0; c < DR_NW; c++) {
                x = drn[0].w[c];
                ok_bounds &= x >= -32767 && x <= 32767;
                if (i && abs(x - last[c]) > dmax)
                    dmax = abs(x - last[c]);
                last[c] = x;
            }
            x = drn[0].cut < 0 ? -drn[0].cut : drn[0].cut;
            cutmax = x > cutmax ? x : cutmax;
            if (i % 28u == 0u && n < 6000u) {
                for (c = 0; c < DR_NW; c++)
                    w[c][n] = (int16_t)drn[0].w[c];
                n++;
            }
        }
        for (k = 0; k < n; k++) {
            lo = w[0][k] < lo ? w[0][k] : lo;
            hi = w[0][k] > hi ? w[0][k] : hi;
            zc += k && ((w[0][k] < 0) != (w[0][k - 1] < 0));
        }
        for (k = 0; k + 3000u < n; k++)                  /* the second minute is not the first again */
            diff += fabs((double)w[0][k] - w[0][k + 3000u]) / 32767.0;
        diff /= n - 3000u;
        for (k = 0; k < n; k++) {                        /* walk 0 against walk 1: their own ways */
            corr += (double)w[0][k] * w[1][k];
            sa += (double)w[0][k] * w[0][k];
            sb += (double)w[1][k] * w[1][k];
        }
        corr = fabs(corr) / sqrt(sa * sb + 1);
        printf("drone: walk 0 over 120 s: %.2f .. %.2f, %u zero crossings, max step %d / tick (Q15), "
               "minute 2 vs 1: %.2f apart, walks 0 / 1 correlation %.2f\n",
               lo / 32767.0, hi / 32767.0, zc, dmax, diff, corr);
        check("EVOL: the walks stay inside -1 .. 1", ok_bounds);
        check("EVOL: they wander (over 120 s walk 0 spans more than half the range)", hi - lo > 32767);
        check("EVOL: slowly (2 .. 40 zero crossings in 120 s: cycles of tens of seconds)", zc >= 2u && zc <= 40u);
        check("EVOL: smoothly (no step above 1/800 of the range per 0.7 ms tick)", dmax <= 82);
        check("EVOL: not a loop (the second minute is not the first again)", diff > 0.15);
        check("EVOL: the walks go their own ways (|correlation| < 0.8)", corr < 0.8);
        check("EVOL: the filter moves inside +-22 steps", cutmax <= 22 * 256);
    }

    {   /* no clicks: what the voices get changes by tiny steps per block (a block ramps the amplitude) */
        int32_t pcut = 0, pshp = 0, pg[NVOICE] = {0}, pf[NVOICE] = {0}, dcut = 0, dshp = 0, dg = 0, df = 0;
        int16_t pe[NEDIT];
        int32_t de = 0, pdist = 0;
        t = setup(0, preset_by_name(0, "FERRUGEM"));
        t->p[P_EVOL] = 127;
        t->p[P_TENS] = 127;
        t->p[P_TRAMP] = 1;                               /* the fastest ramp: 1 bar */
        chord(t, CH, 4);
        for (i = 0; i < ticks(60.0); i++) {
            int16_t keep[NEDIT];
            run(1);
            memcpy(keep, &t->p[P_E0], sizeof keep);       /* this block's engine values, as the voices see them */
            if (drone_tick(t, 0)) {
                for (c = 0; c < NEDIT; c++)
                    if (i > 2u && abs(t->p[P_E0 + c] - pe[c]) > de)
                        de = abs(t->p[P_E0 + c] - pe[c]);
                memcpy(pe, &t->p[P_E0], sizeof pe);
                drone_restore(t);
            }
            if (i > 2u) {
                dcut = abs(drn[0].cut - pcut) > dcut ? abs(drn[0].cut - pcut) : dcut;
                dshp = abs(drn[0].shp - pshp) > dshp ? abs(drn[0].shp - pshp) : dshp;
            }
            pcut = drn[0].cut;
            pshp = drn[0].shp;
            if (i > 2u && abs(drn[0].dist - pdist) > de)
                de = abs(drn[0].dist - pdist);
            pdist = drn[0].dist;
            for (c = 0; c < NVOICE; c++) {
                int32_t f, g = voice_gain(t, c, &f);
                if (i > 2u) {
                    dg = abs(g - pg[c]) > dg ? abs(g - pg[c]) : dg;
                    df = abs(f - pf[c]) > df ? abs(f - pf[c]) : df;
                }
                pg[c] = g;
                pf[c] = f;
            }
            (void)keep;
        }
        printf("drone: largest change per block: cutoff %d/256 step, shape %d/256, level %d/32768, fine %d/4096, "
               "engine parameter or DIST %d\n", dcut, dshp, dg, df, de);
        check("no clicks: cutoff and shape move < 1/2 step per block", dcut < 128 && dshp < 128);
        check("no clicks: a voice's level moves < 0.1 % per block, its tune < 0.5 cent", dg < 33 && df <= 1);
        check("no clicks: an engine parameter or the DIST moves at most 1 per block", de <= 1);
    }

    {   /* it stops when the drone stops, and starts again from the preset's own sound */
        int32_t w0[DR_NW];
        uint32_t still = 1;
        t = setup(0, preset_by_name(0, "DRONE SAW"));
        t->p[P_EVOL] = 127;
        chord(t, CH, 4);
        run(ticks(20.0));
        check("the walks run while the drone sounds", drn[0].on && (drn[0].w[0] | drn[0].w[1]) != 0);
        latch_off_req = 1;                               /* ARP held: DRONE OFF */
        for (i = 0; i < ticks(20.0) && sounding(t); i++)
            run(1);
        check("DRONE OFF: the voices end with the release", !sounding(t));
        memcpy(w0, drn[0].w, sizeof w0);
        run(ticks(10.0));
        for (c = 0; c < DR_NW; c++)
            still &= drn[0].w[c] == w0[c];
        check("... and the walks stop with them (10 s later: the same)", still && !drn[0].on);
        chord(t, CH, 4);
        run(1);
        check("a new drone starts from the preset's own sound (walks at 0)",
              drn[0].on && abs(drn[0].w[0]) < 64 && abs(drn[0].w[1]) < 64);
    }

    {   /* RAMP: 16 bars at 120 BPM = 32 s from 0 to TENS; halfway at 16 s; back down as slowly */
        double reach = -1, half;
        t = setup(0, preset_by_name(0, "DRONE SAW"));
        t->p[P_TENS] = 127;
        t->p[P_TRAMP] = 5;
        chord(t, CH, 4);
        run(ticks(16.0));
        half = (drn[0].tens >> 16) / 32766.0;
        for (i = 0; i < ticks(20.0); i++) {
            run(1);
            if (reach < 0 && drn[0].tens >> 16 >= 32766)
                reach = 16.0 + (i + 1) * TICK_S;
        }
        printf("drone: RAMP 16BAR at 120 BPM: %.1f %% after 16 s, at TENS after %.2f s\n", half * 100, reach);
        check("RAMP 16BAR: halfway after 8 bars (16 s, +-1 %)", fabs(half - 0.5) < 0.01);
        check("RAMP 16BAR: at TENS after 16 bars (32 s, +-0.1 s)", fabs(reach - 32.0) < 0.1);
        song.g[G_BPM] = 60;
        t->p[P_TRAMP] = 3;                               /* 4BAR at 60 BPM: 16 s back down */
        t->p[P_TENS] = 0;
        run(ticks(8.0));
        half = (drn[0].tens >> 16) / 32766.0;
        run(ticks(8.1));
        check("TENS down: as slowly (4BAR at 60 BPM: half after 8 s, 0 after 16 s)",
              fabs(half - 0.5) < 0.01 && drn[0].tens == 0);
        run(2);
        check("TENS back at 0 (EVOL 0): the drone code is off again", !drone_tick(t, 0) && !drn[0].on);
        t->p[P_TRAMP] = 0;
        t->p[P_TENS] = 64;
        run(ticks(0.5));
        check("RAMP OFF: TENS follows at once (smoothed, < 0.5 s)", drn[0].tens == 64 * 258 << 16);
    }

    {   /* TENS opens the sound: brighter (more high end), the walks deeper; and a drone builds from 0 */
        double hf[2], lv[2];
        uint32_t k, n;
        for (k = 0; k < 2u; k++) {
            t = setup(0, preset_by_name(0, "DRONE SAW"));
            t->p[P_TENS] = k ? 127 : 0;
            chord(t, CH, 4);
            run(ticks(6.0));
            hf[k] = lv[k] = 0;
            for (n = 0; n < ticks(4.0); n++) {
                run(1);
                for (i = 1; i < CTL; i++) {
                    hf[k] += fabs((double)blk_out[2 * i] - blk_out[2 * i - 2]);
                    lv[k] += fabs((double)blk_out[2 * i]);
                }
            }
        }
        printf("drone: DRONE SAW high end (|x[n] - x[n-1]| / |x|): TENS 0 %.3f, TENS 100 %.3f\n", hf[0] / lv[0],
               hf[1] / lv[1]);
        check("TENS 100: brighter than TENS 0 (filter, resonance, drive open)", hf[1] / lv[1] > 1.3 * hf[0] / lv[0]);
        t = setup(0, preset_by_name(0, "FERRUGEM"));
        chord(t, CH, 4);
        run(2);
        check("a preset with a RAMP starts at tension 0 and builds", drn[0].tens > 0 && drn[0].tens < 1 << 24);
    }

    {   /* DRIFT in the matrix (EVOL 0): the walk runs and moves the slot's target */
        int32_t d[3], lo = 0, hi = 0;
        int16_t keep[NEDIT];
        t = setup(0, preset_by_name(0, "DRONE SAW"));
        t->p[P_M3SRC] = MS_DRIFT;
        t->p[P_M3DST] = MD_FLT;
        t->p[P_M3AMT] = 63;
        chord(t, CH, 4);
        for (i = 0; i < ticks(60.0); i++) {
            run(1);
            mod_voice(t, ENGINES[0], &t->v[0], 0, 0, d, keep);
            lo = d[0] < lo ? d[0] : lo;
            hi = d[0] > hi ? d[0] : hi;
        }
        check("DRIFT -> FLT (EVOL 0): the matrix slot follows the walk", drn[0].on && hi - lo > 4000);
    }

    {   /* the presets: ARP RPT every 4 bars, HOLD, EVOL; they sound, stay in range and end */
        uint32_t k;
        for (k = 0; k < NDEMO; k++) {
            uint32_t pi = preset_by_name(DEMO[k].e, DEMO[k].name), over = 0;
            int32_t peak = 0;
            char what[96];
            if (pi == 0xFFu) {
                snprintf(what, sizeof what, "preset %s: in the engine", DEMO[k].name);
                check(what, 0);
                continue;
            }
            t = setup(DEMO[k].e, pi);
            chord(t, DEMO[k].n, 4);
            for (i = 0; i < ticks(40.0); i++) {
                run(1);
                for (c = 0; c < 2u * CTL; c++) {
                    int32_t a = abs(blk_out[c]);
                    peak = a > peak ? a : peak;
                    over += a > 32767;
                }
            }
            latch_off_req = 1;
            for (i = 0; i < ticks(20.0) && sounding(t); i++)
                run(1);
            snprintf(what, sizeof what, "preset %s: RPT 4BAR HOLD EVOL, sounds (peak %d), no clip, ends", DEMO[k].name, peak);
            check(what, t->p[P_AMODE] == 7 && t->p[P_ARATE] == 9 && t->p[P_AHOLD] && t->p[P_EVOL] > 0 && peak > 1500 &&
                            !over && !sounding(t));
        }
    }

    {   /* the cost: the control tick alone, and a whole drone with and without (instructions, host) */
        uint64_t i0, i1, base = 0, on = 0;
        uint32_t k, n = 200000;
        t = setup(0, preset_by_name(0, "FERRUGEM"));
        chord(t, CH, 4);
        run(ticks(2.0));
        i0 = instr_now();
        for (k = 0; k < n; k++) {
            if (drone_tick(t, 0))
                drone_restore(t);
        }
        i1 = instr_now();
        if (i0) {
            double per = (double)(i1 - i0) / n, voice;
            int32_t f;
            static volatile int32_t sink;
            i0 = instr_now();
            for (k = 0; k < n; k++)
                sink += voice_gain(t, k & 7u, &f) + f;
            voice = (double)(instr_now() - i0) / n;
            (void)sink;
            printf("drone: cost on the host: drone_tick %.0f instructions per block, drone_voice %.0f per voice "
                   "and block; 8 voices: %.1f instructions per sample\n", per, voice, (per + 8 * voice) / CTL);
            for (k = 0; k < 2u; k++) {
                t = setup(0, preset_by_name(0, "FERRUGEM"));
                if (!k)
                    t->p[P_EVOL] = t->p[P_TENS] = 0;
                {
                    static const uint8_t n8[8] = {38, 45, 50, 53, 57, 60, 62, 65};
                    chord(t, n8, 8);
                }
                run(ticks(1.0));
                i0 = instr_now();
                run(ticks(5.0));
                *(k ? &on : &base) = instr_now() - i0;
            }
            printf("drone: FERRUGEM, 8 voices: %.1f instructions per sample static, %.1f evolving (+%.1f %%)\n",
                   (double)base / (ticks(5.0) * CTL), (double)on / (ticks(5.0) * CTL), 100.0 * ((double)on / base - 1));
            check("cost: the drone adds < 2 % to an 8-voice superwave drone", (double)on / base < 1.02);
        } else {
            printf("drone: no instruction counter here: cost skipped\n");
        }
    }

    if (argc > 1)
        demos(argv[1]);
    printf(fails ? "drone: %d FAILED\n" : "drone: all ok\n", fails);
    return fails ? 1 : 0;
}
