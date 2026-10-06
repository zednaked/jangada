/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA boot and main loop. Boot order: WDT first, boot-loop guard, fatal
 * vectors, guards; then LCD, input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];


#if FELUCCA_UAC
/* Jangada, from Felucca 1.0.1: with the USB audio input, TIMER5 outranks ALNK0. The stream cannot
 * wait for the render (one packet per 1 ms USB frame; a 256-frame half renders for up to ~5 ms), so
 * uac_service runs nested in it. It touches only EP4 (INDEX is set on every access) and the consumer
 * side of the audio ring (usb.c); usb_poll never runs nested, so the two never interleave on the SIE.
 * Nested, the tick also scans (fm1_input_tick: GPIO and fm1_in; the audio ISR only reads fm1_in.notes,
 * one word, in seq.c keyboard_block) and counts ms (fm1_ms: one word, read by lk_put in the ISR);
 * the scan keeps its 100 us pace instead of stopping for the whole render. USB and UART polls are owed
 * to the first tick after the render (their rings are shared with the audio ISR, which they must not
 * interrupt), and the time spent nested is handed to the audio ISR (t5_nested_ticks) so its load
 * figures stay render-only. Without UAC (FELUCCA_UAC=0) TIMER5 stays below ALNK0, as before. */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    fm1_input_tick();
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;                             /* 2 kHz: all USB SIE traffic lives here */
#if FELUCCA_UART
    if (sub % 5u == 2u)
        owed |= 2u;                             /* 2 kHz: <= ~7 bytes per call at 31250 baud */
#endif
    if (++sub == 10u)
        sub = 0;
    if (felucca_dbg.in_audio) {
        felucca_dbg.nested++;
        if (usb_due)
            uac_service();
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
#if FELUCCA_UART
    if (owed & 2u)
        uart_midi_poll();
#endif
    owed = 0;
}
extern void isr_timer5(void);

static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
    fm1_timer5_start(isr_timer5, 4);   /* above ALNK0 (3): nests into the render (see fm1_timer5_irq) */
}
#else
void fm1_timer5_irq(void)
{
    static uint32_t sub;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    if (felucca_dbg.in_audio)
        felucca_dbg.nested++;                      /* only possible if this IRQ outranks ALNK0 */
    fm1_input_tick();
    if (sub % 5u == 0u)
        usb_poll();                             /* 2 kHz: all USB SIE traffic lives here */
#if FELUCCA_UART
    if (sub % 5u == 2u)
        uart_midi_poll();                       /* 2 kHz: <= ~7 bytes per call at 31250 baud */
#endif
    if (++sub == 10u)
        sub = 0;
    {   /* milliseconds from the 24 MHz TIMER4: TIMER5 ticks coalesce while ALNK0 renders */
        static uint32_t last, acc;
        uint32_t now = fm1_ticks();
        acc += now - last;
        last = now;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
}
extern void isr_timer5(void);

static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
    fm1_timer5_start(isr_timer5, 1);   /* below ALNK0 (3): no nesting into audio */
}
#endif

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "JANGADA CRASH", C_WHITE, 1);
    hexs(b, c->vec);
    draw_text_box(10, 40, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->pc);
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->emu);
    draw_text_box(10, 84, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->dbg);
    draw_text_box(10, 102, 220, &FONT_S, b, C_WHITE, 0);
    hexs(b, c->rets);
    draw_text_box(10, 120, 220, &FONT_S, b, C_WHITE, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* power-on: three parts with their default sounds (TRK_DEF), the drum track, empty patterns. RAM only, no
 * IRQ work of its own (set_engine_raw): MENU > NEW PROJECT (ui_menu.c menu_new_project) runs it with the
 * audio IRQ off, as a project load */
static void felucca_init(void)
{
    uint32_t i;
    fm6_init();                                 /* Jangada: every track the FM6 init voice */
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_raw(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);   /* with its sends */
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);              /* the sequencers start empty */
    }
    song.sel = 0;
    song.solo = 0;                              /* (Jangada: also NEW PROJECT, ui_menu.c) */
    song.rec = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    persist_boot();
#if FELUCCA_FLASH
    if (flash_ok)
        fm6_bank_boot();                                /* Jangada: which FM6 bank slots are used */
#endif
#if FELUCCA_OTA
    if (flash_ok)
        ota_boot_cleanup();                             /* staging area left by an update */
#endif
    settings_init();
    lcd_init();
    draw_text_box(0, 100, 240, &FONT_L, "JANGADA", C_HI, 1);
    draw_text_box(0, 130, 240, &FONT_S, "MULTI-ENGINE SYNTH", C_GRAY, 1);
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
#if FELUCCA_UAC
    felucca_dbg.in_audio = 0;                       /* a reset in the render leaves it set: TIMER5 would */
#endif                                              /* take itself for nested (no usb_poll) until a half ends */
    felucca_dbg.prev_stage = felucca_dbg.stage;     /* a WDT reset leaves the last breadcrumb here */
    felucca_dbg.prev_page = felucca_dbg.page;
    felucca_dbg.prev_home = felucca_dbg.home;
    felucca_dbg.prev_frames = felucca_dbg.ui_frames;
    felucca_dbg.prev_rst = fm1_boot.p3_rst;
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    felucca_init();
    audio_init();
    usb_start();
#if FELUCCA_UART
    uart_midi_init();
#endif
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    if ((fm1_in.buttons & 3u) == 3u) {
        panel_setup();                        /* OCT- + OCT+ held at power-on */
        settings_save();
    }
    fm1_delay_ms(400);
#if FELUCCA_FLASH
    autosave_resume();                        /* Jangada: the project as it was left (project.c) */
#endif
    lcd_fill(0, 0, 240, 240, C_BLACK);

    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
        if (fm1_ms > 30000u && bootguard.pending) {     /* a crash or hang in the first 30 s counts */
            bootguard.pending = 0;
            bootguard.failed = 0;
        }
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);     /* battery: slow IIR */
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                song.master_q12 = (k10 * k10) >> 8;            /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s: enter UBOOT with RAM intact (debug / update); a countdown
             * shows from 2 s, letting go cancels it */
            static uint32_t t0, shown;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (shown)
                    ui_say("UPDATE MODE ", "CANCELLED");
                shown = 0;
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != shown) {
                    char d[4] = {(char)('0' + left), '.', '.', 0};
                    ui_say("UPDATE MODE IN ", d);
                    shown = left;
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
                lcd_fill(0, 0, 240, 240, C_BLACK);
                draw_text_box(0, 110, 240, &FONT_S, "UBOOT", RGB(80, 120, 255), 1);
                usb_detach();
                fm1_delay_ms(30);
                bootguard.pending = 0;                  /* intentional reset: not a failed boot */
                fm1_enter_uboot();
            }
        }
#if FELUCCA_OTA
        ed_service();                                   /* web editor SysEx */
#if FELUCCA_DX7 && FELUCCA_FLASH
        dx_service();                                   /* Jangada: DX7 SysEx (Dexed) for the FM6 track */
#endif
        ota_service();                                  /* M-UPGRADE handshake */
        if (usb.ota_req) {                              /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            panic_req = (1u << NTRK) - 1u;               /* every track (bit per track) */
            if (flash_ok)
                ota_session();                          /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, C_BLACK);
            ui.force = 1;
        }
#endif
        if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */
            fm1_audio_stop();
            lcd_fill(0, 0, 240, 240, C_BLACK);
            draw_text_box(0, 110, 240, &FONT_S, "UBOOT (USB)", C_WHITE, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard.pending = 0;
            fm1_enter_uboot();
        }
#if FELUCCA_CDC
        cdc_task();
#endif
        felucca_dbg.ui_frames++;
        felucca_dbg.page = ui.page;
        felucca_dbg.home = ui.home;
        felucca_dbg.stage = 1;
        ui_input();
        felucca_dbg.stage = 2;
        ui_leds();
        ui_draw();
#if FELUCCA_FLASH
        autosave_tick();                                /* Jangada: the working project into flash, when quiet */
#endif
        felucca_dbg.stage = 9;
        while (fm1_ms - m < 15u) {                               /* ~60 UI frames/s at most */
            ui_input();
#if FELUCCA_OTA
            ed_service();                       /* editor replies without waiting for the next frame */
#if FELUCCA_DX7 && FELUCCA_FLASH
            dx_service();                       /* (a knob turned in Dexed: heard within a millisecond or two) */
#endif
#endif
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt, boot_mode;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    if ((p3 & 1u) && !(p3 & (4u | 0x40u)))
        bootguard_clear(&bootguard);           /* a power-on (not a watchdog or soft reset): no failed boot to count */
    boot_mode = bootguard_begin(&bootguard);   /* Jangada (after SLOOP): failed boots -> the rescue, then ROM */
    if (boot_mode == BOOT_ROM)
        fm1_enter_uboot();
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
#if FELUCCA_OTA
    if (boot_mode == BOOT_RECOVERY || recovery_key())
        recovery_main();                       /* OCT- held alone at power-on, or failed boots */
#else
    if (boot_mode == BOOT_RECOVERY)
        fm1_enter_uboot();
#endif
    fm1_main();
    for (;;)
        ;
}
