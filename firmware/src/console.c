/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Serial console on the CDC-ACM function (FELUCCA_CDC=1): read-only
 * diagnostics. Runs in the main loop (cdc_task); usb_poll moves the bytes.
 * The baud rate is ignored. Nothing here writes memory or flash; `uboot`
 * does what the SysEx soft key does. */
#define CON_LINE 64u

static struct {
    char line[CON_LINE];
    uint32_t len;
    uint8_t dtr_seen, stalled;   /* stalled: the host stopped reading, drop the rest of this reply */
} con;

static void con_putc(char c)
{
    uint32_t t0 = fm1_ms;
    while (co_w - co_r >= CO_N) {                      /* full: wait for usb_poll, briefly */
        if (!cdc.dtr || con.stalled || fm1_ms - t0 > 20u) {
            con.stalled = 1;
            return;
        }
        fm1_wdt_feed();
    }
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}

static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}

static void con_hex(uint32_t v, uint32_t digits)
{
    while (digits--)
        con_putc("0123456789ABCDEF"[(v >> (digits * 4u)) & 15u]);
}

static void con_dec(int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        con_putc('-');
    do
        b[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0 && n < sizeof b);
    while (n)
        con_putc(b[--n]);
}

static void con_kv(const char *k, int32_t v)            /* "key value\r\n" */
{
    con_puts(k);
    con_putc(' ');
    con_dec(v);
    con_puts("\r\n");
}

static void con_kx(const char *k, uint32_t v)
{
    con_puts(k);
    con_puts(" 0x");
    con_hex(v, 8);
    con_puts("\r\n");
}

static uint32_t con_num(const char **p, int *ok)        /* decimal or 0x hex */
{
    const char *s = *p;
    uint32_t v = 0, base = 10, d, n = 0;
    while (*s == ' ')
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2, base = 16;
    for (;; s++, n++) {
        char c = *s;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            break;
        v = v * base + d;
    }
    *ok = n > 0;
    *p = s;
    return v;
}

static int con_word(const char **p, const char *w)       /* match a whole word */
{
    const char *s = *p;
    while (*s == ' ')
        s++;
    while (*w && *s == *w)
        s++, w++;
    if (*w || (*s && *s != ' '))
        return 0;
    *p = s;
    return 1;
}

static void con_memr(const char *p)
{
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok) {
        con_puts("usage: memr ADDR [LEN<=256]\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (!fm1_mem_readable(a, n)) {
        con_puts("only RAM 01C00000-01C80000 and XIP 02000000-02100000\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 8);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(fm1_peek8(a + i), 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}

#if FELUCCA_FLASH
static void con_flr(const char *p)                  /* flash read over SPI (no XIP decryption) */
{
    static uint8_t b[256];
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok || a < 0x93000u || a >= 0x100000u) {
        con_puts("usage: flr OFFSET [LEN<=256]   (0x93000..0xFFFFF)\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (a + n > 0x100000u)
        n = 0x100000u - a;
    if (!flash_ok || st_read(a, b, n)) {
        con_puts("flash not available\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 6);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(b[i], 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}
#endif

#if FELUCCA_UAC
static void con_uac(void)                              /* USB audio input: stream state and glitches */
{
    uint32_t p = uac.pkts;
    con_kv("uac_alt", uac.alt);
    con_kv("uac_starts", (int32_t)uac.starts);
    con_kv("uac_pkts", (int32_t)p);
    con_kv("uac_rate_hz", p ? 44000 + (int32_t)(uac.frames - 44u * p) * 1000 / (int32_t)p : 0);   /* < 5 h */
    con_kv("uac_underruns", (int32_t)uac.underruns);
    con_kv("uac_overruns", (int32_t)uac.overruns);
    con_kv("uac_missed", (int32_t)uac.missed);
    con_kv("uac_stalls", (int32_t)uac.stalls);
    con_kv("uac_adj_up", (int32_t)uac.adj_up);
    con_kv("uac_adj_down", (int32_t)uac.adj_down);
    con_kv("uac_fill", (int32_t)uac.fill_min);
    con_kv("uac_fill_lo", uac.fill_lo == 0xFFFFFFFFu ? -1 : (int32_t)uac.fill_lo);
    con_kv("uac_fill_hi", (int32_t)uac.fill_hi);
}
#endif

static void con_status(void)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    con_puts("jangada ");
    con_puts(FELUCCA_VERSION);
    con_puts("\r\n");
    con_kv("uptime_ms", (int32_t)fm1_ms);
    con_kv("cpu_pct", (int32_t)(song.cpu_q8 * 100u / 256u));
    con_kv("cpu2_mode", cpu2_mode);                   /* menu 2ND CORE: 0 AUTO, 1 OFF, 2 BOOST (panel.c) */
    con_kv("cpu2_failed", fm1_c1_failed);             /* held this session: it misread or timed out */
    con_kv("cpu2", fm1_c1_on);                        /* Jangada 1.0: the second core answers (fx.c mix_block) */
    con_kv("cpu2_split", c1_split);                   /* parts handed to it (cpu2 on / off) */
    con_kv("cpu2_blocks", (int32_t)c1_blocks);        /* blocks it rendered parts of */
    con_kv("cpu2_wait_max_us", (int32_t)(fm1_c1_wait_max / FM1_TICKS_PER_US));   /* the longest wait for it */
    con_kv("cpu2_timeouts", (int32_t)fm1_c1_timeouts);   /* jobs it did not finish (then held) */
    con_kv("cpu2_stack", (int32_t)fm1_cpu1_stack_used());   /* bytes of its 4096 ever used */
    con_kx("cpu2_trace", fm1_c1_trace);
    con_kv("sysvdd", (int32_t)fm1_rail_get(FM1_P3_SYSVDD, 15u, 1));   /* the core supply (hal/fm1_sys.h): 11 1.26 V, 14 1.35 V */
    con_kv("vdc14", (int32_t)fm1_rail_get(FM1_P3_VDC14, 7u, 1));      /* 3 1.40 V, 4 1.45 V */
    con_kv("cpu2_bad", (int32_t)fm1_c1_mb.bad);      /* jobs it would not run (no function, out of sequence) */
    con_kx("cpu2_bad_job", fm1_c1_mb.bad_job);
    con_kx("cpu2_bad_done", fm1_c1_mb.bad_done);
    con_kx("cpu2_bad_fn", fm1_c1_mb.bad_fn);
    con_kx("cpu2_job", fm1_c1_mb.job);
    con_kx("cpu2_done", fm1_c1_mb.done);               /* C1000001 its entry, C1000002 its loop */
    con_kv("audio_max_us", (int32_t)felucca_dbg.max_us);
    con_kv("voices_shed", (int32_t)shed_count);
    con_kv("voices_given_up", (int32_t)voice_kills);
    con_kv("track", (int32_t)song.sel + 1);
    con_kv("batt_raw", song.batt_raw);
    con_puts("engine ");
    con_puts(e->name);
    con_puts("\r\n");
    con_puts("preset ");
    con_puts(TSEL->preset < e->npresets ? e->presets[TSEL->preset].name : "-");
    con_puts("\r\n");
    con_kv("bpm", song.g[G_BPM]);
    con_kv("playing", song.playing);
    con_kv("boots", (int32_t)felucca_dbg.boots);
    con_kv("usb_resets", (int32_t)usb.resets);
    con_kv("usb_sof", (int32_t)usb.sof_seen);
    con_kv("usb_suspends", (int32_t)usb.suspends);
    con_kv("usb_retries", (int32_t)usb.retries);
    con_kv("usb_frame", (int32_t)usb.frame);
    con_kv("usb_frame_stalls", (int32_t)usb.frame_stalls);
    con_kv("usb_max_gap_polls", (int32_t)usb.max_gap);
    con_kv("midi_rx_pkts", (int32_t)usb.rx_pkts);
    con_kv("midi_rx_held", (int32_t)usb.rx_held);     /* EP1 packets held back (NAK): the ring was full */
    con_kv("midi_rx_bad", (int32_t)usb.rx_bad);       /* malformed events ignored */
    con_kv("midi_tx_pkts", (int32_t)usb.tx_pkts);
#if FELUCCA_UART
    con_kv("trs_bytes", (int32_t)um.bytes);           /* TRS MIDI IN (midi_uart.c) */
    con_kv("trs_msgs", (int32_t)um.msgs);
    con_kv("trs_drops", (int32_t)um.drops);           /* the MIDI ring was full */
#endif
#if FELUCCA_UAC
    con_uac();
#endif
#if FELUCCA_FLASH
    con_kv("flash", flash_ok);
#endif
}

static void con_dbg(void)
{
    const uint32_t *w = (const uint32_t *)&felucca_dbg;
    static const char *const NAMES[] = {"magic", "halves", "max_us", "nested", "in_audio", "late",
                                        "timer_irqs", "ui_frames", "last_us", "cpu_q8", "boots",
                                        "stage", "page", "home", "prev_stage", "prev_page",
                                        "prev_home", "prev_rst", "prev_frames"};
    uint32_t i;
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0] && i < sizeof felucca_dbg / 4u; i++)
        con_kx(NAMES[i], w[i]);
#if FELUCCA_UAC
    con_uac();
#endif
}

static void con_crash(void)
{
    if (fm1_crash.magic != FM1_CRASH_MAGIC) {
        con_puts("no crash record\r\n");
        return;
    }
    con_kv("count", (int32_t)fm1_crash.count);
    con_kx("vec", fm1_crash.vec);
    con_kx("pc", fm1_crash.pc);
    con_kx("rets", fm1_crash.rets);
    con_kx("emu", fm1_crash.emu);
    con_kx("sp", fm1_crash.sp);
    con_kx("psr", fm1_crash.psr);
    con_kv("uptime_ms", (int32_t)fm1_crash.uptime_ms);
    con_kv("early", (int32_t)fm1_crash.early);
    con_kv("core", (int32_t)fm1_crash.core);
    con_kx("dbg", fm1_crash.dbg);
    con_kx("etm0", fm1_crash.etm[0]);
    con_kx("etm1", fm1_crash.etm[1]);
}

static void con_params(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) {
        con_dec((int32_t)i);
        con_putc('=');
        con_dec(TSEL->p[i]);
        con_puts(i % 8u == 7u || i + 1u == P_COUNT ? "\r\n" : " ");
    }
}

/* color [N | NAME]: the screen palette, saved like the HOME-hold COLOR menu (Jangada) */
static void con_color(const char *p)
{
    int ok;
    uint32_t i, n = con_num(&p, &ok);
    if (!ok)
        for (i = 0; i < NPALETTES; i++)
            if (con_word(&p, PALETTES[i].name)) {
                n = i;
                ok = 1;
            }
    if (ok && n < NPALETTES) {
        settings.palette = n;
        palette_set(n);
        settings_save();
        ui.force = 1;
    } else if (*p) {
        con_puts("usage: color [N | NAME]\r\n");
    }
    for (i = 0; i < NPALETTES; i++) {
        con_puts(i == settings.palette ? "* " : "  ");
        con_dec((int32_t)i);
        con_putc(' ');
        con_puts(PALETTES[i].name);
        con_puts("\r\n");
    }
}

/* preset ENGINE INDEX [TRACK 1..3]: as the editor's PRESET (hardware tests: tools/fm1_console.py) */
static void con_preset(const char *p)
{
    int ok1, ok2, ok3;
    uint32_t e = con_num(&p, &ok1), pi = con_num(&p, &ok2), tr = con_num(&p, &ok3);
    track_t *t;
    if (!ok1 || !ok2 || e >= NENGINES || (ok3 && (tr < 1u || tr > NTRK || !trk_synth(tr - 1u)))) {
        con_puts("usage: preset ENGINE INDEX [TRACK 1..3, 4 when T4 is SYNTH]\r\n");
        return;
    }
    t = &trk[ok3 ? tr - 1u : trk_synth(song.sel) ? song.sel : 0u];
    if (e != t->eng_req)
        set_engine_of(t, e);
    apply_preset_to(t, pi);
    ui.force = 1;
    con_puts(ENGINES[e]->name);
    con_putc(' ');
    con_puts(ENGINES[e]->presets[t->preset].name);
    con_puts("\r\n");
}

/* g ID [-]VALUE: set a global parameter (G_*), clamped to its range; g ID alone prints it */
static void con_global(const char *p)
{
    int ok1, ok2, neg;
    uint32_t id = con_num(&p, &ok1), v;
    while (*p == ' ')
        p++;
    neg = *p == '-';
    p += neg;
    v = con_num(&p, &ok2);
    if (!ok1 || id >= G_COUNT) {
        con_puts("usage: g ID [VALUE]\r\n");
        return;
    }
    if (ok2) {
        song.g[id] = (int16_t)clamp(neg ? -(int32_t)v : (int32_t)v, GP[id].min, GP[id].max);
        ui.force = 1;
    }
    con_puts(GP[id].label);
    con_putc(' ');
    con_dec(song.g[id]);
    con_puts("\r\n");
}

/* punch N | off: start punch-in effect N (0..15) as FX + its key, or let it go (hardware tests) */
static void con_punch(const char *p)
{
    int ok;
    uint32_t n = con_num(&p, &ok);
    if (ok && n < PUNCH_NFX) {
        punch.req = (int8_t)n;
        con_puts(PUNCH_NAME[n]);
    } else {
        punch.req = -1;
        con_puts("off");
    }
    con_puts("\r\n");
}

/* t4 [drum | synth]: GLO > DRUMS T4, as the knob (ui.c t4_follow does the rest) */
static void con_t4(const char *p)
{
    if (con_word(&p, "synth"))
        song.g[G_T4] = 1;
    else if (con_word(&p, "drum"))
        song.g[G_T4] = 0;
    else if (*p)
        con_puts("usage: t4 [drum | synth]\r\n");
    con_puts(song.g[G_T4] ? "track 4: synth\r\n" : "track 4: drum\r\n");
}

/* voices: per track what sounds and why (latched chords, arp, sequencer): hardware checks */
static void con_voices(void)
{
    uint32_t i, k;
    for (i = 0; i < NTRK; i++) {
        const track_t *t = &trk[i];
        uint32_t a = 0, g = 0;
        for (k = 0; k < NVOICE; k++) {
            a += t->v[k].active;
            g += t->v[k].active && t->v[k].gate;
        }
        con_puts("track ");
        con_dec((int32_t)i + 1);
        con_puts(is_drum(t) ? " drum" : " synth");
        con_puts(" active ");
        con_dec((int32_t)a);
        con_puts(" gate ");
        con_dec((int32_t)g);
        con_puts(" held ");
        con_dec(t->nheld);
        con_puts(" keys ");
        con_dec(t->arp_phys);
        con_puts(" arp ");
        con_dec(t->p[P_AMODE]);
        con_puts(" hold ");
        con_dec(t->p[P_AHOLD]);
        con_puts(" evol ");                            /* Jangada DRONES: EVOL, TENS, the tension now (%) */
        con_dec(t->p[P_EVOL]);
        con_puts(" tens ");
        con_dec(t->p[P_TENS]);
        con_puts(" now ");
        con_dec((drn[i].tens >> 16) * 100 / 32767);
        con_puts("\r\n");
    }
    con_kv("playing", song.playing);
}

static void con_exec(const char *p)
{
    if (con_word(&p, "help") || con_word(&p, "?"))
        con_puts("status  dbg  crash  params  color [N|NAME]  preset E I [T]  t4 [drum|synth]  g ID [VAL]  punch N|off  voices  droneoff  cpu2 [on|off|clr]  vdd [S [D]]  p33 ADDR [BYTE]  memr ADDR [LEN]  flr OFF [LEN]  uboot yes\r\n");
    else if (con_word(&p, "status"))
        con_status();
    else if (con_word(&p, "cpu2")) {                   /* Jangada 1.0: the second core's split on / off */
        if (con_word(&p, "on"))
            c1_split = 1;
        else if (con_word(&p, "off"))
            c1_split = 0;
        else if (con_word(&p, "clr"))                  /* a fresh count of the jobs it would not run */
            fm1_c1_mb.bad = 0;
        con_kv("cpu2", fm1_c1_on);
        con_kv("cpu2_split", c1_split);
        con_kv("cpu2_bad", (int32_t)fm1_c1_mb.bad);
    } else if (con_word(&p, "vdd")) {                  /* the core supply: vdd [11..15] (fm1_sys.h) */
        int ok;
        uint32_t s = con_num(&p, &ok);
        if (ok)
            fm1_core_supply(s, 1);
        s = con_num(&p, &ok);                          /* vdd S D: then VDC14 to D, either way (measuring) */
        if (ok && s <= 7u)
            fm1_rail_set(FM1_P3_VDC14, 7u, s, 1);
        con_kv("sysvdd", (int32_t)fm1_rail_get(FM1_P3_SYSVDD, 15u, 1));
        con_kv("vdc14", (int32_t)fm1_rail_get(FM1_P3_VDC14, 7u, 1));
    } else if (con_word(&p, "p33")) {                  /* p33 ADDR [BYTE]: one P33 register, raw (measuring) */
        int ok, ok2;
        uint32_t a = con_num(&p, &ok), v = con_num(&p, &ok2);
        if (ok && a < 0x400u) {
            if (ok2)
                fm1_rail_poke(a, v);
            con_kx("p33", fm1_rail_get(a, 0xFFu, 1));
        }
    } else if (con_word(&p, "dbg"))
        con_dbg();
    else if (con_word(&p, "crash"))
        con_crash();
    else if (con_word(&p, "params"))
        con_params();
    else if (con_word(&p, "color"))
        con_color(p);
    else if (con_word(&p, "preset"))
        con_preset(p);
    else if (con_word(&p, "t4"))
        con_t4(p);
    else if (con_word(&p, "g"))
        con_global(p);
    else if (con_word(&p, "punch"))
        con_punch(p);
    else if (con_word(&p, "voices"))
        con_voices();
    else if (con_word(&p, "droneoff")) {               /* as ARP held: latched chords off */
        latch_off_req = (uint8_t)((1u << NTRK) - 1u);
        con_puts("drone off\r\n");
    }
    else if (con_word(&p, "memr"))
        con_memr(p);
#if FELUCCA_FLASH
    else if (con_word(&p, "flr"))
        con_flr(p);
#endif
    else if (con_word(&p, "uboot")) {
        if (con_word(&p, "yes")) {
            con_puts("entering UBOOT\r\n");
            usb.uboot_req = 1;                         /* main loop: same path as the SysEx key */
        } else {
            con_puts("type 'uboot yes'\r\n");
        }
    } else if (*p)
        con_puts("? (help)\r\n");
}

static void cdc_task(void)                              /* main loop */
{
    if (cdc.dtr && !con.dtr_seen) {
        con_puts("\r\nJangada ");
        con_puts(FELUCCA_VERSION);
        con_puts(" console - 'help'\r\n> ");
    }
    con.dtr_seen = cdc.dtr;
    while (ci_r != ci_w) {
        char c;
        RING_PUBLISH();                                /* usb_poll (producer) can preempt us */
        c = (char)cdc_in[ci_r % CI_N];
        RING_PUBLISH();
        ci_r++;
        if (c == '\r' || c == '\n') {
            if (c == '\n' && con.len == 0)
                continue;                              /* the LF of a CR LF */
            con_puts("\r\n");
            con.line[con.len] = 0;
            con.stalled = 0;
            con_exec(con.line);
            con.len = 0;
            con_puts("> ");
        } else if (c == 0x7F || c == 0x08) {
            if (con.len) {
                con.len--;
                con_puts("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && con.len + 1u < CON_LINE) {
            con.line[con.len++] = c;
            con_putc(c);
        }
    }
}
