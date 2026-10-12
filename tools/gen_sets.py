#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Jangada's own SAMPLE / GRAIN sets, synthesized here at the stored rate (no recordings).

Jangada 0.9.8 (issues #5 / #6): these take the places of the four CC0 melodic sets (PIANO,
TRANH, FLUTE, SAX), in the same order, so a saved SET / SRC keeps its number (USR1..3 and PERC
too). About 50 KB of ADPCM where those were ~180 KB.

  IRON   a struck steel plate: inharmonic modes, each its own decay, two of them beating
  WIRE   a plucked wire, Karplus-Strong with a stiff, slightly clipped string
  TABLE  a wavetable sweep, sine to a buzzing, hollow tone, for GRAIN: POS walks the timbre
  MOTOR  a machine running: a buzzing hum, its gears ticking, the air it moves; it spins up,
         then loops seamlessly (one whole period of the hum, noise and gears)

Each set -> (name, kind, [(int16 samples, loop or None, root as a MIDI note, C4 = 60)]).
A looped sound's loop holds a whole number of the hum's periods, so it needs no crossfade.
"""
import math
import random

TR = 22050


def _norm(x, peak=30000):
    m = max(1e-9, max(abs(v) for v in x))
    return [int(round(v * peak / m)) for v in x]


def _midi(hz):
    return 69 + 12 * math.log2(hz / 440)


def _fade(x, secs):
    n = min(len(x), int(secs * TR))
    for i in range(n):
        x[len(x) - n + i] *= 1 - i / n
    return x


def iron():
    """a steel plate, struck: modes at the ratios of a free plate, the high ones dying first"""
    f0, secs = 130.81, 1.1
    modes = [(1.0, 1.0, 1.6), (1.006, 0.6, 1.4), (2.76, 0.7, 0.9), (5.40, 0.45, 0.5), (8.93, 0.3, 0.3),
             (13.34, 0.2, 0.18), (19.1, 0.12, 0.1)]
    r = random.Random(11)
    ph = [r.random() * 6.283 for _ in modes]
    x, lp = [], 0.0
    for i in range(int(secs * TR)):
        t = i / TR
        s = sum(a * math.sin(2 * math.pi * f0 * k * t + p) * math.exp(-t / d) for (k, a, d), p in zip(modes, ph))
        n = r.uniform(-1, 1)
        lp += 0.35 * (n - lp)
        s += lp * 0.9 * math.exp(-t * 90)          # the strike
        x.append(math.tanh(1.4 * s))
    return [(_norm(_fade(x, 0.1)), None, _midi(f0))]


def wire():
    """Karplus-Strong at A2: the loop filter a little bright (stiff), the output gently clipped"""
    f0, secs = 110.0, 1.0
    n = int(round(TR / f0))
    r = random.Random(5)
    buf = [r.uniform(-1, 1) for _ in range(n)]
    x = []
    for i in range(int(secs * TR)):
        a, b = buf[i % n], buf[(i + 1) % n]
        buf[i % n] = 0.4985 * (a + b) + 0.0015 * (a - b) * 40
        x.append(math.tanh(2.2 * a))
    return [(_norm(_fade(x, 0.08)), None, _midi(TR / n))]


def _cycles(spectrum, period, count, ph):
    """count periods of an additive tone (harmonic: whole periods) -> samples, the phases carried on"""
    out = []
    for i in range(period * count):
        out.append(sum(a * math.sin(2 * math.pi * k * i / period + ph[k]) for k, a in spectrum))
    return out


def table():
    """a sweep through 8 spectra, 0.2 s each, crossfaded: sine, octaves, saw, odd (hollow), a formant,
    a comb (every 3rd missing), a metallic top, a buzz; the last one held and looped"""
    period = 168                                    # 131.25 Hz: whole periods
    K = 40
    specs = [
        {1: 1.0},
        {1: 1.0, 2: 0.5, 4: 0.25},
        {k: 1.0 / k for k in range(1, K)},
        {k: 1.0 / k for k in range(1, K, 2)},
        {k: math.exp(-((k - 7) ** 2) / 6) + 0.3 / k for k in range(1, K)},
        {k: (0 if k % 3 == 0 else 1.0 / math.sqrt(k)) for k in range(1, K)},
        {k: (0.15 / k if k < 9 else 0.5 / math.sqrt(k)) for k in range(1, K)},
        {k: 1.0 / math.sqrt(k) for k in range(1, K)},
    ]
    r = random.Random(3)
    ph = {k: r.random() * 6.283 for k in range(1, K)}
    seg = 26                                        # periods a spectrum (~0.2 s)
    x = []
    for j in range(len(specs)):
        a, b = specs[j], specs[min(j + 1, len(specs) - 1)]
        for c in range(seg):
            w = c / seg if j + 1 < len(specs) else 0.0
            sp = [(k, (1 - w) * a.get(k, 0) + w * b.get(k, 0)) for k in range(1, K)]
            x += _cycles(sp, period, 1, ph)
    loop = (len(x) - seg * period, len(x) - 1)      # the last spectrum, whole periods
    return [(_norm(x), loop, _midi(TR / period))]


def motor():
    """a machine: a hum at 55 Hz (a pulse, its odd harmonics), a gear ticking every 3 periods, the air
    it moves (noise, the same each period). One spin-up, then a loop of 24 periods"""
    period, loopn = 400, 24                         # 55.125 Hz
    r = random.Random(9)
    noise = [r.uniform(-1, 1) for _ in range(period * 3)]
    hum = [sum((1.0 / k) * math.sin(2 * math.pi * k * i / period) for k in (1, 2, 3, 5, 7, 9, 11)) for i in range(period)]
    L = period * loopn
    x, lp = [], 0.0
    for i in range(2 * L):
        p = i % period
        g = i % (3 * period)
        tick = math.exp(-g / 60.0) * math.sin(2 * math.pi * g / 9.0)       # the gear's tooth
        lp += 0.12 * (noise[g] - lp)
        s = 0.9 * hum[p] + 0.55 * tick + 0.35 * lp * (0.6 + 0.4 * math.sin(2 * math.pi * p / period))
        if i < L:                                   # spin-up over the first pass
            s *= (i / L) ** 0.5
        x.append(math.tanh(1.3 * s))
    return [(_norm(x), (L, 2 * L - 1), _midi(TR / period) + 12)]   # rooted an octave up: it sounds an octave
                                                                   # under the key (the FM-1's keys start at F3)


# in the order of the sets they replace (PIANO, TRANH, FLUTE, SAX): a saved SET keeps its number
SETS = [("IRON", "oneshot", iron), ("WIRE", "oneshot", wire), ("TABLE", "sus", table), ("MOTOR", "sus", motor)]
