#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""The UI palettes (run by tools/build.py generate()). Jangada, after Felucca 1.0's tool of that name.

  gen_ui_palettes.py OUT.h [--report]

A palette is five colours by role: BG (background), SURF (surface: the value cards, the graph, menu
rows), TEXT (values, curves: the brightest of the hue), THEME (the identity colour: secondary text,
meters) and ACCENT (what is touched or where we are; white in every Jangada palette). The firmware
derives the rest at palette_set() time (firmware/src/gfx.c, the same integer maths as mix() here):
  LINE  = mix(BG, THEME, 30 %)    1 px rules, empty steps, the gauge track
  DIM   = mix(BG, THEME, 52 %)    inactive, units
  MID   = mix(BG, TEXT, 72 %)     labels
  SEL   = mix(BG, THEME, 42 %)    a selected row or tile (white text on it)
  RAISE = mix(SURF, TEXT, 16 %)   a raised area on a card: the gauge track, chips
The old five steps of one colour (C_LINE C_DIM C_GRAY C_AMB C_HI) are LINE DIM MID THEME TEXT, so the
graphs keep their colours. MONO is pure grayscale (R = G = B) in every colour and every derived tint.
The order is the stored settings.palette index (CHOQUE, index 5, is the default; new ones are appended).
--report prints the WCAG contrast of every pairing. The tool fails (exit 1) on any miss.
"""
import argparse
import sys
from pathlib import Path

# name, bg, surf, text, theme, accent (8-bit RGB; stored as RGB565)
PALETTES = [
    ("GREEN",  (4, 14, 7),    (16, 42, 24),   (150, 255, 172), (56, 200, 92),  (255, 255, 255)),
    ("AMBER",  (16, 10, 2),   (52, 34, 12),   (255, 204, 120), (232, 128, 16), (255, 255, 255)),
    ("CYAN",   (2, 10, 18),   (12, 36, 56),   (170, 232, 255), (56, 172, 222), (255, 255, 255)),
    ("RED",    (18, 4, 4),    (60, 18, 16),   (255, 170, 152), (232, 72, 54),  (255, 255, 255)),
    ("MONO",   (10, 10, 10),  (38, 38, 38),   (232, 232, 232), (176, 176, 176), (255, 255, 255)),
    ("CHOQUE", (10, 2, 7),    (50, 18, 40),   (255, 150, 230), (255, 20, 170), (255, 255, 255)),   # rosa-choque
    # Jangada 0.7, after Felucca 1.0.2: true black, green-tinted text and lines (the 0.9 look); appended
    ("NIGHT",  (0, 0, 0),     (2, 34, 15),    (150, 230, 170), (56, 220, 100), (255, 255, 255)),
]
MONO = "MONO"
PCT = {"LINE": 30, "DIM": 52, "MID": 72, "SEL": 42}
RAISE_PCT = 16                     # SURF -> TEXT


def to565(c):
    r, g, b = c
    if r == g == b:                     # a gray stays on the RGB565 gray axis (G = 2 R)
        return ((r >> 3) << 11) | ((r >> 3) << 6) | (r >> 3)
    return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)


def from565(v):
    return ((v >> 11) * 255 // 31, ((v >> 5) & 63) * 255 // 63, (v & 31) * 255 // 31)


def cdiv(a, b):
    return a // b if a >= 0 else -((-a) // b)


def mix(a, b, pct, mono=False):
    """firmware/src/gfx.c mix565: a + (b - a) * pct / 100 per channel, rounded; MONO on the 5-bit red channel"""
    if mono:
        x, y = a >> 11, b >> 11
        d = (y - x) * pct
        v = x + cdiv(d + (50 if d >= 0 else -50), 100)
        return (v << 11) | (v << 6) | v
    out = 0
    for sh, mask in ((11, 31), (5, 63), (0, 31)):
        x, y = (a >> sh) & mask, (b >> sh) & mask
        d = (y - x) * pct
        out |= (x + cdiv(d + (50 if d >= 0 else -50), 100)) << sh
    return out


def lum(c565):
    def lin(v):
        v /= 255.0
        return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4
    r, g, b = from565(c565)
    return 0.2126 * lin(r) + 0.7152 * lin(g) + 0.0722 * lin(b)


def contrast(a, b):
    la, lb = lum(a), lum(b)
    return (max(la, lb) + 0.05) / (min(la, lb) + 0.05)


def derive(p):
    name, *cols = p
    bg, surf, text, theme, accent = map(to565, cols)
    mono = name == MONO
    return name, dict(bg=bg, surf=surf, text=text, theme=theme, accent=accent,
                      line=mix(bg, theme, PCT["LINE"], mono), dim=mix(bg, theme, PCT["DIM"], mono),
                      mid=mix(bg, text, PCT["MID"], mono), sel=mix(bg, theme, PCT["SEL"], mono),
                      raise_=mix(surf, text, RAISE_PCT, mono))


# (foreground, background, minimum contrast, why)
CHECKS = [("text", "bg", 10.0, "values"), ("text", "surf", 7.0, "values on a card"),
          ("mid", "bg", 5.0, "labels"), ("mid", "surf", 4.0, "labels on a card"),
          ("theme", "bg", 4.5, "secondary text, curves"), ("theme", "surf", 3.5, "secondary text on a card"),
          ("accent", "surf", 9.0, "the touched value"), ("accent", "sel", 4.5, "text on a selection"),
          ("text", "sel", 3.0, "a value on a selection"), ("text", "raise_", 5.0, "text on a chip"),
          ("dim", "bg", 2.0, "inactive (not read)"), ("dim", "surf", 1.4, "inactive on a card"),
          ("line", "bg", 1.4, "1 px rules"), ("surf", "bg", 1.2, "a card on the background"),
          ("raise_", "surf", 1.25, "the gauge track on a card"), ("bg", "accent", 15.0, "black ink on white")]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    fails = []
    rows = [derive(p) for p in PALETTES]
    for name, d in rows:
        if name == MONO:
            for k, v in d.items():
                if not (v >> 11 == v & 31 and ((v >> 5) & 63) == (v >> 11) << 1):
                    fails.append(f"MONO is not grayscale: {k} = {v:#06x}")
        for f, b, lim, why in CHECKS:
            c = contrast(d[f], d[b])
            if c < lim:
                fails.append(f"{name}: {f}/{b} {c:.2f} < {lim} ({why})")
    if a.report:
        print(f"{'palette':8s} " + " ".join(f"{f}/{b}".ljust(12) for f, b, _, _ in CHECKS))
        for name, d in rows:
            print(f"{name:8s} " + " ".join(f"{contrast(d[f], d[b]):5.1f}".ljust(12) for f, b, _, _ in CHECKS))
        print("minimum  " + " ".join(f"{lim:<12.2f}" for _, _, lim, _ in CHECKS))
    if a.out:
        out = ["/* generated by tools/gen_ui_palettes.py: the UI palettes (5 colours by role each, the rest derived) */",
               "#pragma once", "#include <stdint.h>", "",
               "/* ui_pal_t {name, bg, surf, text, theme, accent} (firmware/src/gfx.c) */",
               "static const ui_pal_t PALETTES[] = {"]
        for p in PALETTES:
            v = [to565(c) for c in p[1:]]
            out.append(f'    {{"{p[0]}", ' + ", ".join(f"0x{x:04x}" for x in v) + "},")
        out += ["};", f"#define UI_MONO_INDEX {[p[0] for p in PALETTES].index(MONO)}u"]
        out += [f"#define UI_{k}_PCT {v}" for k, v in PCT.items()] + [f"#define UI_RAISE_PCT {RAISE_PCT}   /* SURF -> TEXT */", ""]
        Path(a.out).write_text("\n".join(out))
    for f in fails:
        print("FAIL", f)
    if fails:
        sys.exit(1)
    if a.out:
        print(f"palettes: {len(PALETTES)}, contrast checks passed -> {a.out}")


if __name__ == "__main__":
    main()
