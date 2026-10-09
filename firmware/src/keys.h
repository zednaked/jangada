/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: stable keys of the per-track parameters. Saved data (projects, user presets) store
 * the key of each value, not its position in the P_* enum, so parameters can be added or moved
 * without breaking what is stored: unknown keys are skipped, missing ones take the default.
 *
 * A key never changes and is never reused. Keys 0..56 are Felucca's format-3 positions (so a
 * FUN3 project is the key list 0..56); new parameters take the next free key.
 * tests/keys_test.c holds the list as it was released and fails on any change to it. */
#ifndef KEYS_H
#define KEYS_H
#define KEY_NONE 0xFFu
#define KEY_MAX 128u                       /* keys are < KEY_MAX */
static const uint8_t P_KEY[P_COUNT] = {
    [P_LEVEL] = 0, [P_ATK] = 1, [P_DEC] = 2, [P_SUS] = 3, [P_REL] = 4,
    [P_ED_FLT] = 5, [P_ED_PIT] = 6, [P_ED_SHP] = 7, [P_ED_FX] = 8,
    [P_LRATE] = 9, [P_LWAVE] = 10, [P_LPHASE] = 11, [P_LFADE] = 12,
    [P_LD_PIT] = 13, [P_LD_FLT] = 14, [P_LD_SHP] = 15, [P_LD_AMP] = 16,
    [P_AMODE] = 17, [P_ARATE] = 18, [P_AOCT] = 19, [P_AGATE] = 20,
    [P_ASWING] = 21, [P_APROB] = 22, [P_AHOLD] = 23, [P_AORDER] = 24,
    [P_ROOT] = 25, [P_SCALE] = 26, [P_QUANT] = 27, [P_TRANS] = 28,
    [P_SLEN] = 29, [P_SDIV] = 30, [P_SSWING] = 31, [P_SGATE] = 32,
    [P_DIST] = 33, [P_CHOR] = 34, [P_DLY] = 35, [P_REV] = 36,
    [P_VOICE] = 37, [P_GLIDE] = 38, [P_PAN] = 39, [P_MUTE] = 40,
    [P_GLMODE] = 41, [P_PRIO] = 42, [P_ALLOC] = 43, [P_DETUNE] = 44,
    [P_SLCR] = 45, [P_SLPAT] = 46, [P_SLRATE] = 47, [P_SLDEPTH] = 48,
    [P_E0] = 49, [P_E1] = 50, [P_E2] = 51, [P_E3] = 52, [P_E4] = 53, [P_E5] = 54, [P_E6] = 55, [P_E7] = 56,
    /* Jangada 0.1 */
    [P_E8] = 57, [P_E9] = 58, [P_E10] = 59, [P_E11] = 60, [P_E12] = 61, [P_E13] = 62, [P_E14] = 63, [P_E15] = 64,
    [P_M1SRC] = 65, [P_M1DST] = 66, [P_M1AMT] = 67, [P_M2SRC] = 68, [P_M2DST] = 69, [P_M2AMT] = 70,
    [P_M3SRC] = 71, [P_M3DST] = 72, [P_M3AMT] = 73, [P_M4SRC] = 74, [P_M4DST] = 75, [P_M4AMT] = 76,
    /* Jangada 0.2 */
    [P_CHORD] = 77,
    /* Jangada GRIT */
    [P_DTYPE] = 78, [P_DRING] = 79,
    /* Jangada DRONES (keys from 100: 80.. stay free for parameters made at the same time elsewhere) */
    [P_EVOL] = 100, [P_TENS] = 101, [P_TRAMP] = 102,
    [P_TFLT] = 103, [P_STRUM] = 104, [P_VLEAD] = 105,
    /* Jangada 0.9 */
    [P_ITYPE] = 106, [P_IA] = 107, [P_IB] = 108, [P_IC] = 109, [P_IMIX] = 110,
};

/* the P_* index of a key, P_COUNT when this firmware does not know it */
static uint32_t key_param(uint32_t key)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        if (P_KEY[i] == key)
            return i;
    return P_COUNT;
}

/* values stored as (keys[i], vals[i]) -> out[] in P_* order; missing -> fill */
static void key_map(const uint8_t *keys, const int16_t *vals, uint32_t n, int16_t *out, int16_t fill)
{
    uint32_t i, j;
    for (i = 0; i < P_COUNT; i++)
        out[i] = fill;
    for (i = 0; i < n; i++)
        if ((j = key_param(keys[i])) < P_COUNT)
            out[j] = vals[i];
}
#endif
