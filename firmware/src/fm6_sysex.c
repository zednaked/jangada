/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Jangada: after Melodee's fm6_store.c (Kerem Kilic / Ellic Studio, GPL-3.0) */
/* DX7 SysEx over USB-MIDI for the FM6 engine, so Dexed (or any DX7 editor / librarian) edits an FM6 track live and
 * keeps the bank. usb.c (FELUCCA_DX7) collects the frames (dx_byte); the main loop takes them here (dx_service).
 * Accepted on any channel n:
 *   F0 43 0n 00 01 1B <155 bytes> <checksum> F7    a voice (VCED) -> the FM6 track's own patch (as the editor's FM6_PUT:
 *                                                  heard at once, PTCH as it is, the project keeps it)
 *   F0 43 0n 09 20 00 <4096 bytes> <checksum> F7   32 voices (VMEM) -> the bank B1..B32, saved (not while the song plays)
 *   F0 43 1n gg pp dd F7                           a voice parameter pp + 128 * (gg & 3) = dd (0..154) -> the track's
 *                                                  patch, live. 155 (the operator switches) and the function
 *                                                  parameters (gg 8) have nothing to act on here: ignored
 *   F0 43 2n 00 F7 / F0 43 2n 09 F7                dump requests: the track's voice / the bank (an empty slot: the init
 *                                                  voice), answered on channel n
 * The FM6 track: the selected track when it plays FM6, else track n + 1 when it does, else the first FM6 track; with
 * none, a voice or a parameter does nothing (the top bar says so). Checksum: the data's sum + it = 0 (7 bits); a
 * frame with a wrong one is ignored. Included by felucca.c after editor.c (FELUCCA_DX7, FELUCCA_FLASH). */
#define DX_VCED 161u                             /* 43 0n 00 01 1B, 155, checksum (between F0 and F7) */
#define DX_VMEM 4102u                            /* 43 0n 09 20 00, 4096, checksum */

static int dx_is_fm6(uint32_t k) { return k < NTRK && trk_synth(k) && trk[k].eng_req == ENGI_FM6; }

static int dx_track(uint32_t ch)                 /* the FM6 track a message on channel ch is for, -1 = none */
{
    uint32_t k;
    if (dx_is_fm6(song.sel))
        return (int)song.sel;
    if (dx_is_fm6(ch))
        return (int)ch;
    for (k = 0; k < NTRK; k++)
        if (dx_is_fm6(k))
            return (int)k;
    return -1;
}

static uint32_t dx_chk(const uint8_t *p, uint32_t n)   /* DX7 checksum: data + it = 0 mod 128 */
{
    uint32_t s = 0;
    while (n--)
        s += *p++;
    return (0u - s) & 0x7Fu;
}

/* the track's patch now (main loop): the ISR takes it at its next block; PTCH stays as it is (fm6_poll keeps it) */
static void dx_put(uint32_t tr, const uint8_t *v)
{
    fm6_set_patch(tr, v);
    fm6_slot[tr] = (uint8_t)trk[tr].p[P_E7];
    ui.force = 1;
}

static void dx_take_long(void);

/* ------------------------------------------------------------- out --- */
static void dx_send_voice(uint32_t tr, uint32_t ch)    /* VCED: the track's patch */
{
    uint8_t m[163];
    m[0] = 0xF0;
    m[1] = 0x43;
    m[2] = (uint8_t)(ch & 15u);
    m[3] = 0x00;
    m[4] = 0x01;
    m[5] = 0x1B;
    memcpy(m + 6, fm6_patch[tr % NTRK], FP_SIZE);
    m[161] = (uint8_t)dx_chk(m + 6, FP_SIZE);
    m[162] = 0xF7;
    ota_wire_send(m, sizeof m);
}

static void dx_send_bank(uint32_t ch)            /* VMEM: B1..B32, built in dx_rx (dx_hold keeps the ISR out) */
{
    dx_take_long();                              /* a frame waiting in dx_rx goes first */
    dx_hold = 1;
    RING_PUBLISH();
    dx_rx[0] = 0xF0;
    dx_rx[1] = 0x43;
    dx_rx[2] = (uint8_t)(ch & 15u);
    dx_rx[3] = 0x09;
    dx_rx[4] = 0x20;
    dx_rx[5] = 0x00;
    fm6_bank_get_all(dx_rx + 6);                 /* each half read once; an empty slot the init voice */
    dx_rx[4102] = (uint8_t)dx_chk(dx_rx + 6, 4096);
    dx_rx[4103] = 0xF7;
    ota_wire_send(dx_rx, 4104);                 /* ~90 ms: 1368 packets, 16 per USB frame */
    RING_PUBLISH();
    dx_hold = 0;
}

/* ------------------------------------------------------------- in --- */
static void dx_long_frame(const uint8_t *b, uint32_t n)   /* the bytes between F0 and F7 */
{
    uint32_t st = b[1] & 0xF0u;
    if (n == DX_VCED && st == 0x00u && b[2] == 0x00 && b[3] == 0x01 && b[4] == 0x1B && dx_chk(b + 5, FP_SIZE) == b[160]) {
        int tr = dx_track(b[1] & 15u);
        uint8_t v[FP_SIZE + 1u];
        char nm[11];
        if (tr < 0) {
            ui_message("FM6: NO FM6 TRACK");
            return;
        }
        memcpy(v, b + 5, FP_SIZE);
        dx_put((uint32_t)tr, v);
        fm6_name(nm, fm6_patch[tr]);
        ui_say("FM6 VOICE ", nm);
    } else if (n == DX_VMEM && st == 0x00u && b[2] == 0x09 && b[3] == 0x20 && b[4] == 0x00 &&
               dx_chk(b + 5, 4096) == b[4101]) {
        uint32_t i;
        for (i = 0; i < 4096u; i++)
            if (b[5 + i] > 127u)
                return;
        if (fm6_bank_put_all(b + 5))             /* (STOP TO SAVE while the song plays; nothing changed) */
            return;
        ui_message("FM6 BANK SAVED B1-B32");
        ui.force = 1;
    }
}

static void dx_take_long(void)                   /* the long frame waiting in dx_rx, if any */
{
    if (!dx_ready)
        return;
    RING_PUBLISH();                              /* read the frame only after the flag */
    dx_long_frame(dx_rx, dx_n);
    RING_PUBLISH();
    dx_ready = 0;
}

/* main loop: the queued short messages (parameters coalesced into one patch write per track), then a long frame */
static void dx_service(void)
{
    uint8_t v[FP_SIZE + 1u];
    int tr = -1;
    while (dx_qr != dx_qw) {
        uint32_t m, st, ch;
        RING_PUBLISH();                          /* the slot only after the index */
        m = dx_q[dx_qr % DXQ];
        RING_PUBLISH();
        dx_qr++;
        st = m & 0xF0u;
        ch = m & 15u;
        if (st == 0x10u && !((m >> 8) & 0x7Cu)) {                 /* a voice parameter (group 0) */
            uint32_t k = ((m >> 8) & 3u) << 7 | ((m >> 16) & 0x7Fu);
            int t = dx_track(ch);
            if (t < 0 || k >= FP_SIZE)
                continue;                        /* (155: the operator switches; nothing here) */
            if (t != tr) {
                if (tr >= 0)
                    dx_put((uint32_t)tr, v);
                tr = t;
                memcpy(v, fm6_patch[tr], FP_SIZE);
            }
            v[k] = (uint8_t)((m >> 24) & 0x7Fu);
        } else if (st == 0x20u) {                                  /* a dump request */
            uint32_t f = (m >> 8) & 0x7Fu;
            int t = dx_track(ch);
            if (tr >= 0) {                       /* (what came before it first) */
                dx_put((uint32_t)tr, v);
                tr = -1;
            }
            if (f == 0x00u && t >= 0)
                dx_send_voice((uint32_t)t, ch);
            else if (f == 0x09u)
                dx_send_bank(ch);
        }
    }
    if (tr >= 0)
        dx_put((uint32_t)tr, v);
    dx_take_long();
}
