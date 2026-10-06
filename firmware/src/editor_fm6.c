/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Jangada: ported from Felucca 1.0 (editor_fm6.c), with the bank of 32 (fm6_bank.c) and rc 3 for a song playing */
/* Editor protocol: the FM6 patches (EDITOR_PROTOCOL.md "FM6 patches", cmds 68..71, as Felucca numbers them; INFO
 * advertises them with 46 01 nfactory nbank after the protocol version). A patch travels as the 128-byte packed
 * record (every byte 7-bit: no pack7). Targets: 0 a track's own patch (index 0..NTRK-1), 1 a bank slot
 * (0..FM6_BANK_N-1, flash), 2 a factory patch (0..FM6_NFACTORY-1, read only). Included by editor.c. */
enum { ED_FM6_GET = 68, ED_FM6_PUT, ED_FM6_LIST, ED_FM6_ERASE };
enum { ED_FM6_TRACK, ED_FM6_BANK, ED_FM6_FACTORY };

static void ed_fm6_name(const uint8_t *pk)       /* the record's name, trailing spaces off */
{
    char s[11];
    uint32_t i, n = 0;
    for (i = 0; i < 10u; i++) {
        s[i] = (char)(pk[118 + i] >= 32u && pk[118 + i] <= 126u ? pk[118 + i] : ' ');
        if (s[i] != ' ')
            n = i + 1u;
    }
    s[n] = 0;
    ed_str(s, 10);
}

/* a bank write: 0 ok, 1 bad slot, 2 flash, 3 the song plays (a flash erase stops the audio for a moment) */
static uint32_t ed_fm6_write(uint32_t k, const uint8_t *pk)
{
    if (k >= FM6_BANK_N)
        return 1;
    if (song.playing || transport_req)
        return 3;
    return fm6_bank_put(k, pk) ? 2u : 0u;
}

static int ed_fm6_handle(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    uint8_t pk[FM6_PACKED];
    uint32_t i, rc;
    switch (cmd) {
    case ED_FM6_GET:                                       /* target, index -> target, index, rc, [128 bytes] */
        rc = n != 2u || a[0] > ED_FM6_FACTORY ? 1u : 0u;
        if (!rc) {
            if (a[0] == ED_FM6_TRACK && a[1] < NTRK) {
                memset(pk, 0, sizeof pk);
                fm6_pack(fm6_patch[a[1]], pk);
            } else if (a[0] == ED_FM6_BANK && a[1] < FM6_BANK_N)
                rc = fm6_bank_get(a[1], pk) ? 2u : 0u;
            else if (a[0] == ED_FM6_FACTORY && a[1] < FM6_NFACTORY)
                memcpy(pk, FM6_FACTORY[a[1]], FM6_PACKED);
            else
                rc = 1;
        }
        ed_b(n ? a[0] : 127u);
        ed_b(n > 1u ? a[1] : 127u);
        ed_b(rc);
        for (i = 0; !rc && i < FM6_PACKED; i++)
            ed_b(pk[i]);
        return 1;
    case ED_FM6_PUT:                                       /* target, index, 128 bytes -> target, index, rc */
        rc = n != 2u + FM6_PACKED ? 1u : 0u;
        if (!rc && a[0] == ED_FM6_TRACK && a[1] < NTRK) {
            uint8_t v[FP_SIZE + 1u];
            fm6_unpack(a + 2, v);                          /* (every value into its range) */
            fm6_set_patch(a[1], v);
            fm6_slot[a[1]] = (uint8_t)trk[a[1]].p[P_E7];   /* the track's patch now, PTCH as it is: fm6_poll keeps it */
            ui.force = 1;
        } else if (!rc && a[0] == ED_FM6_BANK) {
            rc = ed_fm6_write(a[1], a + 2);
        } else {
            rc = 1;
        }
        ed_b(n ? a[0] : 127u);
        ed_b(n > 1u ? a[1] : 127u);
        ed_b(rc);
        return 1;
    case ED_FM6_LIST: {                                    /* -> factory count, bank count, then per slot: used, name */
        uint32_t h;
        if (n)
            return 0;
        ed_b(FM6_NFACTORY);
        ed_b(FM6_BANK_N);
        for (i = 0; i < FM6_NFACTORY; i++) {
            ed_b(1);
            ed_fm6_name(FM6_FACTORY[i]);
        }
        for (h = 0; h < 2u; h++) {                         /* each half read once from flash */
            const fm6_half_t *b = fm6_half_view(h);
            for (i = 0; i < FM6_HALF; i++) {
                uint32_t used = b && ((b->used >> i) & 1u);
                ed_b(used);
                if (used)
                    ed_fm6_name(b->v[i]);
                else
                    ed_b(0);
            }
        }
        return 1;
    }
    case ED_FM6_ERASE:                                     /* bank index -> index, rc */
        rc = n != 1u ? 1u : ed_fm6_write(a[0], 0);
        ed_b(n ? a[0] : 127u);
        ed_b(rc);
        return 1;
    }
    return 0;
}
