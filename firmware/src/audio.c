/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* I2S output (ALNK0 -> external codec) and the audio ISR: each half buffer is
 * rendered in blocks of CTL samples by mix_block (fx.c: events -> each synth part
 * -> dist -> level / pan -> sends -> drums -> buses -> master), then scaled to 24-bit stereo. */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} felucca_dbg __attribute__((section(".noinit")));
static volatile uint32_t audio_halves, audio_max_us;
#if FELUCCA_UAC
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
#endif
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;

static void audio_block(int32_t *out, uint32_t n)       /* mix (fx.c), then Q15 -> 24 bit */
{
    uint32_t i;
    mix_block(out, n);
#if FELUCCA_UAC
    uac_tap(usb_full_now ? usb_out : out, n);           /* the USB audio input: the master output (usb.c), or at
                                                         * full level (menu USB AUDIO = FULL: fx.c usb_full_block) */
#endif
    click_add(out, n);
    for (i = 0; i < n; i++) {
        if (i & 1u)
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)out[2u * i];
        out[2u * i] <<= OUT_SHIFT;
        out[2u * i + 1u] <<= OUT_SHIFT;
    }
}

/* overload: two halves in a row over 85 % of their time ask for one voice to be shed before the next
 * one (shed_voice, voice.c: a fade, never a part's bass or lead) */
static volatile uint8_t shed_req;
static uint8_t shed_over;                      /* bit k: the half k halves ago was over 85 % */

static void shed_late(uint32_t us)             /* end of a half: us = its time, as the deadline sees it */
{
    shed_over = (uint8_t)(shed_over << 1 | (us * 100u > (HALF_FRAMES * 1000000u / FS) * 85u));
    if ((shed_over & 3u) == 3u)
        shed_req = 1;                          /* two in a row */
}

#if FELUCCA_UAC
/* end of a half: all = its ticks, TIMER5 nested in it included (main.c). The deadline (the shed) sees
 * both; the load figures get the render alone, in us (out of line: the ISR's loops stay as they were) */
static __attribute__((noinline)) uint32_t shed_check(uint32_t all)
{
    shed_late(all / FM1_TICKS_PER_US);
    return (all - t5_nested_ticks) / FM1_TICKS_PER_US;
}
#endif

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
#if FELUCCA_UAC
    uint8_t p;
    uint32_t t0;
    felucca_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
#else
    uint8_t p = fm1_audio_pending();
    uint32_t t0 = fm1_ticks();
    fm1_audio_ack_aux(p);
    felucca_dbg.in_audio = 1;
#endif
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        if (shed_req) {
            shed_req = 0;
            shed_voice();
        }
#if FELUCCA_UAC
        uac_render_start();
        usb_full_now = (uint8_t)(usb_full && uac.feed);  /* menu USB AUDIO = FULL, while the computer records */
#endif
        for (b = 0; b < HALF_FRAMES; b += CTL)
            audio_block(o + 2u * b, CTL);
        fm1_audio_ack_half();
        audio_halves++;
#if FELUCCA_UAC
        us = shed_check(fm1_ticks() - t0);
        if (us > audio_max_us)
            audio_max_us = us;
#else
        us = (fm1_ticks() - t0) / FM1_TICKS_PER_US;
        if (us > audio_max_us)
            audio_max_us = us;
        shed_late(us);
#endif
        song.cpu_q8 = (song.cpu_q8 * 15u + (us * 256u) / (HALF_FRAMES * 1000000u / FS)) / 16u;
        if (fm1_audio_free_half() != half)
            felucca_dbg.late++;                         /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* hal/fm1_audio.h */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
