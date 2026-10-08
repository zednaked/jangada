/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: CHORD mode (seq.c chord_notes, keyboard_block) on the host: the chords of the scale, the
 * white keys walking it, one key down = the whole chord, up = all of it released.
 * Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static int same(const uint8_t *c, uint32_t n, const int *want)
{
    uint32_t i;
    for (i = 0; i < n; i++)
        if (c[i] != want[i])
            return 0;
    return want[n] < 0;
}

static uint32_t gates(const track_t *t)
{
    uint32_t i, g = 0;
    for (i = 0; i < NVOICE; i++)
        g += t->v[i].active && t->v[i].gate;
    return g;
}

int main(void)
{
    static const struct { int scale, root, chord, note; int want[5]; const char *what; uint32_t mods; } C[] = {
        {1, 0, 1, 60, {60, 64, 67, -1}, "C MAJ TRIAD on C: C E G"},
        {1, 0, 1, 62, {62, 65, 69, -1}, "C MAJ TRIAD on D: D F A (minor)"},
        {1, 0, 2, 67, {67, 71, 74, 77, -1}, "C MAJ 7TH on G: G B D F"},
        {1, 0, 3, 60, {60, 64, 71, 74, -1}, "C MAJ 9TH on C: C E B D"},
        {1, 0, 4, 60, {60, 65, 67, -1}, "C MAJ SUS4 on C: C F G"},
        {1, 0, 5, 60, {60, 67, 72, -1}, "POWER on C: C G C"},
        {0, 9, 1, 69, {69, 72, 76, -1}, "CHR (minor) root A: A C E"},
        {2, 2, 1, 62, {62, 65, 69, -1}, "D MIN TRIAD on D: D F A"},
        /* CHORD+ (Jangada 0.7, after SLOOP 2.4): the black keys' modifiers */
        {1, 0, 1, 60, {60, 63, 67, -1}, "CHORD+ F#: C -> Cm", CM_MIN},
        {1, 0, 1, 62, {62, 66, 69, -1}, "CHORD+ F#: Dm -> D", CM_MIN},
        {1, 0, 1, 67, {67, 71, 74, 77, -1}, "CHORD+ G#: G -> G7 (from the scale)", CM_7},
        {1, 0, 1, 67, {67, 70, 74, 77, -1}, "CHORD+ F# + G#: Gm7", CM_MIN | CM_7},
        {1, 0, 1, 60, {60, 65, 67, -1}, "CHORD+ A#: Csus4", CM_SUS},
        {1, 0, 1, 60, {60, 64, 67, 74, -1}, "CHORD+ C#: Cadd9", CM_9},
        {1, 0, 1, 60, {60, 64, 71, 74, -1}, "CHORD+ G# + C#: C9 (no 5th)", CM_7 | CM_9},
        {1, 0, 1, 60, {72, 64, 67, -1}, "CHORD+ D#: C/E (the root on top)", CM_INV},
    };
    uint32_t i, fails = 0;
    uint8_t c[4];
    track_t *t = &trk[0];
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    for (i = 0; i < sizeof C / sizeof C[0]; i++) {
        uint32_t n;
        t->p[P_SCALE] = (int16_t)C[i].scale;
        t->p[P_ROOT] = (int16_t)C[i].root;
        t->p[P_CHORD] = (int16_t)C[i].chord;
        n = chord_notes(t, (uint32_t)C[i].note, C[i].mods, c);
        if (!same(c, n, C[i].want)) {
            uint32_t j;
            printf("%s: got", C[i].what);
            for (j = 0; j < n; j++)
                printf(" %u", c[j]);
            printf("\n");
            fails++;
        } else {
            printf("%-46s ok\n", C[i].what);
        }
    }

    /* the keys: CHORD walks the white keys from C4 (= the root), black keys are silent */
    t->p[P_SCALE] = 1;
    t->p[P_ROOT] = 0;
    t->p[P_CHORD] = 1;
    t->p[P_QUANT] = 0;
    assert(kb_map(t, 7) == 60u);                        /* C4 key -> C */
    assert(kb_map(t, 9) == 62u);                        /* D4 key -> D */
    assert(kb_map(t, 8) == KB_SILENT);                  /* C#4: silent */
    printf("%-46s ok\n", "CHORD: white keys walk the scale from C4");

    {   /* one key = three voices; its key-up releases all three */
        uint32_t b;
        static int32_t out[2 * CTL];
        t->p[P_VOICE] = V_POLY;
        fm1_in.notes = 1u << 9;                         /* D4: D F A */
        for (b = 0; b < 4u; b++)
            mix_block(out, CTL);
        if (gates(t) != 3u) {
            printf("CHORD key: %u voices held, want 3\n", gates(t));
            fails++;
        }
        fm1_in.notes = 0;
        for (b = 0; b < 4u; b++)
            mix_block(out, CTL);
        if (gates(t) != 0u) {
            printf("CHORD key up: %u voices still held\n", gates(t));
            fails++;
        }
        t->p[P_CHORD] = 0;                              /* OFF: one note again */
        fm1_in.notes = 1u << 9;
        for (b = 0; b < 4u; b++)
            mix_block(out, CTL);
        if (gates(t) != 1u) {
            printf("CHORD OFF: %u voices held, want 1\n", gates(t));
            fails++;
        }
        fm1_in.notes = 0;
        for (b = 0; b < 4u; b++)
            mix_block(out, CTL);
        if (!fails)
            printf("%-46s ok\n", "CHORD: key down = the chord, key up = all off");
    }
    {   /* CHORD+ on the keys: G#4 held, then D4: Dm7; F#4 pressed while held: D7; both up: Dm again */
        uint32_t b, f0 = fails;
        static int32_t out[2 * CTL];
        t->p[P_CHORD] = 1;
        t->p[P_VOICE] = V_POLY;
        fm1_in.notes = 1u << 15;                        /* G#4 */
        for (b = 0; b < 2u; b++)
            mix_block(out, CTL);
        if (gates(t)) { printf("CHORD+ modifier alone sounds\n"); fails++; }
        fm1_in.notes |= 1u << 9;                        /* + D4 */
        for (b = 0; b < 2u; b++)
            mix_block(out, CTL);
        if (gates(t) != 4u || kb_n[9] != 4u || kb_nt[9][3] != 72u) { printf("CHORD+ G# + D: %u voices\n", gates(t)); fails++; }
        fm1_in.notes |= 1u << 13;                       /* + F#4 while held: D F# A C */
        for (b = 0; b < 2u; b++)
            mix_block(out, CTL);
        if (gates(t) != 4u || kb_nt[9][1] != 66u) { printf("CHORD+ F# under the finger: %u voices, 3rd %u\n", gates(t), kb_nt[9][1]); fails++; }
        fm1_in.notes = 1u << 9;                         /* modifiers up: D F A */
        for (b = 0; b < 2u; b++)
            mix_block(out, CTL);
        if (gates(t) != 3u || kb_n[9] != 3u || kb_nt[9][1] != 65u) { printf("CHORD+ back: %u voices\n", gates(t)); fails++; }
        fm1_in.notes = 0;
        for (b = 0; b < 4u; b++)
            mix_block(out, CTL);
        if (gates(t)) { printf("CHORD+ up: %u held\n", gates(t)); fails++; }
        if (fails == f0)
            printf("%-46s ok\n", "CHORD+ keys: modifiers before / under the finger");
    }
    {   /* STRUM 20 ms: the notes one after the other (low to high); let go before the last: it never starts */
        uint32_t b, f0 = fails, per = 20u * FS / 1000u / CTL;
        static int32_t out[2 * CTL];
        t->p[P_STRUM] = 20;
        fm1_in.notes = 1u << 7;                         /* C4: C E G */
        mix_block(out, CTL);
        if (gates(t) != 1u) { printf("STRUM: %u at once, want 1\n", gates(t)); fails++; }
        for (b = 0; b < per + 1u; b++)
            mix_block(out, CTL);
        if (gates(t) != 2u) { printf("STRUM: %u after 20 ms, want 2\n", gates(t)); fails++; }
        fm1_in.notes = 0;                               /* up before the third */
        for (b = 0; b < 2u * per; b++)
            mix_block(out, CTL);
        if (gates(t)) { printf("STRUM: %u held after the key went up\n", gates(t)); fails++; }
        t->p[P_STRUM] = 0;
        if (fails == f0)
            printf("%-46s ok\n", "STRUM: 20 ms apart; up early: the rest never");
    }
    {   /* STRUM -20 on a chord step: high to low, the first note the top one, all three after 40 ms */
        uint32_t b, k2, top = 0, per = 20u * FS / 1000u / CTL;
        static int32_t out[2 * CTL];
        t->p[P_STRUM] = -20;
        t->p[P_SLEN] = 1; t->p[P_SGATE] = 127;
        t->step[0] = (step_t){{60, 64, 67, 0}, 3, ST_NOTE, 0, 100};
        transport_req = 1;
        mix_block(out, CTL);
        for (k2 = 0; k2 < NVOICE; k2++)
            if (t->v[k2].active && t->v[k2].gate)
                top = t->v[k2].note;
        b = gates(t);
        for (k2 = 0; k2 < 2u * per + 2u; k2++)
            mix_block(out, CTL);
        if (b != 1u || top != 67u || gates(t) != 3u) { printf("STRUM step: %u first (%u), %u later\n", b, top, gates(t)); fails++; }
        else
            printf("%-46s ok\n", "STRUM on a chord step: high to low");
        transport_req = 2;
        for (k2 = 0; k2 < 4u; k2++)
            mix_block(out, CTL);
        t->p[P_STRUM] = 0;
        t->step[0].n = 0;
    }
    {   /* VLEAD: C then A (A C E): voiced near the C chord's middle, not an octave up */
        uint8_t c1[4], c2[4];
        uint32_t n1, n2, k2;
        int32_t s1 = 0, s2 = 0;
        t->p[P_VLEAD] = 1;
        vl_mid[0] = 0;
        n1 = chord_notes(t, 60, 0, c1); chord_vlead(t, c1, n1);
        n2 = chord_notes(t, 69, 0, c2); chord_vlead(t, c2, n2);
        for (k2 = 0; k2 < n1; k2++) s1 += c1[k2];
        for (k2 = 0; k2 < n2; k2++) s2 += c2[k2];
        if (abs(s2 / (int32_t)n2 - s1 / (int32_t)n1) > 6) { printf("VLEAD: middles %d -> %d\n", s1 / (int32_t)n1, s2 / (int32_t)n2); fails++; }
        else
            printf("%-46s ok\n", "VLEAD: the next chord near the last");
        t->p[P_VLEAD] = 0;
    }
    if (fails)
        printf("CHORD: %u FAILED\n", fails);
    return fails != 0;
}
