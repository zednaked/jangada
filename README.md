<p align="center"><img src="docs/jangada.gif" alt="Jangada" width="720"></p>

<p align="center">
<b>English</b> · <a href="README.pt-BR.md">Português</a><br>
<a href="https://github.com/zednaked/jangada/actions/workflows/ci.yml"><img src="https://github.com/zednaked/jangada/actions/workflows/ci.yml/badge.svg" alt="CI"></a>
<img src="https://img.shields.io/badge/license-GPL--3.0-ff14aa" alt="GPL-3.0">
<img src="https://img.shields.io/badge/M--VAVE-FM--1-ff14aa" alt="M-VAVE FM-1">
</p>

# Jangada 🛶

**Alternative firmware for the M-VAVE FM-1, with a dark, industrial and Brazilian accent.**
Drones that breathe on their own, rust, machines and manguebeat on a €70 pocket synth: ten synth engines
(6-operator FM among them), a superwave analog, a modulation matrix, ratchets and four tracks.
A fork of [Felucca](https://github.com/hugelton/Felucca) by Leo Kuroshita (Hügelton Instruments).
A *felucca* is a Nile sailboat; a *jangada* is the Brazilian one.

## Philosophy

Jangada does not want to be a compendium of the other FM-1 firmwares. It has a taste:

- **Dark and industrial.** For people who think of Nine Inch Nails, *The Downward Spiral*, *Ghosts I–IV*,
  film scores: sounds with rust, grain, worn tape, a hum underneath, broken pianos.
- **Drones as a language.** A chord that holds and breathes on its own while you play the rest is the
  most Jangada idea of all, and it will grow: drones that evolve, tension that opens slowly.
- **Brazilian.** A *jangada* is a boat from the Brazilian northeast. **Manguebeat** (Chico Science &
  Nação Zumbi) already crossed maracatu with industrial weight: that is our crossing. Kits of alfaia,
  zabumba and agogô, maracatu, baião and coco patterns, northeastern scales.
- **Made to be played live.** Hold a button and everything changes, effects that come in with one
  finger, nothing that makes you stare at the screen.

We bring in the good things from the other forks (Felucca, SLOOP, Melodee) when they serve that taste,
always with Jangada's name and look and with credit to whoever made them. What is only more of the same
stays out.

> **Alpha.** Use at your own risk. The FM-1's boot area is never touched, and you can go back to
> the official firmware at any time.

## Listen

Rendered by the firmware's own DSP (the same C code, run on a PC):

| Dark / industrial | Drones | Superwave |
|---|---|---|
| [RUST BASS](docs/sounds/rust-bass.mp3) | [DRONE SAW](docs/sounds/drone-saw.mp3) | [SUPER SAW](docs/sounds/super-saw.mp3) |
| [HURT PAD](docs/sounds/hurt-pad.mp3) | [DRONE RING](docs/sounds/drone-ring.mp3) | [SUPER PAD](docs/sounds/super-pad.mp3) |
| [GRIND LEAD](docs/sounds/grind-lead.mp3) | [DRONE FM](docs/sounds/drone-fm.mp3) | [HP SHIMMER](docs/sounds/hp-shimmer.mp3) |
| [MACHINE](docs/sounds/machine.mp3) | [DRONE DUST](docs/sounds/drone-dust.mp3) | [four tracks at once](docs/sounds/four-tracks.mp3) |
| [METAL HIT](docs/sounds/metal-hit.mp3) | [DRONE VOX](docs/sounds/drone-vox.mp3) | |
| [BROKEN BELL](docs/sounds/broken-bell.mp3) · [STATIC](docs/sounds/static.mp3) | [DRONE ORGAN](docs/sounds/drone-organ.mp3) | |
| [QUIET KEYS](docs/sounds/quiet-keys.mp3) · [BROKEN KEY](docs/sounds/broken-key.mp3) | | |
| [GHOST KEYS](docs/sounds/ghost-keys.mp3) · [DIRTY ORGAN](docs/sounds/dirty-organ.mp3) | | |

## Install

Current version: **[Jangada 0.5](https://github.com/zednaked/jangada/releases/tag/v0.5)** (alpha).
Plug the FM-1 straight into the computer with a USB **data** cable.

**Mac / Windows / Linux, in the browser**: open the
**[Jangada web installer](https://zednaked.github.io/jangada/)** in Chrome or Edge and press
*Install*. It carries the latest release; the web editor is next to it.

**Linux, from a terminal**: clone this repository and run

```
./instalar-linux.sh                    # downloads and installs the latest release
./instalar-linux.sh jangada-0.5.fwsc   # installs a file downloaded from the releases
./instalar-linux.sh --original         # back to M-VAVE's official firmware (V15)
./instalar-linux.sh --info             # what the FM-1 is running
./instalar-linux.sh --console          # serial console access (a udev rule, asks for sudo)
```

It sets up its own Python environment (`mido` + `python-rtmidi`) in `~/.local/share/jangada/` and
checks the SHA-256 of what it downloaded.

- **If an install fails**: hold **OCT−** while switching the FM-1 on (USB rescue) and install again.
- **Back to the official firmware**: `./instalar-linux.sh --original`, or M-VAVE's M-UPGRADE.
- Since 0.2 the FM-1 shows up on the computer as **Jangada** (MIDI and audio). Felucca's web
  installer no longer finds an FM-1 running Jangada: use Jangada's.

## What's new over Felucca

### Performance: hold a button
Tap a function button and its pages open, as always. **Hold** it and it becomes a **layer**: the 16
white keys and the 4 knobs change job while it is held, and the screen shows the keys as 16 tiles
(4 × 4) and the knobs as dials. **HOME** tapped while a layer is held **locks** it open (both hands
free); any other button lets it go. PLAY, REC and OCT keep working inside it. Idea and much of the
code from [SLOOP](https://github.com/isod89/sloop-fm1).

| Hold | Keys | Knobs 1 · 2 · 3 · 4 |
|---|---|---|
| **FX**: punch | 16 effects on the whole mix while the key is held: loops 1/4 to 1/32, stutter, reverse, tape stop, half, LP / HP sweep, phone, crush, alias, gate, echo, wobble | FILT · DUST · DUCK |
| **GLO**: mix | 1–4 mute, 5–8 solo, the last one tap tempo | levels of tracks 1–4 |
| **SEQ**: steps | the 16 steps of the page: empty = set with the last note, set = press and release clears. Black keys: F# G# A# C# = page; D#4 / F#4 shift, G#4 / A#4 half / double, C#5 / D#5 transpose, **F#5 held erases** what the playhead passes | NOTE · DIV · SWG · LEN; steps held: NOTE · RTCH · CHNC · FLAG |
| **SCL**: key | any key = the key of the song (every track) | CHRD · SCL · QNT · TRN |
| **EDIT**: engine | 1–9 = the track's engine; the last one = track 4 DRUM / SYNTH | PRST · VOICE · GLIDE · LVL |

With **SEQ** held, **OCT− / OCT+** = undo / redo of the pattern.

### Master
**GLO → MASTER** (and the FX layer's knobs): **DUST** (an old sampler and a record: bits, rate,
crackle while playing), **DUCK** (the kick dips the synths for an eighth note), **FILT** (DJ
filter: left low-pass, right high-pass).

### One-key chords
**SCL → CHORD** (or knob 1 of the SCL layer): OFF, TRIAD, 7TH, 9TH, SUS4, POWER. On, the white keys
walk the scale from C4 and each plays the whole chord of the scale (recorded as a chord in the
step). The track goes POLY by itself.

### TRACKS
**REC** on a page with nothing to record opens the tracks view: BPM, bar.beat, one row per track
with the sound, the engine, the steps and the playhead, the level and the REC / SOLO / MUTE badges.

### Sound
- **A bigger ANALOG** (EDIT 3 / 4): **SUPR** superwave (up to 6 detuned copies of the
  oscillator), **SDTN** spread, **SUB** a square an octave down, **DRFT** slow per-voice drift,
  **FTYP** LP12 / LP24 / BP / HP. With many voices the superwave keeps fewer copies, to fit the
  CPU (8 voices of SUPER SAW: 55 % on the FM-1).
- **Modulation matrix**: LFO button → pages **MOD 1–4**. Each slot: source (LFO, ENV, VEL, KEY,
  RND) → target (filter, pitch, shape or any engine parameter) × amount.
- **16 parameters per engine** (Felucca has 8).
- **20 new presets**: dark and industrial textures, superwaves and six drones.
- The GM kit's **hi-hats and crash** play their own samples (they sounded like toms).
- **FM6**: 6-operator FM (Dexed's msfa core, as ported by Felucca 1.0), 32 algorithms, 8 factory
  patches (PTCH F1–F8), a **bank of 32 patches in flash** (PTCH B1–B32) and macros on the knobs (ALG FB
  MLVL MRAT MEG VMOD DTUN). Each track's patch goes with the project, the autosave and the backup (an
  older project opens with its PTCH's patch). The 4-operator DIGITAL stays.
- **Dexed / DX7 over USB**: Dexed (or another DX7 editor) edits the selected FM6 track live (parameter
  change), sends it a voice (VCED) or a bank of 32 (VMEM) into B1–B32, and asks for the track's voice or
  the bank back (dump request). The bank is written with the music stopped.
- **Synthesised drums** (from SLOOP): **GLO → KIT**, or the PRESETS knob on the drum track: the sampled
  GM kit or 37 synthesised kits (808, 909, TECHNO, INDUSTR, GLITCH, DUBSTEP, JUNGLE…).
- **Jangada's own kits**, first in the list: **RUST** (dry industrial: distorted kick, gated snare,
  crushed hats), **FORGE** (anvil, chains, sheets: ringing metal), **PISTON** (machines: clicks, steam,
  valves), **HURT** (low, muffled, breathing) and **MANGUE** (alfaia, zabumba and bacalhau, maracatu
  snare, gonguê, agogô, cuíca, ganzá, triangle, claps, with manguebeat weight).
- **Factory beats** (**GLO → KIT**, knob 4 **BEAT**): MARACATU (baque virado), BAIAO, COCO, GRIND,
  ANVIL, ENGINE, FRAGILE. A Jangada kit brings its own beat to an empty drum track; over a pattern of
  yours, BEAT asks for a second turn.
- **Nordeste**: the **NORD** scale (SCL; mixolydian with a raised 4th, the northeastern mode: NORD or
  MIX for baião, DOR for xote and toada), and the **BAIAO BASS** and **RABECA** (ANALOG) and
  **SANFONA** (WHEEL) presets, the last two as drones.
- **Reverbs** (**FX → REVERB**, TYPE): ROOM (as before), SPRING (from Felucca 1.0) and PLATE (a stereo
  feedback delay network, from SLOOP).

### MIDI
- **TRS MIDI input** (the FM-1's 3.5 mm jack): a MIDI keyboard plays as over USB. Channels 1–3
  play the synth tracks, 4 track 4 when it is SYNTH, the drum channel (GLO → DRUMS, default 10) the
  drums, any other the selected track.
- **Pitch bend** (±2 semitones), **sustain** (CC64), **all notes off** (CC120 / 123), reset (CC121).
- **Mod wheel**, **aftertouch** and **expression** (CC11) as matrix sources (MODW, AT, EXPR).
- **GLO → GLOBAL CLK USB** or **TRS**: follows the MIDI clock of the computer or of the TRS jack, pulse
  by pulse (24 a beat, no drift): the steps, BPM, punch FX, DUCK and the TRACKS screen follow it; START
  starts from the top, CONTINUE carries on where it stopped, STOP stops; with no pulse for 0.5 s the
  internal clock takes over.
- **GLO → SYSTEM SYNC OUT**: sends clock over USB (24 a beat, start, stop); never while following a
  clock (CLK USB or TRS).

### USB audio
The FM-1 shows up on the computer as a stereo audio input (44.1 kHz, "Jangada"), no driver needed:
record the master straight into the DAW (HOME → USB AUDIO: the level follows MASTER, or FULL) (on Linux: `arecord -D hw:Jangada -f S16_LE -r 44100 -c 2 take.wav`).

### Web editor and installer
On the **[Jangada site](https://zednaked.github.io/jangada/)** (Chrome or Edge):
- **Backup and restore** (editor → Projects → Backup): everything on the FM-1 in one file
  `jangada-backup-DATE.json` (the working project, the 4 projects, the 32 presets, the samples
  USR1–3, the settings), and back. A damaged file is refused before anything is written.
- **6-OP FM** (editor): the whole FM6 patch (the 6 operators, LFO, pitch EG), live on the track; the
  bank B1–B32; DX7 `.syx` import and export (one voice, or the bank of 32, all of it into B1–B32 with
  one button).
- **CHOP** (editor → Samples): cut a recording of any length into up to 16 chops, keep the ones you
  want, shorten them, *Fit to slot*, and send them to USR1–3.
- **Back to the official firmware** from the installer (M-VAVE's FM-1 V15 file), a backup first.

### Storage and safety
- **Autosave**: stopped and untouched for a few seconds, the project goes to flash and comes back at
  power-on. **OCT+** held at power-on starts empty; **HOME → NEW PROJECT** clears everything.
- **Safer updates** (from SLOOP): the installer refuses a damaged package; the loader checks the CRC
  before it lets the new firmware start.
- **USB rescue**: **OCT−** held at power-on (or two failed boots) opens JANGADA USB RESCUE, where only
  the installer runs. Panel calibration: **OCT− + OCT+** at power-on.
- Menu (hold **HOME**): COLOR, SPEAKER (low cut for the speaker), LIGHTS, KEYS, NOTES, USB AUDIO, NEW
  PROJECT, ABOUT. KNOB 1 sets the row's value.
- **Lights for the dark**: **LIGHTS** OFF / LOW / MID / HIGH makes every button glow dimly (the labels
  read in the dark; the lit ones stay full); **KEYS** lights the C keys or every white key too; **NOTES**
  lights the key of every sounding note (sequencer, MIDI, drums), on every page and layer. The glow is a
  short pulse on every scan: no flicker. Kept with the device's settings, not in the project.
- **USB AUDIO**: MASTER (the recording follows the MASTER knob) or FULL (a fixed level, MASTER all the way up).

### Drones
The DRONE presets run the arpeggiator in **RPT** every **4 bars** with **HOLD**: play a chord,
let go, and it keeps breathing on its own — also while you play the other tracks.
- **Hold ARP** → DRONE OFF: the latched chords are released (they fade with the preset).
- **Hold ARP again** → SILENCE: the tails stop now.

To sequence a drone: arp OFF, PATTERN **DIV 4BAR**, one chord a step (each step lasts 4 bars).

**Drones that evolve.** Tap ARP to the **DRONE** page (HOLD, EVOL, TENS, RAMP), per track:
- **EVOL**: while the track sounds, slow, smooth random walks move the sound on their own (cycles of
  tens of seconds to minutes, never the same twice): the filter, the shape, the engine's natural
  parameter (the superwave's MIX, the FM index, the grain size…), and every voice drifts in tune and
  breathes its own way. It stops when the drone stops; a new drone starts from the preset's sound.
- **TENS**: the tension. Opens the filter, raises resonance / drive / brightness and the track's DIST,
  pulls the voices apart in tune (up to ±12 cents) and makes the walks deeper and faster. At 0, the
  sound of today.
- **RAMP**: OFF, 1 to **32BAR**: the tension travels to TENS over that many bars (at the tempo). A drone
  born from silence starts at 0 and builds; turning TENS down goes back as slowly.
- The screen shows the four walks and the tension bar rising to the TENS mark.
- In the matrix, the **DRIFT** source is the track's walk, for any target.
- Presets: **FERRUGEM** (ANALOG, superwave), **ABISMO** (DIGITAL, FM), **SERTAO** (WHEEL, the sanfona
  going sour) and **CINZA** (GRAIN, piano ash).

### Arpeggiator and sequencer
- Arp modes **UDI** (up-down, ends repeated) and **RPT** (the whole chord each step); divisions
  **1/2, 1/1, 2BAR, 4BAR** (sequencer too).
- **STEP 2** page: **RTCH** ratchet x1–x4 and **CHNC** chance 100/75/50/25 % per step.

### Tracks
- **Track 4: DRUM or SYNTH.** On **TRACKS**, pick track 4 with ALGORITHM and turn **knob 1
  (TYPE)**: SYNTH makes it a fourth synth part (engine, preset, arp, sequencer, MIDI channel 4);
  DRUM brings the GM kit back.

### Screen and storage
- The **CHOQUE** palette (shocking pink) by default; the others stay in the menu (hold HOME → COLOR).
- Projects (**JNG1**) and user presets (**UPB2**) store every value with a **stable key**:
  parameters can be added or moved without losing what you saved. Felucca's projects and
  presets are read and converted.

## Tools

| | |
|---|---|
| `tools/fm1_console.py status` | CPU, audio, USB, battery |
| `tools/fm1_console.py check` | on-device test: CPU peak, late audio, resets |
| `tools/fm1_console.py voices` | what sounds on each track, and why |
| `tools/fm1_console.py preset E I [T]` | load preset I of engine E on track T |
| `tools/fm1_console.py t4 synth\|drum` | track 4's type |
| `tools/fm1_console.py droneoff` | as holding ARP |
| `tools/fm1_console.py g ID [VALUE]` | read or set a global parameter (e.g. `g 28 90` = DUST) |
| `tools/fm1_console.py punch N\|off` | start a punch-in effect (0–15) or stop it |
| `tools/fm1_console.py color CHOQUE` | the screen palette |

## Build and test

See [BUILDING.md](BUILDING.md). On Linux x86-64 JieLi's toolchain runs natively, no Docker:

```
tools/get_toolchain.sh        # the toolchain, in ~/.jieli
tools/get_sdk_files.sh        # only the 3 files of the AC79 SDK the package needs
./build.sh                    # build/felucca.fwsc
sh tests/run_tests.sh         # every test, on the PC
```

- **Reproducible builds**: the date comes from the last commit; two builds give the same bytes.
- The tests cover the sound (fingerprinted renders of every preset), health (clipping, DC, stuck
  notes), CPU budgets (instruction counters on Linux and the Mac), storage formats, arp, steps,
  the matrix, track 4, the installer and the web editor, whose mock tables are generated from
  the firmware (`tools/gen_editor_tables.py`).
- **CI** on every push; a `vX.Y[-suffix]` tag publishes the `.fwsc` in a release.

## Next

1. ~~Grit on the master~~ (done: TAPE, HUM, FUZZ / FOLD / CRUSH / RING).
2. ~~Our own kits~~ (done: RUST, FORGE, PISTON, HURT, MANGUE; maracatu, baião, coco; NORD scale).
3. ~~The Studio in the browser~~ (done: `webapp/studio/` on the site).
4. ~~An FM6 patch bank~~ (done: B1–B32 in flash, the patch in the project, DX7 `.syx` in the editor,
   Dexed editing Jangada live over SysEx).
5. Finer MIDI (per-channel bend range, CC1 vibrato) and the chord name on HOME.

Then: ~~drones that evolve~~ (done: EVOL, TENS, RAMP, DRIFT), broken pianos, RUST, ASH and MANGUE palettes, the jangada on the boot screen.

## Credits and license

The layers, punch FX, master and chords come from [SLOOP](https://github.com/isod89/sloop-fm1)
(GPL-3.0), another Felucca fork; the FM6 bank and the DX7 SysEx from Felucca 1.0 and
[Melodee](https://github.com/keremimo/melodee) (GPL-3.0, Kerem Kilic / Ellic Studio). Jangada is GPL-3.0-only, as Felucca is. The original work is **Leo Kuroshita's (@kurogedelic),
Hügelton Instruments** — see [README.felucca.md](README.felucca.md) and [LICENSING.md](LICENSING.md)
for the full credits (fonts, samples, engines).

M-VAVE and FM-1 are trademarks of their owners. Jangada is not affiliated with or endorsed by
them, nor by Felucca.
