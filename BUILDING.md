# Building Felucca

The build makes three files in `build/`:

| File | What |
| --- | --- |
| `felucca.bin` | the firmware app |
| `loader/ota.bin` | the update loader |
| `felucca.fwsc` | the installable package (app + loader) |

## Prerequisites (macOS)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools`
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0). The package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`); they are not part of this tree.

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`./build.sh --release 0.9-beta` makes a release build: the package identity becomes
`FM-1_909` and the version string `0.9-BETA`; the package is `build/felucca-0.9-beta.fwsc`.

Build options (environment, `0` or `1`; defaults in `firmware/src/felucca.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_UAC` | 1 | USB audio input: the master output as a 16-bit stereo 44.1 kHz USB recording device (UAC1) |
| `FELUCCA_UAC_TONE` | 0 | bench: the USB audio input sends test triangles instead of the music |
| `FELUCCA_UART` | 0 | TRS MIDI IN (not tested on hardware) |

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works and the
SAMPLE engine has only the generated drum kit.

## Tests

```
tests/run_tests.sh
```

Runs the host tests (flash storage, user presets, MIDI parser, update entry, update
loader, a DSP render, the 4-track mix, project formats, the SLICER, the regression suite,
the command-line installer) and, with Node.js, the web page tests. Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build).

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Studio (the sound in the browser)

The site's Studio (`web/studio/`) plays Jangada without the FM-1: the firmware's DSP
(`firmware/src`, #included by `web/studio/engine.c`, as the host tests do) compiled to
WebAssembly and run in an AudioWorklet. It needs Zig (its clang targets wasm32); no root:

```
python3 tools/build_studio.py --get-zig     # Zig 0.14.1 into ~/.local (once)
python3 tools/build_studio.py --native      # build/studio/engine.wasm (+ the same C for this computer)
node web/test_studio.mjs                    # sound, and wasm == native bit for bit
python3 web/make_site.py build/felucca.fwsc dev /tmp/jangada-site
cd /tmp/jangada-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/studio/
```

`tests/run_tests.sh` runs the Studio test when Zig is there and skips it otherwise.
After Felucca [Salt]'s browser audio (Chance Roth).

## Install

Use the web installer in Chrome or Edge:
<https://hugelton.github.io/Felucca/webapp/installer/>. It installs the released package.

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/felucca.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1
```

Or, to install your own build from the web installer, make a local copy of the site and open it from `localhost`
(Web MIDI needs a secure context):

```
python3 web/make_site.py build/felucca.fwsc dev /tmp/felucca-site
cd /tmp/felucca-site && python3 -m http.server 8000
# open http://localhost:8000/webapp/installer/
```

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## The second core (on, with a raised supply)

The AC79 in the FM-1 has two cores. Jangada uses the second one (after
[X0X](https://github.com/charlesvestal/fm1-x0x) and [Melodee](https://github.com/keremimo/melodee)):
`hal/fm1_cpu1.{h,S}` starts it at power-on, and `fx.c` mix_block hands it some of the synth parts each
block. The output is bit-for-bit the same on one core and on two (`tests/dualcore_test.c`, also under
ThreadSanitizer with `TSAN=1 sh tests/run_tests.sh`). `FELUCCA_CPU2=0` builds a one-core image.

**The supply.** On some FM-1s the second core reads values from RAM that are not there, its instruction
fetches included, at the supply the boot loader leaves: SYSVDD (core and SRAM, `P3_ANA_CON9`) at 11,
1.26 V, and VDC14 (`P3_ANA_CON6`) at 3, 1.40 V. The FM-1 runs at 360 MHz, above the AC79 SDK's 320 MHz
table. So before it starts core 1, Jangada raises VDC14 to 5 (1.50 V) and then SYSVDD to 15 (1.38 V), a
step at a time (`hal/fm1_sys.h` fm1_core_supply, after Melodee's `fm1_power.h`). Measured on our unit
(October 2026), core 1 rendering part of 12 held voices, drums and the sequencer, each from a fresh boot:

| SYSVDD | VDC14 | core 1 |
|---|---|---|
| 11, 1.26 V (boot loader) | 3, 1.40 V (boot loader) | faults within ~6 s, every time |
| 12 / 13, 1.29 / 1.32 V | 3, 1.40 V | faults within ~6 s |
| 11, 1.26 V | 4, 1.45 V | faults after ~90 s |
| 14, 1.35 V (JieLi's above 320 MHz) | 4, 1.45 V | misreads from ~40 s, faults after ~3.5 min |
| **15, 1.38 V** | **5, 1.50 V** | **15 min, 1.23 M blocks: 0 misreads, 0 faults** |

VDC14 is what matters: raising SYSVDD alone does nothing. With both cores the same load takes ~21% of
core 0 instead of ~31%. X0X 1.0.3 also misbehaved on this unit (clicks, update sessions stopping), at
the boot loader's supply. Reported to [X0X (#10)](https://github.com/charlesvestal/fm1-x0x/issues/10) and
[Melodee (#17)](https://github.com/keremimo/melodee/issues/17).

**Measuring it.** `tools/fm1_console.py status` shows `sysvdd` and `vdc14`, `cpu2` (1 while it answers),
`cpu2_blocks`, `cpu2_bad` (mailbox reads it refused: stays 0 on a healthy unit), `cpu2_timeouts`,
`cpu2_stack`. `cpu2 on` / `off` hands it parts or stops it, `cpu2 clr` zeroes `cpu2_bad`, `vdd S [D]` sets
SYSVDD to S (11..15) and VDC14 to D, and `p33 ADDR [BYTE]` reads or writes one P33 register. A trap: a
`cpu2_bad` that stops rising can also mean core 1 has died (it counts nothing then), so check that
`cpu2_blocks` still rises. If core 1 faults it is held, the fault is recorded with `core 1`, and the FM-1
goes on with one core; a crash in a boot's first 30 s keeps the next boot on one core.
