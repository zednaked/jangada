<p align="center"><img src="docs/jangada.gif" alt="Jangada" width="720"></p>

<p align="center">
<b>English</b> · <a href="README.pt-BR.md">Português</a><br>
<a href="https://github.com/zednaked/jangada/actions/workflows/ci.yml"><img src="https://github.com/zednaked/jangada/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
<img src="https://img.shields.io/badge/license-GPL--3.0-ff14aa" alt="GPL-3.0">
<img src="https://img.shields.io/badge/M--VAVE-FM--1-ff14aa" alt="M-VAVE FM-1">
<a href="https://github.com/zednaked/jangada/releases/latest"><img src="https://img.shields.io/github/v/release/zednaked/jangada?include_prereleases&color=ff14aa&label=version" alt="version"></a>
</p>

# Jangada 🛶

**Alternative firmware for the M-VAVE FM-1, with a dark, industrial and Brazilian accent.**

Drones that breathe and evolve on their own, rust, worn tape, machines and manguebeat, on a €70
pocket synth. Ten synth engines (a 6-operator FM that talks to Dexed, a superwave analog with a
Moog-style ladder filter), four
tracks, a modulation matrix, our own drum kits, effects made to be played live.

A fork of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments).
A *felucca* is a Nile sailboat; a *jangada* is the Brazilian one.

**[Play it in the browser](https://zednaked.github.io/jangada/webapp/studio/)** ·
**[Install](https://zednaked.github.io/jangada/)** ·
**[Listen](#listen)** ·
**[Releases](https://github.com/zednaked/jangada/releases)**

> **Alpha.** Use at your own risk. The FM-1's boot area is never touched, and you can go back to the
> official firmware at any time.

<p align="center"><img src="docs/screens.png" alt="Jangada screens: HOME, TRACKS, DRONE, the layers, KIT, DIST, MASTER, FM6, menu" width="100%"></p>

## Philosophy

Jangada does not want to be a compendium of the other FM-1 firmwares. It has a taste:

- **Dark and industrial.** For people who think of Nine Inch Nails, *The Downward Spiral*, *Ghosts I–IV*,
  film scores: sounds with rust, grain, worn tape, a hum underneath, metal, broken pianos.
- **Drones as a language.** A chord that holds and breathes on its own while you play the rest, and that
  evolves slowly, with a tension that opens over bars. It is the most Jangada idea of all.
- **Brazilian.** A *jangada* is a boat from the Brazilian northeast. **Manguebeat** (Chico Science &
  Nação Zumbi) already crossed maracatu with industrial weight: that is our crossing. Alfaia, zabumba,
  gonguê, agogô and cuíca; maracatu, baião and coco; the northeastern scale; rabeca and sanfona drones.
- **Made to be played live.** Hold a button and everything changes, effects that come in with one
  finger, nothing that makes you stare at the screen.

We bring in the good things from the other forks (Felucca 1.0, SLOOP, Felucca [Salt], Melodee) when
they serve that taste, always with Jangada's name and look and with credit to whoever made them. What
is only more of the same stays out.

## Listen

All rendered by the firmware's own DSP (the same C code that runs on the FM-1, running on a PC).

**Drones that evolve** (about a minute each: the sound moves on its own)

| | |
|---|---|
| [FERRUGEM](docs/sounds/drone-ferrugem.mp3) | a superwave that rusts, its tension rising over 16 bars |
| [SERTAO](docs/sounds/drone-sertao.mp3) | the accordion that turns sour |
| [CINZA](docs/sounds/drone-cinza.mp3) | piano ash in grains |
| [ABISMO](docs/sounds/drone-abismo.mp3) | FM that keeps opening |
| [tension ramp](docs/sounds/drone-tension-ramp.mp3) | TENS from 0 to 100 % over 16 bars |
| [static × evolving](docs/sounds/drone-static-vs-evolving.mp3) | the same drone without and with EVOL |

**Mangue** · [MARACATU](docs/sounds/mangue-maracatu.mp3) (baque virado) ·
[BAIAO](docs/sounds/mangue-baiao.mp3) · [COCO](docs/sounds/mangue-coco.mp3) ·
[baião with bass and sanfona](docs/sounds/baiao-bass-sanfona.mp3) ·
[coco with rabeca](docs/sounds/coco-rabeca.mp3)

**Machines** · [RUST](docs/sounds/kit-rust-grind.mp3) · [FORGE](docs/sounds/kit-forge-anvil.mp3) ·
[PISTON](docs/sounds/kit-piston-engine.mp3) · [HURT](docs/sounds/kit-hurt-fragile.mp3)

**Grit** · [dry](docs/sounds/grit-dry.mp3) → [worn tape](docs/sounds/grit-tape.mp3) →
[hum, tape and dust](docs/sounds/grit-hum-tape-dust.mp3) → [everything](docs/sounds/grit-all.mp3) ·
bass through [FUZZ](docs/sounds/grit-bass-fuzz.mp3), [FOLD](docs/sounds/grit-bass-fold.mp3) and
[RING](docs/sounds/grit-bass-ring.mp3)

**From 0.1** · [RUST BASS](docs/sounds/rust-bass.mp3) · [HURT PAD](docs/sounds/hurt-pad.mp3) ·
[GRIND LEAD](docs/sounds/grind-lead.mp3) · [MACHINE](docs/sounds/machine.mp3) ·
[METAL HIT](docs/sounds/metal-hit.mp3) · [BROKEN BELL](docs/sounds/broken-bell.mp3) ·
[STATIC](docs/sounds/static.mp3) · [GHOST KEYS](docs/sounds/ghost-keys.mp3) ·
[DIRTY ORGAN](docs/sounds/dirty-organ.mp3) · [DRONE SAW](docs/sounds/drone-saw.mp3) ·
[DRONE RING](docs/sounds/drone-ring.mp3) · [DRONE FM](docs/sounds/drone-fm.mp3) ·
[DRONE VOX](docs/sounds/drone-vox.mp3) · [SUPER SAW](docs/sounds/super-saw.mp3) ·
[SUPER PAD](docs/sounds/super-pad.mp3) · [HP SHIMMER](docs/sounds/hp-shimmer.mp3) ·
[four tracks together](docs/sounds/four-tracks.mp3)

## Play it in the browser

<a href="https://zednaked.github.io/jangada/webapp/studio/"><img src="docs/studio.png" alt="Jangada Studio" width="100%"></a>

The **[Jangada Studio](https://zednaked.github.io/jangada/webapp/studio/)** runs the firmware's DSP as
WebAssembly, sample for sample the same as the device: pick a track and a preset, play with the
computer keyboard (or a MIDI keyboard), hold drones, run the sequencer, turn the master. No FM-1 needed,
and nothing leaves your browser.

## By the numbers

| | |
|---|---|
| **Engines** | 10: ANALOG (superwave, Moog-style ladder filter), FM6 (6-operator, Dexed), DIGITAL (4-op FM), PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN |
| **Voices and tracks** | 8 voices over 4 tracks (3 synths + drums, or 4 synths) |
| **Parameters** | 16 per engine, a 4-slot modulation matrix with 9 sources |
| **Presets** | 93 factory (31 dark, drone and northeastern ones by Jangada), 32 user, 72 FM6 patches (8 + two banks of 32) |
| **Drums** | 5 Jangada kits + 32 synthesised + GM; 7 factory beats |
| **Sequencer** | 64 steps per track, chords, ratchet, chance, accent, slide, swing; UDI / RPT arp up to 4 bars |
| **Effects** | per-track distortion (5 types), SLICER, chorus, delay, 3 reverbs, DUST / DUCK / FILT / TAPE / HUM on the master, 16 punch-ins |
| **Live** | 5 layers (hold a button), drones with HOLD, EVOL, TENS, RAMP |
| **Connections** | USB MIDI + audio (stereo 44.1 kHz input) + console, TRS MIDI, clock in (USB / TRS) and out |
| **Storage** | 4 projects, autosave, full backup from the editor, projects and presets with stable keys |
| **Hardware** | a stock FM-1: nothing to solder, the boot area is never touched |

## Install

Current version: **[Jangada 0.7](https://github.com/zednaked/jangada/releases/tag/v0.7)** (alpha).
Plug the FM-1 straight into the computer with a USB **data** cable.

**Mac / Windows / Linux, in the browser**: open the
**[Jangada web installer](https://zednaked.github.io/jangada/)** in Chrome or Edge and press
*Install*. It carries the latest release; the editor and the Studio are next to it.

**Linux, from a terminal**: clone this repository and run

```
./instalar-linux.sh                    # downloads and installs the latest release
./instalar-linux.sh jangada-0.7.fwsc   # installs a file downloaded from the releases
./instalar-linux.sh --original         # back to M-VAVE's official firmware (V15)
./instalar-linux.sh --info             # what the FM-1 is running
./instalar-linux.sh --console          # serial console access (a udev rule, asks for sudo)
```

It sets up its own Python environment (`mido` + `python-rtmidi`) in `~/.local/share/jangada/` and
checks the SHA-256 of what it downloaded.

- **If an install fails**: hold **OCT−** while switching the FM-1 on (USB rescue) and install again. We
  have been through it for real: a 0.5.1 write dropped halfway (the cable), the FM-1 came up in *JANGADA
  USB RESCUE*, the installer wrote through it and the device came back whole.
- **Back to the official firmware**: from the web installer (a backup first), `./instalar-linux.sh
  --original`, or M-VAVE's M-UPGRADE.
- Since 0.2 the FM-1 shows up on the computer as **Jangada** (MIDI and audio). Felucca's web installer
  no longer finds an FM-1 running Jangada: use Jangada's.

## First steps

1. **A drone.** PRESETS to **FERRUGEM** (ANALOG), play a chord and let go: it keeps breathing and
   evolving on its own. Tap ARP until the **DRONE** page and turn **TENS**. **Hold ARP** to let the
   drone go; hold it again to cut the tails.
2. **The mangue.** ALGORITHM to track 4, PRESETS to the **MANGUE** kit: its maracatu beat comes with
   it. Press PLAY. In **GLO → KIT**, the BEAT knob switches to baião or coco.
3. **Grit.** Hold **FX**: the white keys become 16 effects (loops, reverse, tape stop…) and the knobs
   become FILT, DUST, DUCK and TAPE.
4. **Live mix.** Hold **GLO**: keys 1–4 mute, 5–8 solo, 9 held a fill, 10 the next bar a fill, the last
   one is tap tempo.
5. **Record.** **REC** on a page with nothing to record opens the tracks view; the FM-1 is also a sound
   card: record the master on the computer from the **Jangada** input.

## What's in it

### Drones
The drone presets run the arpeggiator in **RPT** every **4 bars** with **HOLD**: play a chord, let go,
and it keeps breathing on its own, even while you play the other tracks.
- **Hold ARP** → DRONE OFF (the latched chords fade with their release); **again** → SILENCE.
- **DRONE page** (tap ARP until it): **HOLD**, **EVOL**, **TENS**, **RAMP**, per track.
  - **EVOL**: slow, smooth random walks (cycles of tens of seconds to minutes, never the same) move the
    filter, the shape and the engine's natural parameter; each voice detunes and breathes its own way.
    It stops when the drone stops; a new drone starts from the preset's sound.
  - **TENS**: opens the filter, raises resonance, drive and brightness, spreads the voices (up to ±12
    cents) and makes the walks deeper and faster. At 0, nothing changes.
  - **RAMP**: OFF or 1 to 32 bars for the tension to reach TENS, in time. A drone born from silence
    starts at 0 and builds.
  - The screen shows the four walks and the tension bar.
- In the modulation matrix, the **DRIFT** source is the track's walk, for any destination.
- Presets: **FERRUGEM** (ANALOG), **ABISMO** (DIGITAL), **SERTAO** (WHEEL), **CINZA** (GRAIN),
  **CARVAO** (FM6), **LODO** (ANALOG through the ladder), **RABECA** and **SANFONA**, and DRONE SAW, RING, FM, DUST, VOX and ORGAN.
- To sequence a drone: arp OFF, PATTERN with **DIV 4BAR**, one chord per step.

### Mangue and the northeast
- **MANGUE** kit: alfaia and its middle drum, zabumba and its bacalhau stick, maracatu snare, low and
  high gonguê, agogô, rising cuíca, ganzá, triangle (closed chokes open), claps and tamanco, with the
  mangue's weight.
- **Factory beats** (GLO → KIT, **BEAT** knob): **MARACATU** (baque virado), **BAIAO**, **COCO**. A
  Jangada kit on an empty drum track brings its beat along; over a pattern of yours, BEAT asks for a
  second turn of the knob.
- **NORD** scale (SCL): mixolydian with a raised 4th, the northeastern mode. NORD or MIX for baião, DOR
  for xote and toada.
- **BAIAO BASS** (ANALOG), **RABECA** (ANALOG) and **SANFONA** (WHEEL) presets, the last two as drones.

### Grit: tape, master and punch
- **TAPE** (GLO → MASTER, or knob 4 while holding FX): worn tape, with saturation, wow (the pitch
  swaying slowly), flutter (the fast tremble) and treble loss, all on one knob.
- **HUM** (GLO → MASTER 2): the 60 Hz mains hum with hiss and crackle, only while playing.
- **DUST** (an old sampler and a record: bits, rate, crackle), **DUCK** (the kick dips the synths for an
  eighth note), **FILT** (DJ filter: left low-pass, right high-pass).
- **Distortion per track** (FX → DIST, TYPE): **SOFT** (as always), **FUZZ** (a gated pedal), **FOLD**
  (wavefolder), **CRUSH** (bits and rate) and **RING** (ring mod, carrier on FREQ).
- **Punch-in** (hold **FX** + a white key): loops 1/4 to 1/32, stutter, reverse, tape stop, half, LP /
  HP sweeps, phone, crush, alias, gate, echo and wobble on the whole mix, while the key is held.
- **Reverbs** (FX → REVERB, TYPE): **ROOM**, **SPRING** and **PLATE** (a stereo feedback delay network).

### Drums
- Jangada's kits, first in the list (PRESETS on the drum track, or GLO → KIT): **RUST** (dry
  industrial: distorted kick, gated snare, crushed hats), **FORGE** (anvil, chains, plates), **PISTON**
  (clicks, steam, valves), **HURT** (low, muffled, breathy) and **MANGUE**, each with its beat
  (**GRIND**, **ANVIL**, **ENGINE**, **FRAGILE**, maracatu).
- 32 more synthesised kits (808, 909, TECHNO, INDUSTR, GLITCH, DUBSTEP, JUNGLE…) and the sampled GM kit.
- **Track 4: DRUM or SYNTH** (TRACKS, knob 1 TYPE): it becomes a fourth synth part.

### Engines
- **Turbo ANALOG** (EDIT 3 / 4): **SUPR** superwave (up to 6 detuned copies), **SDTN**, **SUB**,
  **DRFT** (slow per-voice detune), **FTYP** LP12 / LP24 / BP / HP / **LADR**: a Moog-style filter, a
  four-pole transistor ladder (24 dB/octave) with saturated feedback: RES makes it sing and thins the lows as the
  hardware does, DRV pushes it into a growl. To find it: tap **EDIT** (don't
  hold it) up to the EDIT 4 page and turn knob 1 to the end; CUT, RES and DRV are on EDIT 2. Presets **PICHE BASS**, **MOTOR LEAD** and **LODO** (a drone).
  **SAT** (EDIT 4, knobs 2 and 3): a saturation per voice after the filter, before the VCA
  (OSC → FILTER → SAT → VCA): **WARM** (tube, even harmonics), **HARD** (a wall: buzz) or **FOLD** (a
  wavefolder); **SDRV** how hard. Each voice is shaped alone, so chords stay clean where the track's DIST
  would smear them. Preset **SUCATA**.
- **Modulation into the filter**: the MOD 1–4 matrix reaches every engine parameter, so CUT, RES, DRV and SDRV
  are destinations: LFO → RES, ENV → CUT, VEL → DRV, ENV → SDRV. They move per voice, every block.
- **FM6**: 6-operator FM (Dexed's msfa core), 32 algorithms, 8 factory patches (PTCH F1–F8) and
  **two banks of 32** in flash (B1–B32 and B33–B64: two whole cartridges), with macros on the knobs (ALG
  FB MLVL MRAT MEG VMOD DTUN). Each track's patch goes along in the project, the autosave and the backup.
- **The bank voices in PRESETS**: after the factory presets, PRESETS lists every voice stored in the
  banks by name (tagged BK1 / BK2), before the user presets; picking one puts the track on FM6 with that
  patch. **Hold HOME and turn PRESETS** to jump a group at a time (each engine's presets, FM6 BANK 1,
  FM6 BANK 2, the user presets; the group shows in the top bar, and that HOME press opens nothing);
  **KNOB 3 (KIND)** on the PRESETS page does the same.
- **Dexed edits Jangada live**: point Dexed's (or another DX7 editor's) MIDI at the FM-1 and turning a
  knob there changes the FM6 track at once; send a voice to the track or a 32-voice cartridge to a
  bank, and ask for them back. The cartridge goes to the bank the FM6 track's PTCH is in (B33–B64:
  bank 2); with PTCH on a factory patch the screen asks **FM6 BANK 1? SAVE=YES**: OCT- / OCT+ choose
  bank 1 or 2, SAVE writes it, any other button cancels.
- And the others: DIGITAL (4-operator FM), PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN.
- **16 parameters per engine** (Felucca has 8).
- **Modulation matrix** (LFO → MOD 1–4): sources LFO, ENV, VEL, KEY, RND, MODW, AT, EXPR and DRIFT;
  destinations filter, pitch, shape or any engine parameter.
- **One-key chords** (SCL → CHORD): TRIAD, 7TH, 9TH, SUS4, POWER; the white keys walk the scale and
  each plays its chord.

### Live: hold a button
Tap a function button and its pages open. **Hold** it and it becomes a **layer**: the 16 white keys and
the 4 knobs change job, and the screen shows 16 tiles and 4 dials. **HOME** tapped while a layer is held
**locks** it open. PLAY, REC and OCT keep working inside it.

| Hold | Keys | Knobs 1 · 2 · 3 · 4 |
|---|---|---|
| **FX** | the 16 punch-in effects | FILT · DUST · DUCK · TAPE |
| **GLO** | 1–4 mute, 5–8 solo, 9 held = a fill, 10 = the next bar a fill, the last one tap tempo | levels of tracks 1–4 |
| **SEQ** | the 16 steps of the page (empty = set, set = press and release clears); black keys: page, shift, half / double, transpose, **F#5 held erases** what the playhead passes | NOTE · DIV · SWG · LEN; steps held: NOTE · RTCH · CHNC · FLAG |
| **SCL** | any key = the key of the song | CHRD · SCL · QNT · TRN |
| **EDIT** | the track's engine; the last one = track 4 DRUM / SYNTH | PRST · VOICE · GLIDE · LVL |

With **SEQ** held, **OCT− / OCT+** = undo / redo of the pattern. With steps held, the other knobs work on
them too: **SELECT** nudges them off the grid (1/64 of a step, up to half a step early or late),
**ALGORITHM** picks a sound parameter and **PRESETS** gives it another value on those steps only (a
parameter lock; it goes back at the next step without one), **OCT+** sets their condition (ALWAYS,
**FILL** only in a fill, **NO FILL** never in one) and **OCT−** takes their locks and nudge away. The
title shows the picked parameter, its value on the step, the nudge and the condition; a small square at
the top right of a tile marks a lock or a nudge, one at the top left a condition (full: FILL, hollow:
NO FILL). **REC** on a page with nothing to
record opens the **TRACKS** view: BPM, bar.beat, one row per track with its steps and playhead.

### Sequencer and arpeggiator
- 64 steps per track, chords, ties, accent, slide; live recording.
- **RTCH** ratchet x1–x4 and **CHNC** chance 100/75/50/25 % per step (STEP 2).
- **CHORD+**: with a chord mode on (SCL → CHORD), the black keys change the chord, held before the white
  key or pressed while the chord sounds: **F#** major ↔ minor, **G#** + 7th, **A#** sus4, **C#** + 9th,
  **D#** inversion (they combine; the 7th and 9th come from the scale). On the same page **STRM** spreads
  a chord's notes like a strummed guitar (1–60 ms a note; right low to high, left high to low; on the
  keys and the chord steps) and **VLEAD** voices each chord nearest the last one.
- Delay TIME up to the dotted **1/8D** and **1/16D**.
- **FILT** on every track (FX → DIST, knob 4): left a low-pass, right a high-pass, centre off; it stays
  when the sound changes and can be locked on a step.
- **Micro timing**, **parameter locks** (12 a track) and **fills** per step, in the SEQ layer; they are
  saved with the project.
- Arpeggiator with the **UDI** and **RPT** modes; divisions up to **4BAR** (in the sequencer too).

### MIDI and USB
- **MIDI in on the TRS jack** (3.5 mm) and over USB: channels 1–3 the synth tracks, 4 track 4 when it
  is a SYNTH, the drum channel (default 10) the drums, the others the selected track.
- Pitch bend, sustain, all notes off, mod wheel, aftertouch and expression (as matrix sources).
- **Clock** (GLO → GLOBAL CLK): INT, **USB** or **TRS**, pulse by pulse, no drift; **SYNC OUT** sends
  clock over USB.
- **HOME → MIDI OUT = SEQ**: the sequencer and the arp go out over USB too (each track on its channel,
  the drums on theirs), every note ended; KEYS (default): only the keys. **MIDI IN = CLOCK** follows the
  clock and START / STOP and ignores incoming notes. Both are settings of the FM-1, not of the project.
- **USB audio**: the FM-1 shows up as a stereo audio input named **Jangada** (44.1 kHz, no driver).
  HOME → USB AUDIO: the level follows MASTER, or FULL. On Linux:
  `arecord -D hw:Jangada -f S16_LE -r 44100 -c 2 take.wav`.

### Screen, lights and menu
- Antialiased font (Inter Tight), icons, cards; the **CHOQUE** palette (hot pink) by default, and **NIGHT** (true black, green).
- Menu (hold **HOME**): COLOR, SPEAKER (a low cut for the speaker), **LIGHTS** (the buttons glow
  dimly, for playing in the dark), **KEYS** (lights the C keys or the white ones), **NOTES** (sounding
  notes light their keys), USB AUDIO, MIDI OUT, MIDI IN, NEW PROJECT, ABOUT.
- Pages without a graph (EDIT, DIST, CHORD, GLOBAL, SYSTEM…) show their four values **large**, in four
  cards placed as the knobs; the one you turn in white.
- **Visualiser**: on the TRACKS screen, tap **HOME**: the whole screen shows what plays. **SELECT** changes
  the style: **OSC** (the wave with a phosphor trail), **SONAR** (the wave round a circle, a sweep a bar),
  **VU** (a meter per track and the mix), **ESTEIRA** (each track's level scrolling by) and **MAR** (the
  raft riding the tracks' swells). HOME again goes HOME; the keys and the layers keep working.
- Nothing blinks hard: what can be pressed **breathes** (a slow fade), such as a locked layer's button or
  OCT+ in a dialog.

### On the site
- **[Studio](https://zednaked.github.io/jangada/webapp/studio/)**: Jangada in the browser.
- **[Editor](https://zednaked.github.io/jangada/webapp/editor/)** (Chrome or Edge, FM-1 on USB): every
  parameter, the steps, the presets; the **6-OP FM** tab (the whole FM6 patch, the banks B1–B32 and B33–B64,
  DX7 `.syx` both ways); **CHOP** (cut a recording of any length into up to 16 chops for USR1–3);
  **Backup and restore** of everything in one `jangada-backup-DATE.json` file.
- **[Installer](https://zednaked.github.io/jangada/)**: installs the latest release, or goes back to the
  official firmware.

### Storage and safety
- **Autosave**: stopped and untouched for a few seconds, the project goes to flash and comes back at
  power-on. **OCT+** held at power-on starts empty; **HOME → NEW PROJECT** clears everything.
- Projects and presets store each value under a **stable key**: newer versions open what older ones
  saved. Felucca's projects and presets are read and converted.
- **Safe updates**: the installer refuses a damaged package; the loader checks the CRC before it lets
  the new firmware start; **OCT−** at power-on (or two failed boots) opens the **USB rescue**, proven
  on a real interrupted write. The boot area is never written, so an FM-1 running Jangada always has a
  way back.

## Tools

| | |
|---|---|
| `tools/fm1_console.py status` | CPU, audio, USB, MIDI, battery |
| `tools/fm1_console.py check` | on-device test: CPU peak, late audio, resets |
| `tools/fm1_console.py voices` | what sounds on each track, and why (with EVOL and TENS) |
| `tools/fm1_console.py preset E I [T]` | loads preset I of engine E on track T |
| `tools/fm1_console.py t4 synth\|drum` | track 4 type |
| `tools/fm1_console.py droneoff` | as holding ARP |
| `tools/fm1_console.py color CHOQUE` | screen palette |
| `tools/fm1_console.py g ID [VALUE]` | reads or sets a global parameter (e.g. `g 28 90` = DUST) |
| `tools/fm1_console.py punch N\|off` | starts a punch-in effect (0–15) or stops it |

## Build and test

See [BUILDING.md](BUILDING.md). On Linux x86-64 the JieLi toolchain runs natively, no Docker:

```
tools/get_toolchain.sh        # the toolchain, in ~/.jieli
tools/get_sdk_files.sh        # only the 3 AC79 SDK files the package uses
./build.sh                    # build/felucca.fwsc
sh tests/run_tests.sh         # every test, on the PC
python3 tools/build_studio.py --get-zig   # the Studio (WebAssembly), once
```

- **Reproducible build**: the date comes from the last commit; two builds give the same bytes.
- The tests cover the sound (renders of every preset with a fingerprint), health (clipping, DC, stuck
  notes), the CPU budget, formats and compatibility, MIDI and clock, DX7 SysEx, the drones, the grit,
  the kits, the layers and screens, the installer, the editor, the backup and the Studio (the
  WebAssembly comes out sample for sample the same as the same C compiled on the PC).
- **CI** on every push; a `vX.Y` tag publishes the `.fwsc` on a release and updates the site.

## Next

- Finer MIDI (per-channel bend range, CC1 vibrato) and the chord name on HOME.
- Broken pianos (*Hurt*, *The Social Network*).
- RUST, ASH and MANGUE palettes; the jangada on the boot screen.

Ideas, sounds and bugs: open an [issue](https://github.com/zednaked/jangada/issues).

## Credits and license

Jangada is GPL-3.0-only, as Felucca is. The original work is **Leo Kuroshita's (@kurogedelic),
Hügelton Instruments**. Parts come from other forks, with our thanks:

- [Felucca 1.0](https://github.com/hugelton/Felucca): FM6, USB audio, the SPRING reverb, the look
  (Inter Tight, Fukiai icons), the FM6 bank.
- [SLOOP](https://github.com/isod89/sloop-fm1) (isod89): the layers, punch FX, the master, the
  synthesised kits, the chords, PLATE, autosave, safe updates and rescue, the lights, TRS MIDI, micro
  timing, parameter locks and fills.
- [Felucca [Salt]](https://github.com/ChanceTheMaker/Felucca) (Chance Roth): the Studio in the browser.
- [Melodee](https://github.com/keremimo/melodee) (Kerem Kilic / Ellic Studio): DX7 SysEx.
- msfa / Dexed (Google, Pascal Gauthier): the FM6 core (Apache-2.0).

Going the other way: the ratchets went upstream and ship in
[Felucca 1.0.5](https://github.com/hugelton/Felucca/releases/tag/v1.0.5) ([PR #100](https://github.com/hugelton/Felucca/pull/100)).

See [README.felucca.md](README.felucca.md) and [LICENSING.md](LICENSING.md) for the full credits
(fonts, samples, engines).

M-VAVE and FM-1 are trademarks of their respective owners. Jangada is not affiliated with or endorsed
by them, nor by Felucca.
