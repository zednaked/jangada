/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada 0.7: the full-screen visualiser (the idea after SLOOP 2.4's; Jangada's own styles). On the TRACKS
 * screen HOME tapped opens it, HOME again goes HOME, any page button leaves it; SELECT steps through
 * the styles (the name shows a moment). The keys, PLAY, REC and the layers work as ever.
 *   OSC       the master's wave, with the last two traces fading behind it (phosphor)
 *   SONAR     the wave around a circle, a sweep turning once a bar, a ping on each beat
 *   VU        a segmented meter per track and the master, the peaks held
 *   ESTEIRA   the conveyor: each track's level as a strip scrolling left
 *   MAR       the raft on the sea: each track a swell, the raft riding the top one, rocked by the mix
 * It reads what the audio already leaves for the UI: the scope buffer (audio.c, the mix at 22 kHz), the tracks'
 * peaks (fx.c / drums.c, as the TRACKS meters read them: taken here while it shows) and the beat clock. Drawn
 * in the main loop, every other frame, in two bands of 120 rows (the canvas holds 124): no cost to the audio.
 * vis_style is kept with the settings (panel.c lights_word) */
#define VIS_N 5u
static const char *const VIS_NAME[VIS_N] = {"OSC", "SONAR", "VU", "ESTEIRA", "MAR"};
#define VIS_HIST 60u                                    /* ESTEIRA: columns of 4 px */
static uint8_t vis_on, vis_name_t;
static struct {
    int16_t snap[3][240];                               /* OSC: this trace and the two before (y) */
    int16_t lvl[5], vu[5], pk[5], pkt[5];               /* the tracks' and the master's level 0..1000, meters */
    uint8_t hist[4][VIS_HIST];                          /* ESTEIRA: levels / 4 */
    uint32_t hw, frame, last_beat;
    int32_t scale;                                      /* the wave's auto-scale (a smoothed peak) */
    int16_t wave[256];                                  /* this frame's scope snapshot, 256 points */
    uint8_t ping;                                       /* SONAR: frames since the beat */
} vs;

static int vis_shown(void) { return vis_on && !ui.home && !ui.menu && !ui.confirm && cur_page()->scope == SC_TRK; }
static void vis_open(void)
{
    vis_on = 1;
    vis_name_t = 30;
    ui.force = 1;
}
static void vis_select(int32_t s)
{
    vis_style = (uint8_t)((vis_style + (s > 0 ? 1u : VIS_N - 1u)) % VIS_N);
    vis_name_t = 30;
    settings_later = 1;                                 /* (kept: project.c autosave_tick saves the settings) */
    ui.force = 1;
}
static int32_t vsin(uint32_t a) { return SINE[a & 1023u]; }          /* a: 1024 a turn; Q15 */
static int32_t vcos(uint32_t a) { return SINE[(a + 256u) & 1023u]; }
/* |x| (Q15) -> 0..1000 over 48 dB */
static int32_t vis_lvl(int32_t a)
{
    int32_t lg = 0;
    if (a < 0)
        a = -a;
    if (a < 128)
        return 0;
    while (a >= 256) {                                  /* log2, 1/8 steps below */
        a >>= 1;
        lg += 8;
    }
    lg += (a - 128) >> 4;
    return clamp(lg * 1000 / 64, 0, 1000);
}

/* once a drawn frame: the scope snapshot, the levels, the histories */
static void vis_take(void)
{
    uint32_t w = scope_w, i, beat;
    int32_t pk = 0, k;
    for (i = 0; i < 256u; i++) {
        int32_t v = scope_buf[(w - 512u + 2u * i) & (SCOPE_N - 1u)];
        vs.wave[i] = (int16_t)v;
        if ((v < 0 ? -v : v) > pk)
            pk = v < 0 ? -v : v;
    }
    if (pk > vs.scale)
        vs.scale = pk;                                  /* the scale: up at once, down slowly */
    else
        vs.scale += ((pk > 2000 ? pk : 2000) - vs.scale) / 16;
    if (vs.scale < 2000)
        vs.scale = 2000;
    for (k = 0; k < 4; k++) {
        int32_t p;
        if (k == TRK_DRUM && is_drum(TDRUM)) {
            p = drums.peak;
            drums.peak = 0;
        } else {
            p = trk[k].peak;
            trk[k].peak = 0;
        }
        p = vis_lvl(p >> 2);                            /* (the peaks run ~4x the output) */
        vs.lvl[k] = (int16_t)(p > vs.lvl[k] ? p : vs.lvl[k] - (vs.lvl[k] - p) / 4);
    }
    vs.lvl[4] = (int16_t)vis_lvl(pk);
    for (k = 0; k < 5; k++) {                           /* the meters: up at once, down slowly; the peaks held */
        vs.vu[k] = (int16_t)(vs.lvl[k] > vs.vu[k] ? vs.lvl[k] : vs.vu[k] - 25);
        if (vs.vu[k] < 0)
            vs.vu[k] = 0;
        if (vs.vu[k] >= vs.pk[k]) {
            vs.pk[k] = vs.vu[k];
            vs.pkt[k] = 30;
        } else if (vs.pkt[k] && !--vs.pkt[k]) {
            vs.pk[k] = vs.vu[k];
        }
    }
    vs.hw = (vs.hw + 1u) % VIS_HIST;
    for (k = 0; k < 4; k++)
        vs.hist[k][vs.hw] = (uint8_t)(vs.lvl[k] / 4);
    beat = song.playing ? clk_beat : 0u;
    if (beat != vs.last_beat) {
        vs.last_beat = beat;
        vs.ping = 0;
    } else if (vs.ping < 255u) {
        vs.ping++;
    }
    vs.frame++;
}

static void vis_osc(void)
{
    uint32_t i, k;
    int32_t trig = 0;
    for (i = 1; i < 16u; i++)                           /* a rising zero crossing near the start: a still wave */
        if (vs.wave[i - 1] < 0 && vs.wave[i] >= 0) {
            trig = (int32_t)i;
            break;
        }
    for (i = 0; i < 240u; i++) {                        /* (the older traces move back) */
        vs.snap[2][i] = vs.snap[1][i];
        vs.snap[1][i] = vs.snap[0][i];
    }
    for (i = 0; i < 240u; i++) {
        int32_t v = vs.wave[(trig + (int32_t)i) & 255];
        vs.snap[0][i] = (int16_t)clamp(120 - v * 100 / vs.scale, 4, 236);
    }
    cv_rect(0, 120, 240, 1, C_LINE);
    for (k = 3; k-- > 0;) {
        uint16_t c = k == 0 ? C_HI : k == 1 ? C_DIM : C_LINE;
        for (i = 1; i < 240u; i++) {
            cv_line((int32_t)i - 1, vs.snap[k][i - 1], (int32_t)i, vs.snap[k][i], c);
            if (!k)
                cv_line((int32_t)i - 1, vs.snap[k][i - 1] + 1, (int32_t)i, vs.snap[k][i] + 1, c);   /* (thick) */
        }
    }
}

static void vis_sonar(void)
{
    uint32_t i, sweep = song.playing ? ((clk_beat % 4u) * 256u + clk_pos / (BEAT_U / 256u)) : vs.frame * 4u;
    int32_t px = 0, py = 0, r0 = 70, rp;
    cv_line(120, 20, 120, 220, C_LINE);
    cv_line(20, 120, 220, 120, C_LINE);
    for (i = 0; i < 64u; i++) {                         /* the rings */
        uint32_t a = i * 16u;
        cv_pset(120 + vcos(a) * 50 / 32768, 120 + vsin(a) * 50 / 32768, C_LINE);
        cv_pset(120 + vcos(a) * 100 / 32768, 120 + vsin(a) * 100 / 32768, C_LINE);
    }
    for (i = 0; i < 24u; i++) {                         /* the sweep and its fading wake */
        uint32_t a = (sweep - i * 6u) & 1023u;
        cv_line(120, 120, 120 + vcos(a - 256u) * 104 / 32768, 120 + vsin(a - 256u) * 104 / 32768,
                i == 0 ? C_HI : i < 8u ? C_DIM : C_LINE);
    }
    rp = vs.ping < 20u ? 20 + (int32_t)vs.ping * 5 : 0;  /* the beat's ping */
    if (rp)
        for (i = 0; i < 128u; i++)
            cv_pset(120 + vcos(i * 8u) * rp / 32768, 120 + vsin(i * 8u) * rp / 32768, vs.ping < 8u ? C_WHITE : C_DIM);
    for (i = 0; i <= 256u; i++) {                       /* the wave around the circle */
        int32_t v = vs.wave[i & 255u], r = r0 + clamp(v * 40 / vs.scale, -40, 40);
        int32_t x = 120 + vcos(i * 4u - 256u) * r / 32768, y = 120 + vsin(i * 4u - 256u) * r / 32768;
        if (i)
            cv_line(px, py, x, y, C_HI);
        px = x;
        py = y;
    }
}

static void vis_vu(void)
{
    static const char *const L[5] = {"T1", "T2", "T3", "T4", "MIX"};
    uint32_t k, s;
    for (k = 0; k < 5u; k++) {
        int32_t x = 10 + (int32_t)k * 45, lit = vs.vu[k] * 20 / 1000, pk = vs.pk[k] * 20 / 1000;
        for (s = 0; s < 20u; s++) {                     /* 20 segments, the top ones hotter */
            int32_t y = 200 - (int32_t)s * 9;
            uint16_t on = s >= 17u ? C_WHITE : s >= 12u ? C_HI : C_AMB;
            cv_rect(x, y, 36, 6, (int32_t)s < lit ? on : (int32_t)s == pk && pk ? C_HI : C_LINE);
        }
        cv_text(x + 18 - text_w(&FONT_S, L[k]) / 2, 212, &FONT_S, L[k], k == 4u ? C_HI : C_GRAY);
    }
}

static void vis_esteira(void)
{
    uint32_t k, i;
    for (k = 0; k < 4u; k++) {
        int32_t base = 40 + (int32_t)k * 52;
        cv_rect(0, base + 24, 240, 1, C_LINE);
        static const char *const T[4] = {"T1", "T2", "T3", "T4"};
        cv_text(2, base - 16, &FONT_S, k == TRK_DRUM && is_drum(TDRUM) ? "DR" : T[k], C_DIM);
        for (i = 0; i < VIS_HIST; i++) {                /* the newest on the right */
            uint32_t idx = (vs.hw + 1u + i) % VIS_HIST;
            int32_t h = vs.hist[k][idx] * 24 / 250;
            if (h > 0)
                cv_rect((int32_t)i * 4, base + 24 - h, 3, 2 * h, i + 1u == VIS_HIST ? C_WHITE : i > VIS_HIST - 8u ? C_HI : C_DIM);
        }
    }
}

static void vis_mar(void)
{
    static const uint8_t SPD[4] = {3, 5, 7, 4}, LEN[4] = {5, 3, 2, 4};
    uint32_t k, x;
    int32_t top[240], sea = 150, rock, rx, ry;
    for (x = 0; x < 240u; x++)
        top[x] = 240;
    for (k = 0; k < 4u; k++) {                          /* the swells, far (top) to near; each track's level */
        int32_t amp = 3 + vs.lvl[k] * 18 / 1000, y0 = sea - 30 + (int32_t)k * 22, py = 0;
        uint16_t c = k == 3u ? C_HI : k == 2u ? C_AMB : k == 1u ? C_DIM : C_LINE;
        for (x = 0; x < 240u; x++) {
            int32_t y = y0 + vsin(x * LEN[k] * 2u + vs.frame * SPD[k]) * amp / 32768;
            if (x)
                cv_line((int32_t)x - 1, py, (int32_t)x, y, c);
            if (k == 0u)
                top[x] = y;
            py = y;
        }
    }
    for (x = 0; x < 240u; x += 6u)                      /* the stars */
        if (((x * 2654435761u) >> 27) < 3u)
            cv_pset((int32_t)x, 10 + (int32_t)((x * 97u) % 60u), (vs.frame + x) % 50u < 25u ? C_DIM : C_LINE);
    /* the raft: on the far swell at x 120, rocked by the mix (and its slope) */
    rx = 120;
    ry = top[rx] - 4;
    rock = (top[rx + 12] - top[rx - 12]) * 2 + vsin(vs.frame * 6u) * vs.lvl[4] / 1000 * 6 / 32768;
    {
        int32_t dx = 22, dy = rock * dx / 48, mx = rx - rock / 3, i;
        for (i = -2; i <= 2; i++)                       /* the deck: logs */
            cv_line(rx - dx, ry + dy + i, rx + dx, ry - dy + i, i ? C_HI : C_WHITE);
        cv_line(rx, ry, mx, ry - 40, C_WHITE);          /* the mast */
        cv_line(mx, ry - 38, mx + 20 + vs.lvl[4] / 100, ry - 14, C_HI);   /* the sail, filled by the mix */
        cv_line(mx, ry - 14, mx + 20 + vs.lvl[4] / 100, ry - 14, C_HI);
        for (i = 0; i < 24; i++)
            cv_line(mx, ry - 38 + i, mx + (20 + vs.lvl[4] / 100) * i / 24, ry - 38 + i, C_AMB);
    }
}

static void vis_draw(void)
{
    uint32_t band;
    ly.shown = 1;                                       /* (a full screen: layers_draw clears it for the next one) */
    if (!ui.force && (ui.frame & 1u))
        return;
    vis_take();
    if (ui.force)
        lcd_fill(0, 0, 240, 240, C_BG);
    for (band = 0; band < 2u; band++) {
        cv_begin(240, 120, C_BG);
        cv_oy = -(int32_t)band * 120;
        switch (vis_style % VIS_N) {
        case 0: vis_osc(); break;
        case 1: vis_sonar(); break;
        case 2: vis_vu(); break;
        case 3: vis_esteira(); break;
        default: vis_mar(); break;
        }
        if (vis_name_t && band == 0u) {                 /* the style's name, a moment */
            const char *n = VIS_NAME[vis_style % VIS_N];
            int32_t w = text_w(&FONT_M, n) + 20;
            cv_rrect(120 - w / 2, 4, w, 30, 8, C_SURF, C_BG);
            cv_text(120 - text_w(&FONT_M, n) / 2, 7, &FONT_M, n, C_WHITE);
        }
        cv_oy = 0;
        cv_blit(0, band * 120u);
    }
    if (vis_name_t)
        vis_name_t--;
    ui.force = 0;
}
