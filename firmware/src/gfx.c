/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Small-canvas renderer (no full framebuffer). Draw text/lines into
 * an off-screen strip, then blit it in one DMA transfer. Pixels are stored
 * byte-swapped (the panel takes RGB565 big-endian).
 * Jangada (after Felucca 1.0): text is Inter Tight, pre-rasterised to 4-bit alpha at build time
 * (tools/gen_aa_font.py --preset jangada): a glyph is trimmed to its ink box, advances are in 1/16 px with
 * kerning, and each glyph is rasterised at 1 << psh horizontal phases (S M 4, L 2); cv_text takes the phase
 * nearest its true place. The partly covered pixels blend into what lies under them (a card, a selection,
 * the background), through a 16-entry ramp per (ink, under) pair, cached. The colours are roles of the
 * palette (tools/gen_ui_palettes.py); cards are rounded rectangles with anti-aliased corners (cv_rrect). */
typedef struct { uint16_t off, adv; int8_t bx; uint8_t by, bw, bh; } aag_t;
typedef struct {
    uint8_t h, asc, first, last, nex;   /* ink line height, baseline; ASCII range; extras */
    uint16_t nk;                        /* kerning pairs */
    const aag_t *g;
    const uint8_t *data;
    const uint16_t *kkey;               /* a << 8 | b, sorted */
    const int8_t *kd;                   /* 1/16 px */
    const uint8_t *ex;                  /* codes of the extra glyphs */
    uint8_t psh;                        /* log2 of the phases per glyph: g[i << psh] .. g[(i << psh) + phases - 1] */
    const uint8_t *hc;                  /* data's Huffman code (cv_alpha_hc), 0 = 2 px per byte (cv_alpha) */
    uint8_t box, base;                  /* the layout's line: box rows, the baseline on row base */
} aafont_t;
typedef aafont_t felucca_font_t;
typedef struct { const char *name; uint16_t bg, surf, text, theme, accent; } ui_pal_t;
#include "ui_fonts.h"                   /* AF_S 13 px / 500, AF_M 15 px / 600, AF_L 24 px / 600 */
#include "ui_palettes.h"
#define FONT_S AF_S                     /* labels, units, lists, messages (16 px line) */
#define FONT_M AF_M                     /* values (16 px line) */
#define FONT_L AF_L                     /* titles, the focus value (32 px line) */

#define CV_MAX (240u * 124u)      /* the graph strip is 240 x 124 */
static uint16_t cv_px[CV_MAX] __attribute__((section(".pool")));
static uint32_t cv_w, cv_h;
static int32_t cv_oy;            /* y offset for graph drawing */
static int32_t cv_cx0, cv_cy0, cv_cx1, cv_cy1;   /* clip (canvas pixels), cv_begin: the whole canvas */

#define RGB(r, g, b) ((uint16_t)((((r) >> 3) << 11) | (((g) >> 2) << 5) | ((b) >> 3)))
#define C_BLACK 0x0000u              /* one-shot screens (boot, crash, update), ink on white */
#define C_WHITE 0xFFFFu              /* accent only: what is being touched / where we are */
/* The palette's roles (picked in the HOME-hold menu, COLOR) and the old five steps of one colour */
#define NPALETTES (sizeof(PALETTES) / sizeof(PALETTES[0]))
static uint16_t pal[5];
static struct {
    uint16_t bg, surf, sel, raise;
    uint8_t mono;
    uint32_t gen;                    /* bumped by palette_set (the text ramps follow) */
} ux;
#define C_LINE pal[0]                /* 1 rules, separators, empty steps */
#define C_DIM pal[1]                 /* 2 inactive, units */
#define C_GRAY pal[2]                /* 3 labels */
#define C_AMB pal[3]                 /* 4 secondary text (the theme colour) */
#define C_HI pal[4]                  /* 5 values, curves */
#define C_BG ux.bg                   /* the background */
#define C_SURF ux.surf               /* cards: values, the graph, menu rows */
#define C_SEL ux.sel                 /* a selected row or tile */
#define C_RAISE ux.raise             /* raised on a card: the gauge track, chips */

static inline uint16_t gray565(uint32_t x5) { return (uint16_t)((x5 << 11) | (x5 << 6) | x5); }
/* a + (b - a) * pct / 100 per channel, rounded (tools/gen_ui_palettes.py mix); MONO on the red channel */
static uint16_t mix565(uint16_t a, uint16_t b, int32_t pct)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    uint32_t out = 0, k;
    if (ux.mono) {
        int32_t x = a >> 11, d = ((b >> 11) - x) * pct;
        return gray565((uint32_t)(x + (d + (d >= 0 ? 50 : -50)) / 100));
    }
    for (k = 0; k < 3u; k++) {
        int32_t x = (a >> SH[k]) & MK[k], d = (((b >> SH[k]) & MK[k]) - x) * pct;
        out |= (uint32_t)(x + (d + (d >= 0 ? 50 : -50)) / 100) << SH[k];
    }
    return (uint16_t)out;
}

static void palette_set(uint32_t i)
{
    const ui_pal_t *p = &PALETTES[i % NPALETTES];
    ux.mono = i % NPALETTES == UI_MONO_INDEX;
    ux.bg = p->bg;
    ux.surf = p->surf;
    pal[0] = mix565(p->bg, p->theme, UI_LINE_PCT);
    pal[1] = mix565(p->bg, p->theme, UI_DIM_PCT);
    pal[2] = mix565(p->bg, p->text, UI_MID_PCT);
    pal[3] = p->theme;
    pal[4] = p->text;
    ux.sel = mix565(p->bg, p->theme, UI_SEL_PCT);
    ux.raise = mix565(p->surf, p->text, UI_RAISE_PCT);
    ux.gen++;
}

static inline uint16_t swap16(uint32_t c) { return (uint16_t)(((c >> 8) & 0xFFu) | ((c & 0xFFu) << 8)); }

static void cv_begin(uint32_t w, uint32_t h, uint16_t bg)
{
    uint32_t i, n;
    uint16_t s = swap16(bg);
    if (w * h > CV_MAX)
        h = CV_MAX / w;
    lcd_sync();                     /* the last blit may still read cv_px */
    cv_w = w;
    cv_h = h;
    cv_cx0 = cv_cy0 = 0;
    cv_cx1 = (int32_t)w;
    cv_cy1 = (int32_t)h;
    n = w * h;
    for (i = 0; i < n; i++)
        cv_px[i] = s;
}

/* drawing (pixels, rects, lines, text) only inside canvas pixels x0..x1-1, y0..y1-1 */
static void cv_clip(int32_t x0, int32_t y0, int32_t x1, int32_t y1)
{
    cv_cx0 = x0 < 0 ? 0 : x0;
    cv_cy0 = y0 < 0 ? 0 : y0;
    cv_cx1 = x1 > (int32_t)cv_w ? (int32_t)cv_w : x1;
    cv_cy1 = y1 > (int32_t)cv_h ? (int32_t)cv_h : y1;
}
static void cv_noclip(void) { cv_clip(0, 0, (int32_t)cv_w, (int32_t)cv_h); }

static void cv_blit(uint32_t x, uint32_t y) { lcd_blit(x, y, cv_w, cv_h, cv_px); }

/* canvas rows r0 .. cv_h-1 only, to screen row y + r0 */
static void cv_blit_from(uint32_t x, uint32_t y, uint32_t r0)
{
    if (r0 < cv_h)
        lcd_blit(x, y + r0, cv_w, cv_h - r0, cv_px + r0 * cv_w);
}

static inline void cv_pset(int32_t x, int32_t y, uint16_t c)
{
    y += cv_oy;
    if (x >= cv_cx0 && x < cv_cx1 && y >= cv_cy0 && y < cv_cy1)
        cv_px[(uint32_t)y * cv_w + (uint32_t)x] = swap16(c);
}

static void cv_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t c)
{
    int32_t x1 = x + w, y1 = y + h + cv_oy, i;
    uint16_t sc = swap16(c);
    y += cv_oy;                             /* clipped once, then filled row by row */
    if (x < cv_cx0)
        x = cv_cx0;
    if (y < cv_cy0)
        y = cv_cy0;
    if (x1 > cv_cx1)
        x1 = cv_cx1;
    if (y1 > cv_cy1)
        y1 = cv_cy1;
    for (; y < y1; y++)
        for (i = x; i < x1; i++)
            cv_px[(uint32_t)y * cv_w + (uint32_t)i] = sc;
}

static void cv_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint16_t c)
{
    int32_t dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int32_t dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int32_t err = dx + dy, guard = 2000;
    while (guard--) {                       /* bounded: a line is never longer than 480 px */
        int32_t e2 = 2 * err;               /* both tests use the same error value */
        cv_pset(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

/* coverage (0..16) of corner pixel (i, j) of a radius-r corner, (0, 0) the outermost: 4 x 4 samples */
static uint32_t rr_cov(int32_t r, int32_t i, int32_t j)
{
    int32_t sx, sy, n = 0;
    for (sy = 0; sy < 4; sy++)
        for (sx = 0; sx < 4; sx++) {
            int32_t dx = r * 8 - ((i * 4 + sx) * 2 + 1), dy = r * 8 - ((j * 4 + sy) * 2 + 1);   /* 1/8 px */
            n += dx * dx + dy * dy <= r * r * 64;
        }
    return (uint32_t)n;
}
/* a filled rectangle with rounded corners (r <= 8), the corners anti-aliased against `under` (what lies
 * behind it): the cards, rows, tiles and badges (Felucca 1.0's flat look) */
static void cv_rrect(int32_t x, int32_t y, int32_t w, int32_t h, int32_t r, uint16_t c, uint16_t under)
{
    int32_t i, j;
    if (r > w / 2)
        r = w / 2;
    if (r > h / 2)
        r = h / 2;
    if (r < 1) {
        cv_rect(x, y, w, h, c);
        return;
    }
    cv_rect(x + r, y, w - 2 * r, h, c);
    cv_rect(x, y + r, r, h - 2 * r, c);
    cv_rect(x + w - r, y + r, r, h - 2 * r, c);
    for (j = 0; j < r; j++)
        for (i = 0; i < r; i++) {
            uint32_t a = rr_cov(r, i, j);
            uint16_t px = a >= 16u ? c : a == 0u ? under : mix565(under, c, (int32_t)(a * 100u / 16u));
            cv_pset(x + i, y + j, px);
            cv_pset(x + w - 1 - i, y + j, px);
            cv_pset(x + i, y + h - 1 - j, px);
            cv_pset(x + w - 1 - i, y + h - 1 - j, px);
        }
}
/* a card on the background */
static void cv_card(int32_t x, int32_t y, int32_t w, int32_t h) { cv_rrect(x, y, w, h, 6, C_SURF, C_BG); }

/* ---- Jangada 0.9.4: anti-aliased circles (the big values' indicators, ui_draw.c graph_big; issue #5).
 * Coordinates and radii in 1/16 px; each pixel is 4 x 4 samples, blended over what is already drawn. */
static void cv_blend(int32_t x, int32_t y, uint16_t c, uint32_t a16)        /* a16: coverage 0..16 */
{
    uint16_t *p;
    y += cv_oy;
    if (!a16 || x < cv_cx0 || x >= cv_cx1 || y < cv_cy0 || y >= cv_cy1)
        return;
    p = &cv_px[(uint32_t)y * cv_w + (uint32_t)x];
    *p = swap16(a16 >= 16u ? c : mix565(swap16(*p), c, (int32_t)(a16 * 100u / 16u)));
}

/* the ring r0 <= d < r1 around (cx, cy); r0 = 0: a disc */
static void cv_annulus(int32_t cx, int32_t cy, int32_t r0, int32_t r1, uint16_t c)
{
    int32_t x, y, x0 = (cx - r1) >> 4, x1 = (cx + r1) >> 4, y0 = (cy - r1) >> 4, y1 = (cy + r1) >> 4;
    int32_t q0 = r0 > 0 ? r0 * r0 : -1, q1 = r1 * r1;
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++) {
            uint32_t sx, sy, n = 0;
            for (sy = 0; sy < 4u; sy++)
                for (sx = 0; sx < 4u; sx++) {
                    int32_t dx = x * 16 + 2 + (int32_t)sx * 4 - cx, dy = y * 16 + 2 + (int32_t)sy * 4 - cy, q = dx * dx + dy * dy;
                    n += q >= q0 && q < q1;
                }
            cv_blend(x, y, c, n);
        }
}
static void cv_disc(int32_t cx, int32_t cy, int32_t r, uint16_t c) { cv_annulus(cx, cy, 0, r, c); }
static void cv_ring(int32_t cx, int32_t cy, int32_t r, int32_t w, uint16_t c) { cv_annulus(cx, cy, r - w / 2, r + w / 2, c); }

/* ------------------------------------------------- 4-bit alpha blit --- */
/* coverage curve for light ink on a dark ground (Felucca 1.0 CURVE_DARK); dark ink: linear */
static const uint8_t CURVE_DARK[16] = {0, 24, 43, 62, 80, 97, 114, 130, 147, 163, 178, 194, 210, 225, 240, 255};
#define NRAMP 6u
static struct {
    uint16_t fg[NRAMP], bg[NRAMP], v[NRAMP][16];   /* v: byte-swapped panel values */
    uint32_t gen;
    uint8_t next, n;
} rc;

static const uint16_t *ramp(uint16_t fg, uint16_t bg)
{
    static const uint8_t SH[3] = {11, 5, 0}, MK[3] = {31, 63, 31};
    uint32_t i, a, k, light = (fg >> 11) < (bg >> 11) && ((fg >> 5) & 63u) < ((bg >> 5) & 63u);
    uint16_t *v;
    if (rc.gen != ux.gen) {
        rc.gen = ux.gen;
        rc.n = rc.next = 0;
    }
    for (i = 0; i < rc.n; i++)
        if (rc.fg[i] == fg && rc.bg[i] == bg)
            return rc.v[i];
    i = rc.next;
    rc.next = (uint8_t)((i + 1u) % NRAMP);
    if (rc.n < NRAMP)
        rc.n++;
    rc.fg[i] = fg;
    rc.bg[i] = bg;
    v = rc.v[i];
    for (a = 0; a < 16u; a++) {
        uint32_t out = 0, cv = light ? a * 17u : CURVE_DARK[a];
        for (k = 0; k < 3u; k++) {
            int32_t x = (bg >> SH[k]) & MK[k], y = (fg >> SH[k]) & MK[k];
            out |= (uint32_t)(x + (((y - x) * (int32_t)cv + 128) >> 8)) << SH[k];
        }
        v[a] = swap16(out);
    }
    return v;
}

/* one alpha value onto a canvas pixel: 15 is the ink, 1..14 blend with what is there (its ramp is kept
 * while the pixels under the text stay the same colour, which they nearly always do) */
static struct { uint16_t fg, under; const uint16_t *rv; } ap;
static inline void ap_put(uint16_t *px, uint32_t v)
{
    if (v >= 15u) {
        *px = ap.rv[15];
    } else if (v) {
        if (*px != ap.under) {
            ap.under = *px;
            ap.rv = ramp(ap.fg, swap16(*px));
        }
        *px = ap.rv[v];
    }
}
static void ap_begin(uint16_t fg)
{
    ap.fg = fg;
    ap.under = swap16(C_BG);
    ap.rv = ramp(fg, C_BG);
}

/* w x h nibbles, rows back to back, at canvas (x, y) */
static void cv_alpha(int32_t x, int32_t y, uint32_t w, uint32_t h, const uint8_t *d)
{
    uint32_t k = 0, gx, gy;
    y += cv_oy;
    for (gy = 0; gy < h; gy++) {
        int32_t py = y + (int32_t)gy;
        uint16_t *row;
        if (py < cv_cy0 || py >= cv_cy1) {
            k += w;
            continue;
        }
        row = cv_px + (uint32_t)py * cv_w;
        for (gx = 0; gx < w; gx++, k++) {
            uint32_t v = (d[k >> 1] >> ((k & 1u) ? 0 : 4)) & 15u;
            int32_t px = x + (int32_t)gx;
            if (v && px >= cv_cx0 && px < cv_cx1)
                ap_put(row + px, v);
        }
    }
}

/* the same from a glyph's Huffman-coded bit stream (tools/gen_aa_font.py huff_pack): canonical codes, MSB
 * first; hc = the number of codes of each length 1..15, then the symbols in code order, each a value and
 * its repeat count ((count - 1) << 4 | value: runs of zeros and 15s). Codes of up to 8 bits come from a
 * 256-entry table made once per font in RAM (symbol | length << 8; 0 = a longer code: bit by bit). The
 * stream is read in order, so rows outside the clip are decoded and dropped; a run spans rows */
#define HC_FONTS 2                       /* the coded faces: M and L (gen_aa_font.py HUFF) */
static struct { const uint8_t *hc; uint16_t lut[256]; } hc_tab[HC_FONTS];
static const uint16_t *hc_lut(const uint8_t *hc)
{
    uint32_t k, len, code = 0, sym = 15, i;
    for (k = 0; k < HC_FONTS - 1u && hc_tab[k].hc && hc_tab[k].hc != hc; k++)
        ;
    if (hc_tab[k].hc != hc) {                        /* (more fonts than slots: the last slot is rebuilt) */
        hc_tab[k].hc = hc;
        for (i = 0; i < 256u; i++)
            hc_tab[k].lut[i] = 0;
        for (len = 1; len <= 8u; len++, code <<= 1)
            for (i = 0; i < hc[len - 1u]; i++, code++, sym++) {
                uint32_t a = code << (8u - len), n = 1u << (8u - len);
                while (n--)
                    hc_tab[k].lut[a + n] = (uint16_t)(hc[sym] | len << 8);
            }
    }
    return hc_tab[k].lut;
}

static void cv_alpha_hc(int32_t x, int32_t y, uint32_t w, uint32_t h, const uint8_t *d, const uint8_t *hc)
{
    const uint16_t *lut = hc_lut(hc);
    uint32_t gy, v = 0, run = 0, acc = 0, nb = 0;
    y += cv_oy;
    for (gy = 0; gy < h; gy++) {
        int32_t py = y + (int32_t)gy;
        uint16_t *row = py < cv_cy0 || py >= cv_cy1 ? 0 : cv_px + (uint32_t)py * cv_w;
        uint32_t gx = 0;
        while (gx < w) {
            uint32_t n;
            if (!run) {                              /* the next symbol */
                uint32_t e, s;
                while (nb < 16u) {                   /* (the data ends with 2 spare bytes) */
                    acc = acc << 8 | *d++;
                    nb += 8;
                }
                e = lut[(acc >> (nb - 8u)) & 255u];
                if (e) {
                    nb -= e >> 8;
                    s = e & 255u;
                } else {                             /* a code over 8 bits */
                    uint32_t code = 0, first = 0, idx = 0, len = 0;
                    for (;;) {
                        code |= (acc >> --nb) & 1u;
                        if (code - first < hc[len])
                            break;
                        idx += hc[len];
                        first = (first + hc[len]) << 1;
                        code <<= 1;
                        len++;
                    }
                    s = hc[15u + idx + code - first];
                }
                v = s & 15u;
                run = (s >> 4) + 1u;
            }
            n = run < w - gx ? run : w - gx;
            if (v && row) {
                int32_t px = x + (int32_t)gx;
                uint32_t k;
                for (k = 0; k < n; k++, px++)
                    if (px >= cv_cx0 && px < cv_cx1)
                        ap_put(row + px, v);
            }
            gx += n;
            run -= n;
        }
    }
}

/* ------------------------------------------------------------- text --- */
/* the glyph number of ch in f, -1 = not in the face */
static int32_t glyph_at(const aafont_t *f, uint32_t ch)
{
    uint32_t i;
    if (ch >= f->first && ch <= f->last)
        return (int32_t)(ch - f->first);
    for (i = 0; i < f->nex; i++)
        if (f->ex[i] == ch)
            return (int32_t)(f->last - f->first + 1u + i);
    return -1;
}

static uint32_t fold(const aafont_t *f, uint32_t c) { return c >= 'a' && c <= 'z' && f->last < 'a' ? c - 32u : c; }

/* the table entry of ch at phase 0 (its other phases follow it); not in the face: '?' */
static uint32_t glyph(const aafont_t *f, uint32_t ch)
{
    int32_t k;
    if ((k = glyph_at(f, fold(f, ch))) < 0 && (k = glyph_at(f, '?')) < 0)
        k = 0;
    return (uint32_t)k << f->psh;
}

static int32_t kern(const aafont_t *f, uint32_t a, uint32_t b)       /* 1/16 px */
{
    uint32_t key = (a << 8) | b, lo = 0, hi = f->nk;
    while (lo < hi) {
        uint32_t m = (lo + hi) / 2u;
        if (f->kkey[m] == key)
            return f->kd[m];
        if (f->kkey[m] < key)
            lo = m + 1u;
        else
            hi = m;
    }
    return 0;
}

static int32_t text_w(const aafont_t *f, const char *s)
{
    int32_t pen = 0;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t c = fold(f, (uint8_t)*s);
        if (prev)
            pen += kern(f, prev, c);
        pen += f->g[glyph(f, c)].adv;
        prev = c;
    }
    return (pen + 8) >> 4;
}

/* text in colour c; y = the top of the layout's line (the baseline is y + f->base); returns the end x.
 * Placement: the pen (1/16 px) lies between two phase positions a step (16 >> psh) apart; the glyph takes
 * the one whose offset from the pen is nearer the previous glyph's offset, so the space between neighbours
 * stays truest (Felucca 1.0). A glyph with one phase (figures and .) sits at the rounded pen. */
static int32_t cv_text(int32_t x, int32_t y, const aafont_t *f, const char *s, uint16_t c)
{
    int32_t pen = 0, err = 0, yb = y + (int32_t)f->base - (int32_t)f->asc;
    const int32_t step = 16 >> f->psh;
    uint32_t prev = 0;
    ap_begin(c);
    for (; *s; s++) {
        uint32_t ch = fold(f, (uint8_t)*s);
        const aag_t *g;
        int32_t pos, d0, d1;
        if (prev)
            pen += kern(f, prev, ch);
        g = &f->g[glyph(f, ch)];
        if (f->psh && g[1].off != g->off) {
            pos = pen - (pen & (step - 1));          /* the phase position at or left of the pen */
            d0 = pos - pen - err;                    /* .. and the one right of it: which keeps the gap truer */
            d1 = d0 + step;
            if ((d0 < 0 ? -d0 : d0) > (d1 < 0 ? -d1 : d1))
                pos += step;
            g += ((uint32_t)pos >> (4u - f->psh)) & ((1u << f->psh) - 1u);
        } else {
            pos = (pen + 8) & ~15;                   /* one phase (figures, ., blanks): the rounded pen */
        }
        err = pos - pen;
        if (g->bw) {
            int32_t gx = x + (pos >> 4) + g->bx, gy = yb + g->by;
            if (f->hc)
                cv_alpha_hc(gx, gy, g->bw, g->bh, f->data + g->off, f->hc);
            else
                cv_alpha(gx, gy, g->bw, g->bh, f->data + g->off);
        }
        pen += g->adv;
        prev = ch;
    }
    return x + ((pen + 8) >> 4);
}

/* one-shot: text in a box, cleared to black (its screens are), blitted */
static void draw_text_box(uint32_t x, uint32_t y, uint32_t w, const felucca_font_t *f, const char *s,
                          uint16_t c, int align)
{
    int32_t tw = text_w(f, s), tx = 0;
    cv_begin(w, f->box, C_BLACK);
    if (align == 1)
        tx = ((int32_t)w - tw) / 2;
    else if (align == 2)
        tx = (int32_t)w - tw;
    cv_text(tx, 0, f, s, c);
    cv_blit(x, y);
    lcd_sync();                     /* one-shots (boot, crash, UBOOT, update) finish here */
}
