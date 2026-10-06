/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada (after Melodee's fm6_store.c): DX7 SysEx over USB-MIDI for the FM6 engine (usb.c dx_byte, fm6_sysex.c),
 * as USB-MIDI event packets from the host: a voice (VCED) into the selected FM6 track, a bank of 32 (VMEM) into
 * B1..B32 in flash, parameter changes live (a burst of them while the main loop is busy, none lost; coalesced into
 * one patch write), dump requests answered (byte for byte the DX7 format, checksums), the bytes of a long dump sent
 * as CIN 0xF single bytes (macOS), wrong checksums / other makers / no FM6 track ignored, the editor's frame (F0 7D
 * 46 4C) never taken nor blocked by a DX7 frame, and the bank not written while the song plays.
 * Built on tests/backup_test.c's host FM-1 (its main renamed). Build:
 *   cc -w -Ibuild/gen -Ifirmware/src tests/dx7_test.c -lm */
#define FELUCCA_OTA 1
#define FELUCCA_DX7 1
#define main backup_main
#include "backup_test.c"
#undef main
#include "fm6_sysex.c"

/* the host: what the device sent, back to bytes (as the host's USB-MIDI driver) */
static uint8_t host_in[8192];
static uint32_t host_n;
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void)
{
    while (so_r != so_w) {
        uint32_t pkt = sx_out_q[so_r++ % SXQ], cin = pkt & 15u, k;
        uint32_t nb = cin == 4u || cin == 7u ? 3u : cin == 6u ? 2u : cin == 5u ? 1u : 0u;
        for (k = 0; k < nb && host_n < sizeof host_in; k++)
            host_in[host_n++] = (uint8_t)(pkt >> (8u * (k + 1u)));
    }
    fm1_ms++;
}

/* a message from the host as USB-MIDI SysEx packets (CIN 4 / 5 / 6 / 7), or every byte as CIN 0xF */
static void host_send(const uint8_t *m, uint32_t n, int cinf)
{
    uint32_t i = 0;
    while (i < n) {
        uint32_t k = cinf ? 1u : n - i >= 3u ? 3u : n - i, cin, pkt, j;
        cin = cinf ? 0xFu : k == 3u && i + 3u < n ? 4u : k == 3u ? 7u : 4u + k;
        pkt = cin;
        for (j = 0; j < k; j++)
            pkt |= (uint32_t)m[i + j] << (8u * (j + 1u));
        midi_in_event(pkt);
        i += k;
    }
}

static uint32_t chk(const uint8_t *p, uint32_t n)
{
    uint32_t s = 0;
    while (n--)
        s += *p++;
    return (0u - s) & 127u;
}

static uint32_t vced(uint8_t *m, const uint8_t *v, uint32_t ch)   /* -> 163 bytes */
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
    memcpy(m, H, 6);
    m[2] = (uint8_t)ch;
    memcpy(m + 6, v, 155);
    m[161] = (uint8_t)chk(m + 6, 155);
    m[162] = 0xF7;
    return 163;
}

int main(void)
{
    static uint8_t m[4200], v[FP_SIZE + 1u], w[FP_SIZE + 1u], pk[FM6_PACKED];
    uint32_t i, n;
    power_on();
    fm6_bank_boot();
    usb.config = 1;
    set_engine_of(&trk[1], ENGI_FM6);
    song.sel = 1;
    fm6_poll();

    /* a voice (VCED) on channel 3: the selected FM6 track (2) takes it, PTCH as it was */
    fm6_unpack(FM6_FACTORY[4], v);
    v[FP_ALG] = 17;
    memcpy(v + FP_NAME, "DEXED TEST", 10);
    n = vced(m, v, 2);
    host_send(m, n, 0);
    check("VCED: collected apart from the editor's frame", dx_ready && !sx_ready && dx_n == 161u);
    dx_service();
    fm6_poll();
    check("VCED: the selected FM6 track plays it, PTCH kept", !memcmp(fm6_patch[1], v, FP_SIZE) && trk[1].p[P_E7] == 0 && !dx_ready);
    check("VCED: the top bar names it", !strcmp(ui.msg, "FM6 VOICE DEXED TEST"));
    m[100] ^= 1;                                     /* a wrong checksum: ignored */
    host_send(m, n, 0);
    dx_service();
    check("VCED with a wrong checksum: ignored", !memcmp(fm6_patch[1], v, FP_SIZE));

    /* parameter changes: a burst of 20 while the main loop is busy (a knob in Dexed), one patch write */
    {
        uint8_t gen = fm6_pgen[1];
        for (i = 0; i < 20u; i++) {
            uint8_t p[7] = {0xF0, 0x43, 0x10, 0x00, (uint8_t)(5u * 21u + 16u), (uint8_t)(50u + i), 0xF7};   /* OP1 OL */
            host_send(p, 7, 0);
        }
        {
            uint8_t p[7] = {0xF0, 0x43, 0x10, 0x01, (uint8_t)(134u - 128u), 30, 0xF7};   /* ALG (134) = 31 */
            host_send(p, 7, 0);
        }
        {
            uint8_t p[7] = {0xF0, 0x43, 0x10, 0x01, (uint8_t)(155u - 128u), 0x3F, 0xF7};   /* 155: operator switches */
            host_send(p, 7, 0);
        }
        check("parameter changes: 22 queued, none dropped", dx_qw - dx_qr == 22u && !dx_drops);
        dx_service();
        check("parameter changes: the last value of each, one patch write",
              fm6_patch[1][5u * 21u + 16u] == 69 && fm6_patch[1][134] == 30 && (uint8_t)(fm6_pgen[1] - gen) == 1u);
        for (i = 0; i < 40u; i++) {
            uint8_t p[7] = {0xF0, 0x43, 0x10, 0x00, 7, (uint8_t)i, 0xF7};
            host_send(p, 7, 0);
        }
        check("a burst beyond the queue: counted, not corrupted", dx_drops == 40u - DXQ && dx_qw - dx_qr == DXQ);
        dx_service();
        dx_drops = 0;
        {
            uint8_t p[7] = {0xF0, 0x43, 0x10, 0x00, 1, 120, 0xF7};   /* above the range: clamped */
            host_send(p, 7, 0);
            dx_service();
        }
        check("a value out of range: clamped (99)", fm6_patch[1][1] == 99);
    }

    /* dump requests: the voice, then the bank (empty slots: the init voice), on the request's channel */
    host_n = 0;
    {
        uint8_t q[5] = {0xF0, 0x43, 0x25, 0x00, 0xF7};
        host_send(q, 5, 0);
        dx_service();
        ota_idle();                                  /* (the host takes the rest) */
    }
    check("voice dump request: VCED of the track (channel 6, checksum)",
          host_n == 163u && host_in[0] == 0xF0 && host_in[2] == 5 && host_in[5] == 0x1B && !memcmp(host_in + 6, fm6_patch[1], 155) &&
              host_in[161] == chk(host_in + 6, 155) && host_in[162] == 0xF7);
    fm6_bank_put(6, FM6_FACTORY[2]);                 /* B7 */
    host_n = 0;
    {
        uint8_t q[5] = {0xF0, 0x43, 0x20, 0x09, 0xF7};
        host_send(q, 5, 0);
        dx_service();
        ota_idle();                                  /* (the host takes the rest) */
    }
    check("bank dump request: VMEM, B7 = F3, the others the init voice (4104 bytes, checksum)",
          host_n == 4104u && host_in[3] == 9 && host_in[4] == 0x20 && !memcmp(host_in + 6 + 6u * 128u, FM6_FACTORY[2], 128) &&
              !memcmp(host_in + 6, FM6_INIT, 128) && !memcmp(host_in + 6 + 31u * 128u, FM6_INIT, 128) &&
              host_in[4102] == chk(host_in + 6, 4096) && host_in[4103] == 0xF7 && !dx_hold);

    /* a bank of 32 (VMEM) in, its bytes as CIN 0xF singles (macOS): B1..B32, in flash */
    m[0] = 0xF0; m[1] = 0x43; m[2] = 0x00; m[3] = 0x09; m[4] = 0x20; m[5] = 0x00;
    for (i = 0; i < 32u; i++) {
        memcpy(m + 6 + i * 128u, FM6_FACTORY[i % 8u], 128);
        m[6 + i * 128u + 118] = (uint8_t)('A' + i);  /* the name's first letter: A.. */
    }
    m[4102] = (uint8_t)chk(m + 6, 4096);
    m[4103] = 0xF7;
    song.playing = 1;
    host_send(m, 4104, 1);
    dx_service();
    check("VMEM while the song plays: not written (STOP TO SAVE)", fm6_bank_used == 1u << 6 && !strcmp(ui.msg, "STOP TO SAVE"));
    song.playing = 0;
    erases = 0;
    host_send(m, 4104, 1);
    check("VMEM sent as CIN 0xF single bytes: collected whole", dx_ready && dx_n == 4102u);
    dx_service();
    fm6_bank_used = 0;
    fm6_bank_boot();
    {
        int ok = fm6_bank_used == 0xFFFFFFFFu && erases == 2u;
        for (i = 0; i < 32u && ok; i++)
            ok = !fm6_bank_get(i, pk) && pk[118] == 'A' + i && !memcmp(pk, FM6_FACTORY[i % 8u], 118);
        check("VMEM: B1..B32 in flash (two objects), each voice in its slot", ok);
    }
    trk[1].p[P_E7] = FM6_NFACTORY + 9;               /* B10 */
    fm6_poll();
    fm6_unpack(m + 6 + 9u * 128u, w);
    check("PTCH B10 then plays the bank's tenth voice", !memcmp(fm6_patch[1], w, FP_SIZE));

    /* the editor's frame and a DX7 frame side by side; other makers; no FM6 track */
    {
        uint8_t ed[6] = {0xF0, 0x7D, 0x46, 0x4C, 25, 0xF7}, other[8] = {0xF0, 0x41, 0x10, 0x42, 0x12, 0x00, 0x00, 0xF7};
        n = vced(m, v, 0);
        host_send(m, n, 0);                          /* a voice waits in dx_rx ... */
        host_send(ed, 6, 0);                         /* ... the editor's PING still arrives */
        check("an editor frame next to a waiting DX7 voice: both taken", sx_ready && sx_frame_len == 4u && dx_ready);
        ota_frame_done();
        dx_service();
        host_send(other, 8, 0);
        check("another maker's SysEx: not a DX7 frame", !dx_ready && dx_qw == dx_qr);
        ota_frame_done();
        set_engine_of(&trk[1], 0);
        n = vced(m, w, 0);
        host_send(m, n, 0);
        dx_service();
        check("a voice with no FM6 track: nothing changes, the top bar says so", !strcmp(ui.msg, "FM6: NO FM6 TRACK"));
        set_engine_of(&trk[2], ENGI_FM6);            /* not selected, on channel 3 */
        host_send(m, n, 0);
        dx_service();
        check("... with track 3 on FM6 (not selected): it takes it", !memcmp(fm6_patch[2], w, FP_SIZE));
    }

    printf(fails ? "DX7 TESTS FAILED (%d)\n" : "dx7 tests passed\n", fails);
    return fails ? 1 : 0;
}
