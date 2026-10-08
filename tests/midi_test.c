/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: MIDI beyond notes (seq.c midi_cc, midi_clock_in / _out) on the host: pitch bend, the
 * sustain pedal, ALL NOTES OFF, the controllers as modulation sources, the clock in (tempo, START /
 * STOP) and out (24 a beat, START / STOP). The clock in pulse by pulse (after SLOOP 2.3): the steps and
 * the beat clock on the master's pulses for 256 beats at a tempo that is no whole number of samples, a
 * tempo change, CONTINUE, the timeout back to the internal clock, the TRS jack as the source, no clock
 * out while following. Build with the same generated headers and flags as hostsim.c. */
#include <assert.h>
#define main hostsim_main
#include "hostsim.c"
#undef main

static int32_t out[2 * CTL];
static uint32_t fails;

static void ok(int c, const char *what)
{
    printf("%-50s %s\n", what, c ? "ok" : "FAIL");
    fails += !c;
}

static void midi(uint32_t st, uint32_t d1, uint32_t d2)          /* as usb.c queues it */
{
    uint32_t cin = st >= 0xF8u ? 0xFu : st >> 4;
    midi_in_q[mi_w % MQ] = cin | st << 8 | d1 << 16 | d2 << 24;
    mi_w++;
}

static void run(uint32_t blocks)
{
    while (blocks--)
        mix_block(out, CTL);
}

/* a master clock: pulses at exact (fractional) times, queued before the block they fall in;
 * cable 0 USB, 1 the TRS jack. Returns the pulses sent */
static double mc_t, mc_next;                     /* samples */
static uint32_t mc_sent, mc_base, mc_checked, mc_off;   /* mc_off: blocks where a 1/16 step was not on its pulse */
static const track_t *mc_trk;                    /* (checked on every pulse that starts a 1/16 step) */
static void master(double bpm, uint32_t pulses, uint32_t cable)
{
    double per = 60.0 * FS / bpm / 24.0;
    uint32_t end = mc_sent + pulses;
    while (mc_sent < end) {
        uint32_t got = 0;
        while (mc_next <= mc_t + CTL && mc_sent < end) {
            midi_in_q[mi_w % MQ] = 0x0Fu | cable << 4 | 0xF8u << 8;
            mi_w++;
            mc_sent++;
            mc_next += per;
            got = 1;
        }
        run(1);
        mc_t += CTL;
        if (got && mc_trk && mc_sent > mc_base && (mc_sent - mc_base - 1u) % 6u == 0u) {
            mc_checked++;
            mc_off += mc_trk->seq_idx != ((mc_sent - mc_base - 1u) / 6u) % 16u;
        }
    }
}
static void master_rt(uint32_t b, uint32_t cable)
{
    midi_in_q[mi_w % MQ] = 0x0Fu | cable << 4 | b << 8;
    mi_w++;
}

static uint32_t gates(const track_t *t)
{
    uint32_t i, g = 0;
    for (i = 0; i < NVOICE; i++)
        g += t->v[i].active && t->v[i].gate;
    return g;
}

static void setup(void)
{
    memset(trk, 0, sizeof trk);
    memset(&song, 0, sizeof song);
    host_tracks_init();
    song.g[G_BPM] = 120;
    song.master_q12 = 4096;
    mi_r = mi_w = 0;
}

int main(void)
{
    track_t *t = &trk[0];
    setup();

    midi(0xE0, 0x7F, 0x7F);                                   /* bend up, full */
    run(1);
    ok(t->bend16 == 31, "pitch bend up: +2 semitones (31/16)");
    midi(0xE0, 0x00, 0x40);
    run(1);
    ok(t->bend16 == 0, "pitch bend centre: 0");
    midi(0xE1, 0x00, 0x00);
    run(1);
    ok(trk[1].bend16 == -32 && t->bend16 == 0, "bend on channel 2 moves track 2 only");

    midi(0x90, 60, 100);
    midi(0xB0, 64, 127);                                      /* pedal down */
    midi(0x80, 60, 0);
    run(4);
    ok(gates(t) == 1u, "sustain: a released note keeps sounding");
    midi(0xB0, 64, 0);                                        /* pedal up */
    run(4);
    ok(gates(t) == 0u, "sustain up: the held note is let go");

    midi(0x90, 60, 100);
    midi(0x90, 64, 100);
    run(4);
    midi(0xB0, 123, 0);
    run(8);
    ok(gates(t) == 0u, "CC123 ALL NOTES OFF: the track is quiet");

    midi(0xB0, 1, 100);
    midi(0xD0, 50, 0);
    midi(0xB0, 11, 90);
    run(1);
    ok(mod_src(t, &t->v[0], MS_MODW, 0, 0) == 100 * 258 && mod_src(t, &t->v[0], MS_AT, 0, 0) == 50 * 258 &&
           mod_src(t, &t->v[0], MS_EXPR, 0, 0) == 90 * 258, "MODW / AT / EXPR as modulation sources");
    midi(0xB0, 121, 0);
    run(1);
    ok(!t->mw && !t->at && !t->ex && !t->bend16, "CC121 RESET ALL CONTROLLERS");

    {   /* clock in: 24 clocks a beat at 100 BPM -> BPM 100; START / STOP */
        uint32_t k, per = 60u * FS / 100u / 24u / CTL;       /* blocks between two clocks */
        setup();
        song.g[G_CLOCK] = 1;
        midi(0xFA, 0, 0);
        run(2);                                               /* (the transport moves on the next block) */
        ok(song.playing, "clock in: START plays");
        for (k = 0; k < 24u * 4u; k++) {
            midi(0xF8, 0, 0);
            run(per);
        }
        ok(song.g[G_BPM] >= 99 && song.g[G_BPM] <= 101, "clock in: 24 a beat at 100 BPM -> BPM 100");
        midi(0xFC, 0, 0);
        run(2);
        ok(!song.playing, "clock in: STOP stops");
        song.g[G_CLOCK] = 0;
        song.g[G_BPM] = 120;
        midi(0xF8, 0, 0);
        midi(0xFA, 0, 0);
        run(2);
        ok(!song.playing && song.g[G_BPM] == 120, "clock INT: the host's clock is ignored");
    }

    {   /* clock out: START, 24 a beat, STOP; none while following */
        uint32_t f8 = 0, fa = 0, fc = 0, b;
        setup();
        usb.config = 1;
        song.g[G_SYNC] = 1;
        mo_r = mo_w = 0;
        transport_req = 1;
        for (b = 0; b < 4u * 60u * FS / 120u / CTL; b++) {  /* 4 beats at 120 */
            run(1);
            while (mo_r != mo_w) {
                uint32_t p = midi_out_q[mo_r++ % MQ];
                if ((p & 0xFFu) == 0x0Fu) {
                    f8 += ((p >> 8) & 0xFFu) == 0xF8u;
                    fa += ((p >> 8) & 0xFFu) == 0xFAu;
                }
            }
        }
        transport_req = 2;
        run(2);
        while (mo_r != mo_w) {
            uint32_t p = midi_out_q[mo_r++ % MQ];
            fc += (p & 0xFFu) == 0x0Fu && ((p >> 8) & 0xFFu) == 0xFCu;
        }
        ok(fa == 1u && fc == 1u, "clock out: START, STOP");
        ok(f8 >= 95u && f8 <= 97u, "clock out: 24 clocks a beat (96 in 4 beats)");
        if (f8 < 95u || f8 > 97u)
            printf("   (got %u)\n", f8);
    }
    {   /* clock in, pulse by pulse: 256 beats at 127 BPM (868.1 samples a pulse) */
        track_t *t = &trk[0];
        uint32_t want_beats, want_idx, f8 = 0;
        setup();
        usb.config = 1;
        song.g[G_SYNC] = 1;                              /* SYNC OUT on: nothing is sent while following */
        song.g[G_CLOCK] = 1;
        t->p[P_SDIV] = 2;                                /* 1/16 */
        t->p[P_SLEN] = 16;
        mc_t = mc_next = 0;
        mc_sent = mc_base = mc_checked = mc_off = 0;
        mc_trk = t;
        mo_r = mo_w = 0;
        master_rt(0xFA, 0);
        run(3);
        ok(song.playing && t->seq_pos == 0x7FFFFFFFu && !clk_pos, "clock in: START waits for the first pulse");
        mc_next = mc_t;
        master(127.0, 1, 0);                             /* the downbeat: step 0 */
        ok(t->seq_idx == 0 && t->seq_pos < 0x7FFFFFFFu, "clock in: the first pulse plays step 0");
        master(127.0, 24u * 256u, 0);
        want_beats = (mc_sent - 1u) / 24u;
        want_idx = ((mc_sent - 1u) / 6u) % 16u;
        ok(clk_beat == want_beats && t->seq_idx == want_idx,
           "clock in: 256 beats at 127: steps and beats on the pulses");
        if (clk_beat != want_beats || t->seq_idx != want_idx)
            printf("   (beat %u want %u, step %u want %u)\n", clk_beat, want_beats, t->seq_idx, want_idx);
        ok(song.g[G_BPM] == 127, "clock in: BPM shows the master's tempo (127)");
        master(93.0, 24u * 32u, 0);                      /* the master slows down */
        want_beats = (mc_sent - 1u) / 24u;
        want_idx = ((mc_sent - 1u) / 6u) % 16u;
        ok(clk_beat == want_beats && t->seq_idx == want_idx && song.g[G_BPM] == 93,
           "clock in: a tempo change (93): still on the pulses");
        ok(mc_checked == (mc_sent - 1u) / 6u + 1u && !mc_off, "clock in: every 1/16 step on its pulse (1153 steps)");
        if (mc_off)
            printf("   (%u of %u steps off their pulse)\n", mc_off, mc_checked);
        mc_trk = 0;
        while (mo_r != mo_w) {
            uint32_t p = midi_out_q[mo_r++ % MQ];
            f8 += (p & 0xFFu) == 0x0Fu;
        }
        ok(!f8, "clock in: no clock out while following one");
        master_rt(0xFC, 0);
        run(2);
        {
            uint32_t idx = t->seq_idx, beat = clk_beat;
            ok(!song.playing, "clock in: STOP stops");
            mc_t += 20000.0;
            run(20000u / CTL);
            mc_next = mc_t;
            master_rt(0xFB, 0);                          /* CONTINUE */
            run(1);
            ok(song.playing && t->seq_idx == idx && clk_beat == beat, "clock in: CONTINUE: on from where it stopped");
            master(93.0, 24u * 8u, 0);
            ok(clk_beat == beat + 8u, "clock in: ... and on with the pulses (8 beats)");
        }
        {   /* the master goes quiet while playing: 0.5 s later the internal clock takes over */
            uint32_t b0 = clk_beat;
            run(FS / 2u / CTL + 2u);
            ok(!mclk_on() && song.playing, "clock in: no pulse for 0.5 s: the internal clock");
            run(2u * 60u * FS / 93u / CTL);             /* two beats at the last tempo */
            ok(clk_beat >= b0 + 2u && clk_beat <= b0 + 3u, "clock in: ... which plays on at the last tempo");
        }
        master_rt(0xFC, 0);
        run(2);
    }
    {   /* the TRS jack as the source: its pulses drive, USB's are ignored */
        setup();
        song.g[G_CLOCK] = 2;
        mc_t = mc_next = 0;
        mc_sent = 0;
        master_rt(0xFA, 0);                              /* START from USB: ignored */
        run(2);
        ok(!song.playing, "clock TRS: USB START ignored");
        master_rt(0xFA, 1);
        run(2);
        mc_next = mc_t;
        master(140.0, 24u * 16u + 1u, 1);
        ok(song.playing && clk_beat == 16u && song.g[G_BPM] == 140, "clock TRS: START, 16 beats at 140");
        master(100.0, 12u, 0);                           /* USB pulses (0.3 s): not ours */
        master_rt(0xFC, 0);                              /* nor USB's STOP */
        run(2);
        ok(clk_beat == 16u && song.g[G_BPM] == 140 && song.playing, "clock TRS: USB pulses and STOP ignored");
        master_rt(0xFC, 1);
        run(2);
        ok(!song.playing, "clock TRS: STOP");
    }
    {   /* Jangada 0.7 (after SLOOP 2.4): MIDI OUT = SEQ sends what the sequencer plays, on the track's channel,
         * every note ended; KEYS again or STOP end what is on; MIDI IN = CLOCK takes no notes */
        uint32_t on = 0, offs = 0, b, ch_ok = 1, open_n = 0, k;
        int open[128] = {0};
        setup();
        song.g[G_CLOCK] = 0;
        song.g[G_SYNC] = 0;
        usb.config = 1;
        mo_r = mo_w = 0;
        t = &trk[1];
        t->p[P_SLEN] = 4; t->p[P_SDIV] = 2; t->p[P_SGATE] = 64;
        for (k = 0; k < 4u; k++)
            t->step[k] = (step_t){{(uint8_t)(60 + k), 0, 0, 0}, 1, ST_NOTE, 0, 100};
        midi_seq_out = 1;
        transport_req = 1;
        for (b = 0; b < 2u * 60u * FS / 120u / CTL; b++) {   /* two beats: 8 steps */
            run(1);
            while (mo_r != mo_w) {
                uint32_t p = midi_out_q[mo_r++ % MQ], st = (p >> 8) & 0xF0u, nt = (p >> 16) & 0x7Fu;
                if (st == 0x90u) { on++; open[nt]++; ch_ok &= ((p >> 8) & 15u) == 1u; }
                if (st == 0x80u) { offs++; open[nt]--; }
            }
        }
        ok(on == 8u && ch_ok, "MIDI OUT SEQ: 8 steps, 8 notes on channel 2");
        transport_req = 2;
        run(2);
        while (mo_r != mo_w) {
            uint32_t p = midi_out_q[mo_r++ % MQ];
            if (((p >> 8) & 0xF0u) == 0x80u) { offs++; open[(p >> 16) & 0x7Fu]--; }
        }
        for (k = 0; k < 128u; k++)
            open_n += open[k] != 0;
        ok(offs == on && !open_n, "MIDI OUT SEQ: every note ended (STOP too)");
        midi_seq_out = 0;
        mo_r = mo_w = 0;
        transport_req = 1;
        run(200);
        transport_req = 2;
        run(2);
        ok(mo_r == mo_w, "MIDI OUT KEYS: the sequencer sends nothing");
        midi_clk_only = 1;
        midi(0x90, 64, 100);
        run(1);
        ok(!trk[0].v[0].active && !trk[0].v[1].active, "MIDI IN CLOCK: a note in plays nothing");
        midi_clk_only = 0;
        midi(0x90, 64, 100);
        run(1);
        ok(trk[0].v[0].active || trk[0].v[1].active, "MIDI IN NOTES: it plays");
        midi(0x80, 64, 0);
        run(1);
    }
    if (fails)
        printf("MIDI: %u FAILED\n", fails);
    return fails != 0;
}
