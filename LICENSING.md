# Felucca licensing

Felucca is free software. Its **code** is licensed under the GNU General Public License,
version 3 only (`GPL-3.0-only`, full text in `LICENSE`). Its **assets** are not part of
that licence: the panel image `docs/panel.jpg` and the drum sounds made by
`tools/gen_waves.py` (the Hügelton Sample Pack) are Copyright (C) 2026 Hügelton Instruments,
all rights reserved. Their licence terms will be published later.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments

## What is code (GPL-3.0-only)

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, editor, Studio) and their tests: `web/` (not the Fukiai font, below)
- the host tests: `tests/`

You may use, study, change and share it under the GPL. If you distribute Felucca, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified Felucca
inside.

## Additional permission (GPL-3.0 section 7)

As an additional permission under GPL-3.0 section 7, you may combine Felucca, or a work
based on it, with the Felucca Assets (above), and convey the combination.
This is allowed even though the Felucca Assets are not licensed under the GPL, provided
that:

- you follow the GPL for every part that is not a Felucca Asset; and
- you follow the terms published for the assets.

The Felucca Assets are data (wavetables, icons, sample data). They are not program
code. A firmware image built from the GPL sources with replacement assets, or with no
assets, is entirely governed by the GPL.

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Instrument samples (Versilian Studios VSCO-2 CE, VCSL) | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |
| Inter Tight (Copyright 2022 The Inter Project Authors, <https://github.com/rsms/inter>): the firmware's UI font, rasterised at build time by `tools/gen_aa_font.py` (after Felucca 1.0) | SIL OFL 1.1 | `assets/fonts/InterTight[wght].ttf`, `assets/fonts/OFL-InterTight.txt`, `LICENSES/OFL-InterTight.txt` |
| Fukiai icon font (Hügelton Instruments): the firmware's parameter icons, rasterised at build time by `tools/gen_aa_icons.py` (after Felucca 1.0), and the web editor | MIT | `assets/fonts/fukiai.ttf` (the version of Felucca 1.0), `web/fukiai.ttf`, `LICENSES/MIT-Fukiai.txt`, `web/FUKIAI-LICENSE.txt` |
| Terminus font 8x16 (ter-u16n): only the README banner (`tools/make_banner.py`); the firmware no longer uses it | SIL OFL 1.1 | `assets/fonts/ter-u16n.bdf`, `assets/fonts/Terminus-LICENSE.txt` |
| CrispyZebra by Leo Kuroshita (<https://github.com/hugelton/CrispyZebra>): the PHASE engine's waveforms are a C port of its oscillator | GPL-3.0 | `firmware/src/eng_phase.c` |
| klattsch by Tony Gies (<https://github.com/tgies/klattsch>): design reference for the VOICE (formant) engine; no code copied. Formant data from Klatt (1980) / Hillenbrand et al. (1995) | MIT (klattsch) | credit only |
| SLOOP 2.2 by isod89 (<https://github.com/isod89/sloop-fm1>), a Felucca fork: the punch-in effects, the master bus (DUST, DUCK, DJ filter), the layers (hold a button), the one-key chords, the TRACKS view, the synthesised drum kits, the PLATE reverb, the autosave, the update checks and the USB rescue are ported or adapted from it | GPL-3.0 | `firmware/src/punch.c`, `firmware/src/fx.c`, `firmware/src/ui_layers.c`, `firmware/src/seq.c` |
| SLOOP 2.3 by isod89 (most of it after Felucca 1.0): the knob decoder and key debounce, the USB-MIDI back-pressure and checks, the overload shedding, the stricter flash checks, the TRS MIDI input read by content (a fix from Felucca [Salt] by ChanceTheMaker, found by keremimo), the MIDI clock followed pulse by pulse (after Felucca 1.0, from contributions by ChanceTheMaker and keremimo), the menu LIGHTS / KEYS / NOTES (NOTES by @renebohne) and USB AUDIO FULL | GPL-3.0 | `firmware/hal/fm1_enc.h`, `firmware/hal/fm1_input.h`, `firmware/src/usb.c`, `firmware/src/voice.c`, `firmware/src/midi_uart.c`, `firmware/src/seq.c`, `firmware/src/ui_menu.c`, `firmware/src/ui_input.c` |
| Felucca 1.0.1 by Leo Kuroshita / Hügelton Instruments: the FM6 engine (`eng_fm6.c`, its factory patches `tools/gen_fm6_patches.py`), the SPRING reverb, the engines' own envelopes | GPL-3.0 | `firmware/src/eng_fm6.c`, `firmware/src/fx.c` |
| SLOOP 2.4 by isod89: the micro timing (each step nudged in 1/64 of a step), the parameter locks (a sound parameter's value on a step, back at the next one; a knob turned meanwhile keeps its value) and the fills (FILL ONLY / NO FILL steps, a fill held or on the next bar), the track's FILT (the DJ filter on each track, `fx.c` djf_block), the sequencer to MIDI OUT and MIDI IN = CLOCK (`seq.c` seq_out_on), the dotted delay TIMEs, adapted to Jangada's sequencer, its layers and its project format (`seqx_t`, JNG1 section 2) | GPL-3.0 | `firmware/src/seq.c`, `firmware/src/ui_layers.c`, `firmware/src/project.c`, `firmware/src/core.h` |
| SLOOP 2.3 by isod89 and Felucca 1.0.1: the backup and restore of the whole FM-1 from the web editor (editor protocol v5, after Felucca's `fm1backup.js`), the editor's CHOP (a recording into a slot, chops of any length; SLOOP 2.2 / 2.3), the return to the official V15 from the installer (`fm1pkg.js` STOCK_V15, `fm1ota.js` OUR_LOADER) | GPL-3.0 | `firmware/src/editor_backup.c`, `web/fm1backup.js`, `web/editor.html`, `web/index_pkg.html`, `web/fm1pkg.js`, `web/fm1ota.js` |
| Felucca [Salt] by Chance Roth (ChanceTheMaker, <https://github.com/ChanceTheMaker/Felucca>), a Felucca fork: the Studio (Jangada's sound in the browser) follows its browser audio: the firmware's DSP #included by a small glue, built with `zig cc` for wasm32 and run in an AudioWorklet (its `web/audio/engine.c`, `worklet.js`, `browser.js`, `tools/build_browser_audio.py`) | GPL-3.0 | `web/studio/`, `tools/build_studio.py` |
| Felucca 1.0 by Leo Kuroshita / Hügelton Instruments and Melodee by Kerem Kilic / Ellic Studio (keremimo, <https://github.com/keremimo/melodee>, a Felucca fork): the FM6 patch bank in flash (Felucca's `fm6_bank.c`; a whole 32-voice bank as Melodee keeps it) and each track's FM6 patch kept in projects (Felucca's FUN8); the editor's 6-OP FM tab and FM6 commands (Felucca's `editor_fm6.c`, its DX7 `.syx` import / export); DX7 SysEx on the device (Melodee's `fm6_store.c`: voice, bank, parameter change, dump requests; its CIN 0xF fix in `usb.c`) | GPL-3.0 | `firmware/src/fm6_bank.c`, `firmware/src/project.c`, `firmware/src/editor_fm6.c`, `firmware/src/fm6_sysex.c`, `firmware/src/usb.c`, `web/editor.html` |
| SLOOP pull request #27, "DX7 engine", by majnikool (Majid Nik, <https://github.com/isod89/sloop-fm1/pull/27>), for SLOOP by isod89: ideas taken into the FM6 engine (no separate engine, no code of its DX7 core): the stored voices listed by name in PRESETS after the factory sounds, tagged with their bank; HOME held while turning PRESETS, and KNOB 3 on the PRESETS page, jumping a group at a time with the group in the top bar; more banks in flash; the "BANK n? SAVE=YES" question for a SysEx bank with no clear target | GPL-3.0 | `firmware/src/ui.c`, `firmware/src/ui_input.c`, `firmware/src/ui_draw.c`, `firmware/src/fm6_bank.c`, `firmware/src/fm6_sysex.c` |
| msfa (Google, 2012) / Dexed (Pascal Gauthier): the 6-operator FM core, ported to integer C by Felucca | Apache-2.0 | `firmware/src/fm6_core.c`, `LICENSES/Apache-2.0-msfa.txt` |
| JieLi AC79 SDK: `uboot.boot`, `cfg_tool.bin`, `eq_cfg_hw.bin` are read from your SDK checkout at build time and placed in the package; no SDK files are in this tree | Apache-2.0 | <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK> |

## Contributions

Contributions are welcome under GPL-3.0-only. By submitting one, you agree that it may be
combined with the Felucca Assets under the section 7 permission above.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.

"M-VAVE" and "FM-1" are trademarks of their respective owners. Felucca is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners.

## Radio

Felucca never enables the Bluetooth / Wi-Fi radio of the hardware.
