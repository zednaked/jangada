/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada (after Melodee's fm6_store.c): DX7 SysEx over USB-MIDI for the FM6 engine (usb.c dx_byte, fm6_sysex.c),
 * as USB-MIDI event packets from the host: a voice (VCED) into the selected FM6 track, a bank of 32 (VMEM) into
 * B1..B32 or B33..B64 in flash (Jangada 0.6: the bank of the FM6 track's PTCH, else the top bar asks "FM6 BANK n?
 * SAVE=YES" and the buttons answer), parameter changes live (a burst of them while the main loop is busy, none
 * lost; coalesced into one patch write), dump requests answered (byte for byte the DX7 format, checksums), the bytes of a long dump sent
 * as CIN 0xF single bytes (macOS), wrong checksums / other makers / no FM6 track ignored, the editor's frame (F0 7D
 * 46 4C) never taken nor blocked by a DX7 frame, and the bank not written while the song plays. And the FM6 bank
 * voices in PRESETS (ui.c, after SLOOP's DX7 voices there, majnikool): listed by name after the factory presets,
 * loaded, the jump by group (HOME held + PRESETS, KNOB 3 on the PRESETS page) with no HOME tap after it.
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

    /* a bank of 32 (VMEM) in, its bytes as CIN 0xF singles (macOS). The FM6 track's PTCH is F1: no bank is named,
     * so the top bar asks "FM6 BANK 1? SAVE=YES" (Jangada 0.6, two banks); SAVE writes B1..B32 */
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
    check("VMEM while the song plays: not written, nothing asked (STOP TO SAVE)",
          fm6_bank_used[0] == 1u << 6 && !strcmp(ui.msg, "STOP TO SAVE") && !fm6_ask.on && !dx_hold);
    song.playing = 0;
    erases = 0;
    host_send(m, 4104, 1);
    check("VMEM sent as CIN 0xF single bytes: collected whole", dx_ready && dx_n == 4102u);
    dx_service();
    check("VMEM with PTCH on a factory patch: the bank is asked for, held in dx_rx, nothing written",
          fm6_ask.on && dx_hold && !dx_ready && !erases && !strcmp(ui.msg, "FM6 BANK 1? SAVE=YES"));
    host_send(m, 163, 0);                            /* (a voice meanwhile: dropped, the bank kept) */
    n = ui.page;
    host_edges = 1u << panel.btn[B_SAVE];
    ui_input();
    check("SAVE answers yes; the press opens nothing (no SAVE page)", fm6_ask.answer == 1 && ui.page == n);
    dx_service();
    memset(fm6_bank_used, 0, sizeof fm6_bank_used);
    fm6_bank_boot();
    {
        int ok = fm6_bank_used[0] == 0xFFFFFFFFu && !fm6_bank_used[1] && erases == 2u && !fm6_ask.on && !dx_hold &&
                 !strcmp(ui.msg, "FM6 BANK 1 SAVED");
        for (i = 0; i < 32u && ok; i++)
            ok = !fm6_bank_get(i, pk) && pk[118] == 'A' + i && !memcmp(pk, FM6_FACTORY[i % 8u], 118) &&
                 fm6_bank_nm[i][0] == 'A' + (int)i;
        check("VMEM: B1..B32 in flash (two objects), each voice in its slot, the names kept", ok);
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

    /* Jangada 0.6: the FM6 bank 2 (B33..B64, flash 0x93000..0x96FFF). A VMEM with the FM6 track's PTCH on a bank 2
     * slot goes there at once; a dump request then sends bank 2 */
    set_engine_of(&trk[1], ENGI_FM6);
    song.sel = 1;
    ui.home = 0;
    ui.menu = 0;
    trk[1].p[P_E7] = FM6_NFACTORY + 40;              /* B41 */
    fm6_poll();
    m[0] = 0xF0; m[1] = 0x43; m[2] = 0x00; m[3] = 0x09; m[4] = 0x20; m[5] = 0x00;
    for (i = 0; i < 32u; i++) {
        memcpy(m + 6 + i * 128u, FM6_FACTORY[(i + 3u) % 8u], 128);
        m[6 + i * 128u + 118] = (uint8_t)('a' + i % 26u);   /* the name's first letter: a.. */
    }
    m[4102] = (uint8_t)chk(m + 6, 4096);
    m[4103] = 0xF7;
    erases = 0;
    host_send(m, 4104, 0);
    dx_service();
    fm6_poll();
    fm6_unpack(m + 6 + 8u * 128u, w);
    check("VMEM with PTCH on B41: into bank 2 at once (0x93000..0x96FFF), no question, B41 plays its voice",
          !fm6_ask.on && fm6_bank_used[0] == 0xFFFFFFFFu && fm6_bank_used[1] == 0xFFFFFFFFu && erases == 2u &&
              !strcmp(ui.msg, "FM6 BANK 2 SAVED") && fm6_bank_nm[40][0] == 'i' && fm6_bank_nm[8][0] == 'I' &&
              (!memcmp(nor + 0x93000u + 256u, "FM6B", 4) || !memcmp(nor + 0x94000u + 256u, "FM6B", 4)) &&
              !memcmp(fm6_patch[1], w, FP_SIZE));
    host_n = 0;
    {
        uint8_t q[5] = {0xF0, 0x43, 0x20, 0x09, 0xF7};
        host_send(q, 5, 0);
        dx_service();
        ota_idle();
    }
    check("bank dump request with PTCH on B41: bank 2 (B33..B64)",
          host_n == 4104u && host_in[6 + 118] == 'a' && !memcmp(host_in + 6 + 8u * 128u, m + 6 + 8u * 128u, 128) &&
              host_in[4102] == chk(host_in + 6, 4096));

    /* the question: OCT+ / OCT- pick the bank, any other button says no, 15 s drop it; a dump request waits */
    trk[1].p[P_E7] = 3;                              /* F4: no bank named */
    fm6_poll();
    erases = 0;
    {
        int8_t oct = song.octave;
        int ok;
        host_send(m, 4104, 0);
        dx_service();
        ok = fm6_ask.on && !strcmp(ui.msg, "FM6 BANK 1? SAVE=YES");
        host_edges = 1u << panel.btn[B_OCTUP];
        ui_input();
        dx_service();
        ok &= fm6_ask.on && fm6_ask.bank == 1u && !strcmp(ui.msg, "FM6 BANK 2? SAVE=YES") && song.octave == oct;
        host_n = 0;
        {
            uint8_t q[5] = {0xF0, 0x43, 0x20, 0x09, 0xF7};
            host_send(q, 5, 0);
            dx_service();
            ota_idle();
        }
        ok &= host_n == 0u && fm6_ask.on && dx_hold;
        host_edges = 1u << panel.btn[B_PLAY];
        ui_input();
        dx_service();
        check("asked: OCT+ picks bank 2 (octave kept), a dump request waits; PLAY says no and does not play",
              ok && !fm6_ask.on && !dx_hold && !erases && !strcmp(ui.msg, "FM6 BANK: CANCELLED") && !transport_req);
    }
    host_send(m, 4104, 0);
    dx_service();
    fm1_ms += DX_ASK_MS + 1u;
    dx_service();
    check("asked, no answer for 15 s: dropped", !fm6_ask.on && !dx_hold && !erases && !strcmp(ui.msg, "FM6 BANK: CANCELLED"));
    host_send(m, 4104, 0);
    dx_service();
    fm1_in.buttons = 1u << panel.btn[B_HOME];
    host_edges = 1u << panel.btn[B_HOME];
    ui_input();
    fm1_ms += 50u;
    fm1_in.buttons = 0;
    ui_input();
    dx_service();
    check("HOME says no; its release opens no HOME screen, no menu", !fm6_ask.on && !erases && !ui.home && !ui.menu);
    host_send(m, 4104, 0);
    dx_service();
    host_edges = 1u << panel.btn[B_OCTUP];
    ui_input();
    host_edges = 1u << panel.btn[B_OCTDN];
    ui_input();
    host_edges = 1u << panel.btn[B_SAVE];
    ui_input();
    dx_service();
    check("OCT+, OCT-, SAVE: bank 1 written (the a.. voices in B1..B32)",
          !fm6_ask.on && erases == 2u && !strcmp(ui.msg, "FM6 BANK 1 SAVED") && fm6_bank_nm[0][0] == 'a' && fm6_bank_nm[32][0] == 'a');

    /* the PRESETS list (ui.c, after SLOOP's DX7 voices in PRESETS): the factory presets, the used FM6 bank voices
     * by name, the user presets; loading one; the groups (HOME + PRESETS, KNOB 3) */
    {
        uint32_t nf = 0, total, cur, k, e, pg;
        char nm[13];
        for (e = 0; e < NENGINES; e++)
            nf += ENGINES[e]->npresets;
        fm1_ms += ED_BK_HOLD + 1u;
        fm6_bank_put(2, 0);                          /* B3 empty */
        trk[1].user = 0;
        trk[1].p[P_E7] = FM6_NFACTORY + 40;          /* B41 */
        fm6_poll();
        cur = preset_pos(&total);
        check("PRESETS: the factory presets, then the 63 used bank voices; the FM6 track on B41 sits on it",
              total == nf + 63u + up_count() && cur == nf + 39u);
        e = preset_at(nf + 2u, &k);
        check("... B3 empty: the third voice listed is B4, BANK 1", e == PRESET_FM6 && k == 3u && !strcmp(preset_kind(nf + 2u), "BANK 1"));
        e = preset_at(nf + 31u, &k);
        check("... then B33, BANK 2 (FM6 BANK 2 in the top bar)",
              e == PRESET_FM6 && k == 32u && !strcmp(preset_kind(nf + 31u), "BANK 2") &&
                  !strcmp(preset_kind_long(nf + 31u), "FM6 BANK 2") && !strcmp(preset_kind(0), ENGINES[0]->name));
        song.sel = 0;                                /* track 1: ANALOG */
        preset_go(nf + 31u);
        fm6_poll();
        fm6_unpack(m + 6, w);
        trk_short_name(0, nm);
        check("loading B33 from PRESETS: the track on FM6, PTCH B33, its patch, named by it",
              trk[0].eng_req == ENGI_FM6 && trk[0].p[P_E7] == FM6_NFACTORY + 32 && !memcmp(fm6_patch[0], w, FP_SIZE) &&
                  nm[0] == 'a' && !strcmp(nm, fm6_bank_nm[32]) && preset_pos(&total) == nf + 31u);
        check("group jumps: next / previous group, wrapping round",
              preset_group_jump(nf + 5u, 1) == nf + 31u && preset_group_jump(nf + 40u, -1) == nf &&
                  preset_group_jump(nf + 40u, 1) == 0u && preset_group_jump(0, -1) == nf + 31u &&
                  preset_group_jump(0, 1) == ENGINES[0]->npresets && preset_group_jump(3, -1) == nf + 31u);
        for (pg = 0; pg < NPAGES && PAGES[pg].graph != GR_BROWSE; pg++)
            ;
        ui.page = (uint8_t)pg;
        ui.home = 0;
        edit_param(2, 1);
        check("PRESETS page, KNOB 3: the next group (from bank 2: round to the first preset)",
              trk[0].eng_req == 0 && trk[0].preset == 0 && preset_pos(&total) == 0u);
        fm1_in.buttons = 1u << panel.btn[B_HOME];
        host_edges = 1u << panel.btn[B_HOME];
        ui_input();
        fm1_ms += 100u;
        host_enc[panel.enc[EN_PRESET]] = panel.dir[EN_PRESET];
        ui_input();
        cur = preset_pos(&total);
        check("HOME held + PRESETS: the next group, named in the top bar",
              cur == ENGINES[0]->npresets && trk[0].eng_req == 1 && !strcmp(ui.msg, ENGINES[1]->name));
        fm1_ms += 100u;
        host_enc[panel.enc[EN_PRESET]] = -panel.dir[EN_PRESET];
        ui_input();
        fm1_ms += 100u;
        fm1_in.buttons = 0;
        ui_input();
        check("... back again; let go: no HOME screen, no menu", preset_pos(&total) == 0u && !ui.home && !ui.menu);
        fm1_in.buttons = 1u << panel.btn[B_HOME];
        host_edges = 1u << panel.btn[B_HOME];
        ui_input();
        fm1_ms += 100u;
        fm1_in.buttons = 0;
        ui_input();
        check("(a HOME tap alone still opens HOME)", ui.home && !ui.menu);
        memset(fm6_bank_used, 0, sizeof fm6_bank_used);
        song.sel = 1;
        cur = preset_pos(&total);
        check("no bank voice: the list is as before; an FM6 track on an empty slot sits on its preset",
              total == nf + up_count() && cur < nf && preset_at(cur, &k) == ENGI_FM6);
        fm6_bank_boot();
    }

    printf(fails ? "DX7 TESTS FAILED (%d)\n" : "dx7 tests passed\n", fails);
    return fails ? 1 : 0;
}
