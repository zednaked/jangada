#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests (no hardware). Run from the repo root after ./build.sh:
#   tests/run_tests.sh
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, the drum kit, voice modes, FX sends, a 4-track mix: one hash
#                   each in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in build/felucca.dis
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the budget of 8, steal fades, MONO / LEGATO / UNISON keep their note, the VOICE cap,
#                   no hanging notes on any MIDI / key routing.
# After an intended change of the sound: GOLDEN_UPDATE=1 sh tests/run_tests.sh, review the diff
# of tests/golden.txt, commit it with the change. After an intended change of the cost (or a new
# compiler): BUDGET_UPDATE=1 (rewrites cpu_baseline.txt and target_budget.txt). VERBOSE=1: every render.
# USB audio (tests/uac_test.c, from Felucca 1.0.1): the UAC1 descriptors as a host parses them (with and
#                   without CDC), the ring and packetiser at the HALF_FRAMES of src/core.h: 44.1 frames per
#                   packet, every frame in order, underrun / overrun, restart.
set -e
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
OUT=build/host
mkdir -p "$OUT"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"
fail=0
run() { echo "== $1"; t="$1"; shift; "$@" || { fail=1; echo "!! FAILED: $t"; }; }

[ -f build/felucca.fwsc ] || { echo "run ./build.sh first"; exit 1; }

$CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"

$CC -o "$OUT/upreset_test" tests/upreset_test.c
run "user presets (UP_PUT parser, bank round trip, versions)" "$OUT/upreset_test"

$CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/backup_test" tests/backup_test.c -lm
run "backup / restore: editor protocol v5..v7 against simulated flash (Jangada)" "$OUT/backup_test"

$CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/dx7_test" tests/dx7_test.c -lm
run "DX7 SysEx for FM6: voice, banks of 32, parameters live, dumps; the bank voices in PRESETS (Jangada, after Melodee / SLOOP)" "$OUT/dx7_test"

$CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"

HALF=$(sed -n 's/^#define HALF_FRAMES \([0-9]*\).*/\1/p' firmware/src/core.h)
$CC -DT_CDC=1 -DHALF_FRAMES=$HALF -o "$OUT/uac_test" tests/uac_test.c
run "USB audio input: descriptors (with CDC), ring and packets" "$OUT/uac_test"
$CC -DT_CDC=0 -DHALF_FRAMES=$HALF -o "$OUT/uac_test_nocdc" tests/uac_test.c
run "USB audio input: descriptors (without CDC), ring and packets" "$OUT/uac_test_nocdc"

$CC -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry" "$OUT/ota_test" build/felucca.fwsc

head -c 200000 build/felucca.bin > "$OUT/old_app.bin"
python3 tools/fm1pkg_make.py "$OUT/old_app.bin" build/loader/ota.bin "$OUT/old.fwsc" >/dev/null
$CC -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: other app -> this build" "$OUT/ldr_test" "$OUT/old.fwsc" build/felucca.fwsc

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/hostsim" tests/hostsim.c -lm
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/scale_test" tests/scale_test.c -lm
run "scales: white-key mapping and note lifecycle" "$OUT/scale_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/arp_test" tests/arp_test.c -lm
run "arp: UPDN / UDI / RPT, long divisions, keys follow TRN / SCALE (Jangada)" "$OUT/arp_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/step_test" tests/step_test.c -lm
run "steps: RTCH ratchet and CHNC chance (Jangada)" "$OUT/step_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/seqx_test" tests/seqx_test.c -lm
run "steps: nudges, parameter locks, fills (Jangada, after SLOOP 2.4)" "$OUT/seqx_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/punch_test" tests/punch_test.c -lm
run "master: punch-in FX, DUST, DUCK, FILT (Jangada, after SLOOP)" "$OUT/punch_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/chord_test" tests/chord_test.c -lm
run "chords: one key, a chord of the scale (Jangada, after SLOOP)" "$OUT/chord_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/drumkit_test" tests/drumkit_test.c -lm
run "drum kits: synthesised, every GM note (Jangada, after SLOOP)" "$OUT/drumkit_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/midi_test" tests/midi_test.c -lm
run "MIDI: bend, sustain, CCs, clock in / out (Jangada)" "$OUT/midi_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/reverb_test" tests/reverb_test.c -lm
run "reverbs: ROOM, SPRING, PLATE (Jangada)" "$OUT/reverb_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/grit_test" tests/grit_test.c -lm
run "GRIT: TAPE, HUM, DIST FUZZ / FOLD / CRUSH / RING (Jangada)" "$OUT/grit_test"

$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/insert_test" tests/insert_test.c -lm
run "INSERT: every type (DISP 0.9.1), MIX 0 bit for bit, fades (Jangada 0.9)" "$OUT/insert_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/robo_test" tests/robo_test.c -lm
run "ROBO: VOSIM, GENDY, WALSH, SCAN sound, bounded, in tune; SEED repeats; the ring moves (Jangada 0.9.1)" "$OUT/robo_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/ladder_test" tests/ladder_test.c -lm
run "ANALOG LADR: the four-pole ladder low-pass (Jangada)" "$OUT/ladder_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/sat_test" tests/sat_test.c -lm
run "ANALOG SAT: the shaper per voice after the filter (Jangada)" "$OUT/sat_test"
$CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/layers_test" tests/layers_test.c -lm
run "layers: SEQ steps and tools, undo, ENGINE, screens (Jangada)" "$OUT/layers_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/kit_test" tests/kit_test.c -lm
run "kit: GM 42 / 44 / 49 are not toms (Felucca#25)" "$OUT/kit_test"
run "keys: stable parameter keys (Jangada)" python3 tests/keys_test.py
run "editor mock tables == firmware (Jangada)" python3 tools/gen_editor_tables.py --check
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/mod_test" tests/mod_test.c -lm
run "mod: the modulation matrix (Jangada)" "$OUT/mod_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/drone_test" tests/drone_test.c -lm
run "drones: EVOL walks, TENS and its RAMP, DRIFT, the presets that evolve (Jangada)" "$OUT/drone_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fm6_ams_test" tests/fm6_ams_test.c -lm
run "FM6 AMS: the LFO amplitude share as Dexed figures it, a mild tremolo, the voice ends (Jangada)" "$OUT/fm6_ams_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/track4_test" tests/track4_test.c -lm
run "track 4: DRUM / SYNTH (Jangada)" "$OUT/track4_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/voice_test" tests/voice_test.c -lm
run "voices: overload shedding, a key let go after a VOICE change (Jangada, after SLOOP 2.3)" "$OUT/voice_test"
$CC -O2 -o "$OUT/enc_test" tests/enc_test.c
run "panel: encoders (one detent state, whole cycles) and key debounce" "$OUT/enc_test"
run "DSP render (ANALOG preset 0)" "$OUT/hostsim" 0 0 1 "$OUT/render.wav"
mkdir -p build/tracks_demo
run "TRACKS: 4-track pattern, live recording (lengths, swing), voice budget, engine switch, cost" env TRACKS=build/tracks_demo "$OUT/hostsim" 0 0 1 "$OUT/tracks.wav"
$CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/project_test" tests/project_test.c -lm
run "project formats (JNG2 keyed, JNG1 read; Felucca FUN3 / FUN2 / FUN1 read)" "$OUT/project_test"
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/slicer_test" tests/slicer_test.c -lm
mkdir -p build/slicer_demo
run "SLICER: no clicks, timing, sync with the sequencer, STUT, cost, demos" "$OUT/slicer_test" build/slicer_demo
$CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
# host CPU baseline per platform: instruction counts differ between the Mac and Linux x86-64
case "$(uname -s)-$(uname -m)" in
Darwin-*) CPU_BASE=tests/cpu_baseline.txt ;;
*) CPU_BASE="tests/cpu_baseline.$(uname -s | tr A-Z a-z)-$(uname -m).txt" ;;
esac
run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt "$CPU_BASE"
# SLICE (tests/slice_test.c) needs a FELUCCA_SLICE=1 build; the engine is not built by default

run "regression: target cost of the render loops" python3 tests/target_budget.py \
    build/felucca.dis tests/target_budget.txt

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py

if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol, samples, packages, update protocol" node web/test_web.mjs
    run "web backup module (fm1backup.js, Jangada)" node web/test_backup.mjs
    # Studio (web/studio): the DSP as WebAssembly, needs zig (python3 tools/build_studio.py --get-zig)
    st=0; python3 tools/build_studio.py --native >/dev/null || st=$?
    if [ $st -eq 0 ]; then
        run "Studio: the DSP as WebAssembly == the host build, sound, page (Jangada)" node web/test_studio.mjs
    elif [ $st -eq 2 ]; then
        echo "== skip Studio test (no zig: python3 tools/build_studio.py --get-zig)"
    else
        echo "== Studio: tools/build_studio.py failed"; fail=1
    fi
else
    echo "== skip web tests (no node)"
fi

[ $fail -eq 0 ] && echo "ALL HOST TESTS PASSED" || { echo "HOST TESTS FAILED"; exit 1; }
