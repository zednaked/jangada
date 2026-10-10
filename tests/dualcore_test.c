/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada 1.0: the second core (fx.c mix_block, hal/fm1_cpu1.h) on the host. Four synth parts of the same
 * engine at once (every engine), and a few mixes of the heavy ones, with the INSERTs, the SLICER and the
 * drums; each rendered on one core and again with a thread as core 1 taking every split of the parts in turn
 * (c1_host.h), then with the firmware's own choice (mix_split). The outputs must be bit for bit the same, and
 * built with -fsanitize=thread (run_tests.sh), nothing the parts share may be touched by both at once.
 *   build/host/dualcore_test        (run_tests.sh builds it twice: plain, and with ThreadSanitizer) */
#include "c1_host.h"
#define main hostsim_main
#include "hostsim.c"
#undef main

#define SECS 3u

typedef struct { const char *name; uint8_t eng[NTRK], pre[NTRK], drums; } scen_t;

static int32_t peak[NTRK];                          /* each part's loudest (track_t.peak), for the report */

static uint64_t render(const scen_t *sc, int dual, const char *mask)
{
    static const uint8_t CH[3] = {0, 7, 12};
    uint64_t h = 0xCBF29CE484222325ull;
    uint32_t f, k, i, nparts = sc->drums ? NPART : NTRK;
    int32_t o[2 * CTL];
    host_tracks_init();
    song.t4 = (uint8_t)!sc->drums;
    song.g[G_T4] = (int16_t)!sc->drums;
    c1h_off = !dual;
    if (mask)
        setenv("C1_MASK", mask, 1);
    for (k = 0; k < nparts; k++) {
        track_t *t = &trk[k];
        host_preset(t, sc->eng[k], sc->pre[k]);
        t->p[P_ITYPE] = (int16_t)((k * 4u + sc->eng[k]) % IT_N);
        t->p[P_IMIX] = 90;
        if (k == 1u) {
            t->p[P_SLCR] = 1;                       /* the SLICER on one part */
            t->p[P_SLDEPTH] = 100;
        }
        t->p[P_REV] = (int16_t)(20 + 10 * k);
        t->p[P_DLY] = (int16_t)(10 * k);
        t->p[P_CHOR] = (int16_t)(30 - 5 * k);
    }
    fm6_poll();                                     /* (the main loop's): the FM6 parts' patches */
    if (sc->drums) {
        put_step(TDRUM, 0, 1, (const uint8_t[]){36}, ST_NOTE, 0);
        put_step(TDRUM, 4, 1, (const uint8_t[]){38}, ST_NOTE, 0);
        put_step(TDRUM, 2, 1, (const uint8_t[]){42}, ST_NOTE, 0);
        put_step(TDRUM, 6, 1, (const uint8_t[]){42}, ST_NOTE, 0);
        transport_req = 1;
    }
    for (f = 0; f < SECS * FS; f += CTL) {
        uint32_t ms = f / (FS / 1000u);
        for (k = 0; k < nparts; k++) {
            uint32_t on = 100u + 330u * k, off = on + 1400u, base = 48u + 5u * k;
            if (ms == on || (ms == on + 700u))
                for (i = 0; i < 2u; i++)
                    input_on(&trk[k], base + CH[i + (ms != on)], 90 + 10 * k);
            if (ms == off)
                for (i = 0; i < 3u; i++)
                    input_off(&trk[k], base + CH[i]);
        }
        mix_block(o, CTL);
        for (k = 0; k < NTRK; k++)
            if (trk[k].peak > peak[k])
                peak[k] = trk[k].peak;
        for (i = 0; i < 2u * CTL; i++) {
            uint32_t u = (uint32_t)o[i];
            for (k = 0; k < 4u; k++) {
                h ^= (u >> (8u * k)) & 0xFFu;
                h *= 0x100000001B3ull;
            }
        }
    }
    return h;
}

/* in a child of its own: every render starts from the state at boot */
static uint64_t render_fork(const scen_t *sc, int dual, const char *mask)
{
    int fd[2];
    uint64_t h = 0;
    pid_t pid;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (!pid) {
        h = render(sc, dual, mask);
        if (write(fd[1], &h, sizeof h) != sizeof h || write(fd[1], peak, sizeof peak) != sizeof peak)
            _exit(2);
        _exit(0);
    }
    close(fd[1]);
    if (read(fd[0], &h, sizeof h) != sizeof h || read(fd[0], peak, sizeof peak) != sizeof peak)
        h = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return h;
}

int main(void)
{
    static scen_t sc[256];
    static char names[256][40];
    uint32_t n = 0, e, i, k, g, fail = 0;
    static const char *const MASKS[] = {"rot", "auto", "15", "5", "10"};
    for (e = 0; e < NENGINES; e++)                      /* every preset: four parts at a time */
        for (g = 0; g * NTRK < ENGINES[e]->npresets; g++) {
            snprintf(names[n], sizeof names[n], "4 x %s %u..%u", ENGINES[e]->name, g * NTRK, g * NTRK + NTRK - 1u);
            sc[n].name = names[n];
            for (k = 0; k < NTRK; k++) {
                sc[n].eng[k] = (uint8_t)e;
                sc[n].pre[k] = (uint8_t)((g * NTRK + k) % ENGINES[e]->npresets);
            }
            sc[n++].drums = 0;
        }
    sc[n++] = (scen_t){"PHYS ROBO FM6 ANALOG", {ENGI_PHYS, ENGI_ROBO, ENGI_FM6, 0}, {1, 4, 7, 10}, 0};
    sc[n++] = (scen_t){"GRAIN SAMPLE NOISE VOICE", {ENGI_GRAIN, 4, ENGI_NOISE, 5}, {1, 4, 7, 10}, 0};
    sc[n++] = (scen_t){"TRIO WHEEL LOFI DIGITAL", {6, 7, 3, 1}, {1, 4, 7, 10}, 0};
    sc[n++] = (scen_t){"PHASE FM6 PHYS + drums", {2, ENGI_FM6, ENGI_PHYS, 0}, {1, 4, 7, 10}, 1};
    sc[n++] = (scen_t){"ANALOG ANALOG ROBO + drums", {0, 0, ENGI_ROBO, 0}, {1, 4, 7, 10}, 1};
    for (i = 0; i < n; i++) {
        uint64_t ref;
        if (getenv("SCENE") && !strstr(sc[i].name, getenv("SCENE")))
            continue;                                   /* SCENE=text: only the scenes named so */
        ref = render_fork(&sc[i], 0, 0);
        uint32_t m, bad = 0, k, quiet = 0;
        for (k = 0; k < (sc[i].drums ? NPART : NTRK); k++)
            quiet += peak[k] < 200;
        if (quiet) {                                    /* a silent part proves nothing */
            printf("dualcore: %-28s %u part(s) silent: peaks %d %d %d %d   FAIL\n", sc[i].name, quiet, peak[0], peak[1],
                   peak[2], peak[3]);
            fail++;
            continue;
        }
        for (m = 0; m < sizeof MASKS / sizeof MASKS[0]; m++) {
            uint64_t h = render_fork(&sc[i], 1, MASKS[m]);
            if (h != ref || !h) {
                printf("dualcore: %-28s split %-4s FAIL (%016llx, one core %016llx)\n", sc[i].name, MASKS[m],
                       (unsigned long long)h, (unsigned long long)ref);
                bad++;
            }
        }
        if (!bad)
            printf("dualcore: %-28s same on two cores, every split   ok\n", sc[i].name);
        fail += bad != 0;
    }
    printf("dualcore: %u of %u scenes differ\n", fail, n);
    return fail != 0;
}
