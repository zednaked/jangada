/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the arp modes (UPDN, UDI, RPT) and the long divisions on the host.
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static track_t *arp_setup(uint32_t mode, uint32_t oct)
{
    static const uint8_t chord[] = {60, 64, 67};
    track_t *t = &trk[0];
    uint32_t i;
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    t->p[P_AMODE] = (int16_t)mode;
    t->p[P_AOCT] = (int16_t)oct;
    t->p[P_APROB] = 127;                    /* always fire */
    t->p[P_ASWING] = 0;
    for (i = 0; i < sizeof chord; i++)
        arp_add(t, chord[i]);
    return t;
}

/* n samples of the arp: a whole step (div_samples) moves its exact units (seq.c arp_tick counts units) */
static void atick(track_t *t, uint32_t n)
{
    uint32_t r = (uint32_t)t->p[P_ARATE] % 10u;
    arp_tick(t, n, n >= div_samples(r) ? BEAT_U / 24u * DIV_Q24[r] : n * (uint32_t)song.g[G_BPM]);
}

/* the note of each of the next n arp steps */
static void arp_run(track_t *t, uint32_t n, uint8_t *out)
{
    uint32_t period = div_samples((uint32_t)t->p[P_ARATE]), i;
    for (i = 0; i < n; i++) {
        atick(t, i ? period : 1u);
        out[i] = t->arp_note;
    }
}

static void expect(const char *what, const uint8_t *got, const uint8_t *want, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (got[i] != want[i]) {
            printf("%s: step %u is %u, expected %u\n", what, i, got[i], want[i]);
            exit(1);
        }
    printf("%-46s ok\n", what);
}

int main(void)
{
    uint8_t got[12];
    track_t *t;
    uint32_t d, beat;

    host_tracks_init();
    song.g[G_BPM] = 120;
    beat = (uint32_t)FS * 60u / 120u;
    {   /* the old six keep their exact lengths; the long ones are whole multiples */
        static const uint8_t den[6] = {1, 2, 4, 8, 3, 6};
        static const uint8_t mul[4] = {2, 4, 8, 16};
        for (d = 0; d < 6u; d++)
            assert(div_samples(d) == beat / den[d]);
        for (d = 0; d < 4u; d++)
            assert(div_samples(6u + d) == beat * mul[d]);
        printf("%-46s ok\n", "divisions: 1/4..16T unchanged, 1/2..4BAR");
    }

    {
        static const uint8_t want[] = {60, 64, 67, 64, 60, 64, 67, 64, 60};
        t = arp_setup(3, 1);
        arp_run(t, sizeof want, got);
        expect("UPDN: ends once (unchanged)", got, want, sizeof want);
    }
    {
        static const uint8_t want[] = {60, 64, 67, 67, 64, 60, 60, 64, 67};
        t = arp_setup(6, 1);
        arp_run(t, sizeof want, got);
        expect("UDI: ends twice", got, want, sizeof want);
    }
    {
        t = arp_setup(7, 2);
        atick(t, 1);
        assert(t->arp_note == 0 && t->arp_nch == 6);
        assert(t->arp_chord[0] == 60 && t->arp_chord[2] == 67 && t->arp_chord[3] == 72 && t->arp_chord[5] == 79);
        atick(t, div_samples((uint32_t)t->p[P_ARATE]));      /* next step: the chord again */
        assert(t->arp_nch == 6);
        t->nheld = 0;                                            /* keys up: the chord stops */
        atick(t, 1);
        assert(t->arp_nch == 0);
        printf("%-46s ok\n", "RPT: whole chord over OCT, retriggered, stops");
    }
    {
        t = arp_setup(7, 4);                                     /* 12 notes asked: NVOICE kept */
        atick(t, 1);
        assert(t->arp_nch == NVOICE);
        printf("%-46s ok\n", "RPT: at most NVOICE notes");
    }
    {
        static const uint8_t want[] = {60, 64, 67, 60};
        t = arp_setup(1, 1);
        t->p[P_ARATE] = 9;                                       /* 4BAR */
        arp_run(t, sizeof want, got);
        expect("UP at 4BAR", got, want, sizeof want);
    }
    {   /* ARP held (ui_input): a latched drone stops, held again its tails go too */
        static int32_t o[2 * CTL];
        uint32_t b, k, on = 0;
        memset(trk, 0, sizeof trk);
        memset(&song, 0, sizeof song);
        host_tracks_init();
        song.g[G_BPM] = 120;
        t = &trk[0];
        host_preset(t, 0, 14);                                   /* DRONE SAW: RPT, 4BAR, HOLD */
        input_on(t, 48, 100);
        input_on(t, 55, 100);
        input_off(t, 48);
        input_off(t, 55);
        for (b = 0; b < 200u; b++)
            mix_block(o, CTL);
        assert(t->nheld == 2 && !t->arp_phys);                   /* latched: it keeps playing */
        latch_off_req = 1;
        mix_block(o, CTL);
        assert(t->nheld == 0 && t->arp_nch == 0);
        for (k = 0; k < NVOICE; k++)
            on += t->v[k].active;
        assert(on > 0);                                          /* fading with its long release */
        hush_req = 1;
        for (b = 0; b < 3u; b++)
            mix_block(o, CTL);
        for (k = 0, on = 0; k < NVOICE; k++)
            on += t->v[k].active;
        assert(on == 0);
        printf("%-46s ok\n", "DRONE OFF (ARP held), SILENCE (held again)");
    }
    puts("arp: all ok");
    return 0;
}
