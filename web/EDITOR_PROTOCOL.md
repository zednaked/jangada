# Felucca editor protocol (SysEx over USB-MIDI)

The firmware side is `firmware/src/editor.c`. Commands 16-26 (user presets and live sync) form protocol v2; commands 27-30 (tracks) form protocol v3; commands 31-32 (any track's parameters) form protocol v4; commands 34-36 (Jangada: backup / restore, `firmware/src/editor_backup.c`) form protocol v5; v6 (Jangada 0.5)
adds the FM6 patch bank to the backup (objects 8, 9) and the FM6 patch commands 68-72 (`firmware/src/editor_fm6.c`,
numbered as Felucca 1.0 numbers them; 72 and PUT target 3, the staged bank half, came after 0.5: INFO says `46 02`;
see "FM6 patches"); v7 (Jangada 0.6) has two FM6 banks of 32 (B1..B64, INFO says `46 03 08 40`) and adds the
second bank to the backup (objects 10, 11).

**v3 (four tracks):** the device has four tracks: 1..3 are synth parts, 4 is the drum track. One
of them is *selected* (the TRACKS page on the device, or `TRACK`). Every v1 / v2 command acts on the
selected track (its parameters, engine, preset, steps, the user presets it stores or loads); `TRACK`,
`TRACK_MIX`, `TRACK_DUMP` and `TRACK_STEP` reach any track. Command numbers 1-26 are unchanged.

**v4 (any track's parameters):** `TRACK_PARAM` gets or sets a parameter of any track without changing
the selection, and the `TRACK_CHANGED` push follows level, pan and mute of the tracks that are not
selected. The v1-v3 commands are byte for byte as before; v4 is asked for with bit 1 of `WATCH`.

## Framing

A request is `F0 7D 46 4C <cmd> <args...> F7`:

- `7D` is the non-commercial SysEx ID.
- `46 4C` is "FL".

Every request gets exactly one reply, with the same header and the same `<cmd>`. Requests
the device does not understand get no reply. Every data byte is 7 bit. While the editor
watches (v2, `WATCH`), the device also sends push frames (cmds 23, 24, 26) at any time.

| Item | Encoding |
| --- | --- |
| value (v14) | 2 bytes, LSB first, holding value + 8192, so the range is -8192..8191. `[lo, hi]`: value = (lo \| hi << 7) − 8192 |
| string | ASCII bytes, ended by a 0 byte |
| scope | 0 = parameter of the selected track (`P_*`, 0..P_COUNT−1); 1 = global parameter (`G_*`, 0..G_COUNT−1) |
| track | 0..3: tracks 1..3 (synth parts), 3 = the drum track |
| engine byte | 0..NENGINES−1; NENGINES = the drum track (it has no engine and no presets) |

The engine parameters are `P_E0..P_E7`: P_COUNT−8 .. P_COUNT−1, and `INFO` gives `P_E0`.
Their meaning, range and names depend on the current engine, so re-read `DESC` for them
after an engine change.

## Commands

| cmd | Request args | Reply args |
| --- | --- | --- |
| 1 INFO | — | version string, NENGINES, P_COUNT, G_COUNT, NSTEP, P_E0, then NENGINES engine-name strings, then (v3) NTRK (4), then (v5, Jangada) the protocol version (5; 6 since Jangada 0.5; 7 since Jangada 0.6); older firmware ends after the names or after NTRK |
| 2 GET | scope, id | scope, id, v14 |
| 3 SET | scope, id, v14 | scope, id, v14 (the value after clamping). Setting global `G_ENGSEL` (id from DESC label "ENG") changes the engine with its defaults |
| 4 DUMP | — | engine, preset, then P_COUNT × v14 (the selected track), then G_COUNT × v14 (globals) |
| 5 DESC | scope, id | scope, id, fmt, min v14, max v14, def v14, label string, unit string, then for an enum (fmt 8) one name string per value (at most 16) |
| 6 STEP_GET | index 0..NSTEP−1 | index, n (0..4 notes), note0..note3, time (0 NOTE, 1 TIE, 2 REST), flags (1 accent, 2 slide), vel |
| 7 STEP_SET | index, n, note0..3, time, flags, vel | same as STEP_GET (after the write) |
| 8 PRESET | engine, preset | engine, preset (applies the preset: sound, sends, arp, and its pattern if the sequencer is empty or still holds an untouched preset pattern) |
| 9 PROJECT | op (0 load, 1 save, 2 query), slot 0..3 | op, slot, used (1/0). Save writes flash: allow ~2 s |
| 10 NAMES | engine | engine, count, count preset-name strings, then the two edit-page titles |
| 11 SMP_BEGIN | slot 0..2 | slot, rc (0 ok). Erases the slot's header sector: the slot is empty from now on |
| 12 SMP_WRITE | slot, offset (3 × 7 bit, LSB first), pack7 data (≤ 256 bytes) | slot, offset, rc: 0 ok, 1 arguments, 2 erase, 3 write, 4 slot in use (send SMP_BEGIN first). Offset ≥ 512 and a multiple of 256; writes go in increasing order (a write at a 4 KiB boundary erases that sector) |
| 13 SMP_END | slot, pack7 header (480 bytes) | slot, rc: 0 ok, 1 size, 2 header, 3 data CRC, 4 flash, 5 zones |
| 14 SMP_ERASE | slot | slot, rc (erases the whole slot, ~1 s) |
| 15 SMP_INFO | — | slots, slot KiB, then per slot: zone count (0 = empty), name string, data KiB |
| 16 UP_LIST | start, count (1..16) | start, count, total slots, then per slot: used (0/1), engine, name string ("" if unused) |
| 17 UP_GET | slot | slot, used, engine, name, P_COUNT × v14, 16 × (note, flags) |
| 18 UP_PUT | slot, engine, name, P_COUNT × v14, 16 × (note, flags) | slot, rc (0 ok, 1 args, 2 flash). Writes flash: allow 1 s |
| 19 UP_STORE | slot, name | slot, rc. Stores the current sound: engine, parameters, the first 16 sequencer steps as the pattern (TIE steps → flag 4) |
| 20 UP_LOAD | slot | slot, rc (0 ok, 1 empty/invalid). Applies it |
| 21 UP_ERASE | slot | slot, rc |
| 22 WATCH | on (0/1; v4: 3 = also `TRACK_CHANGED`) | on (0/1; v4 firmware: 3 when 3 was asked for). While on, the device pushes cmds 23, 24, 26 (and 32 with bit 1) |
| 23 CHANGED (push) | — | scope, id, v14 |
| 24 RELOAD (push) | — | engine, preset, then (v3) the selected track |
| 25 PING | — | 0 |
| 26 STEP_CHANGED (push) | — | index, then (v3) the selected track |

| cmd (v3) | Request args | Reply args |
| --- | --- | --- |
| 27 TRACK | — (query), or track (select it) | selected track, NTRK, then per track: engine byte, preset, level v14, mute (0/1), armed (0/1, live recording) |
| 28 TRACK_MIX | track (get), or track, level v14 (0..127), mute (set) | track, level v14, mute. The drum track's level is global `G_DRLVL` (GLO > DRUMS LEVEL); mute is the track's `P_MUTE` |
| 29 TRACK_DUMP | track | track, engine byte, preset, P_COUNT × v14 (that track's parameters; no globals) |
| 30 TRACK_STEP | track, index (get), or track, index, n, note0..3, time, flags, vel (set) | track, index, n, note0..3, time, flags, vel |

| cmd (v4) | Request args | Reply args |
| --- | --- | --- |
| 31 TRACK_PARAM | track, id (get), or track, id, v14 (set); id = `P_*` (0..P_COUNT−1) | track, id, v14 (the value after clamping, as `SET`). The selection does not change; no push about the editor's own write |
| 32 TRACK_CHANGED (push) | — | track, id, v14: `P_LEVEL`, `P_PAN` or `P_MUTE` of a track that is not selected changed on the device (only while `WATCH` was sent with bit 1) |

**pack7:** groups of up to 7 bytes, each preceded by one byte holding their top bits
(bit j = bit 7 of byte j).

**User sample slot** (80 KiB each, SAMPLE engine sets USR1..USR3; reference uploader
`tools/fm1_sample_upload.py`, slot builder `sampleio.user_slot`; the editor's port of it is
checked byte for byte by `web/test_web.mjs`): header at 0, ADPCM data at 512.

| Offset | Field |
| --- | --- |
| 0 | magic `"FSMP"` (u32 0x504D5346), u16 version 1, u8 zone count 1..16, u8 0 |
| 8 | name, 8 ASCII bytes (0-padded) |
| 16 | u32 data length (bytes), u32 CRC-32 (zlib) of the data, 8 bytes 0 |
| 32 | 16 zones × 28 bytes: u32 off (in the data), n (samples), loop start, loop end, rate (Hz / 44100 × 65536); i16 root × 16 (MIDI note), ADPCM predictor at the loop start; u8 step index at the loop start, lo note, hi note, looped (0/1) |

Data is IMA ADPCM, 4 bit, low nibble first, starting from predictor 0 and step index 0.
All little endian.

`fmt` values (`firmware/src/core.h`):

| Value | Name | Value | Name | Value | Name |
| --- | --- | --- | --- | --- | --- |
| 0 | INT | 5 | CUTOFF | 10 | NOTE |
| 1 | PCT | 6 | DB | 11 | ONOFF |
| 2 | BIPCT | 7 | SEMI | 12 | OCT |
| 3 | TIME | 8 | ENUM | 13 | STEPS |
| 4 | LFOHZ | 9 | BPM | | |

The editor should show the value with the unit; formatting it exactly like the device does
is not required.

## v2: user presets

A user preset = engine (0..NENGINES−1), name (1..12 chars, ASCII 32..126; the device shows it upper
case), all P_COUNT instrument parameters (v14 each, the same order as `DUMP`), and a 16-step pattern:
16 × (note 0..127 (0 = rest), flags: 1 accent, 2 slide, 4 tie). Loading one applies the engine and
all parameters; the pattern is loaded only if the sequencer is empty or still holds an untouched preset pattern (as factory presets), and then
LEN becomes the stored LEN, at most 16. The slots are numbered 0..31 (the device shows U01..U32).

- `UP_LIST`: count is cut at 16 and at the last slot (start ≥ 32: count 0, no entries).
- `UP_GET` of an empty slot has the same shape with used 0, engine 0, name "" and all values 0.
  Values come back in the current parameter order, inside their ranges.
- `UP_PUT`: rc 1 for a slot ≥ 32, an engine ≥ NENGINES, a name that is empty, longer than 12 or has
  bytes outside 32..126, or a frame that is too short. Values are clamped to their ranges for that
  engine. A note with flag 4 is stored as a tie (note 0); flags on a rest are dropped.
- `UP_STORE`: name "" stores with the automatic name the device uses (engine name + slot number,
  "ANALOG 07"). rc 1 for a bad slot or name.
- rc 2 = the flash write failed or there is no flash; the slot is still changed in RAM until power-off.
- Frames stay below 640 bytes (`UP_PUT` is 5 + 1 + 1 + 13 + 2 × P_COUNT + 32 + 1).

**On the device:** SAVE > USER page: KNOB 1 picks the slot, KNOB 2 LOAD, KNOB 3 ERASE, KNOB 4 SAVE
(one detent arms, a second one within ~1.5 s acts, as PROJECT LOAD / SAVE). SAVE uses the automatic
name. SELECT and the SAVE > PRESETS browser continue past the factory presets into the used user
presets.

**Flash** (`firmware/src/upreset.c`): two storage objects (`OBJ_UPRESET0/1`, A/B sector pairs at
0xDC000..0xDFFFF), 16 records of 192 bytes each, behind a bank header (magic "UPB1", record size,
slot count; a mismatch reads as an empty bank). A record keeps its layout version (mismatch: empty)
and the P_COUNT it was stored with; another count is mapped by count (last 8 values = P_E0..P_E7, the
first ones = P_LEVEL.. in order, missing ones = defaults). P_COUNT was 53 (P_E0 45) until the SLICER
parameters (SLCR, PAT, RATE, DEPTH: ids 45..48) went in just before P_E0: P_COUNT 57, P_E0 49. An
editor takes both from `INFO`; records stored with 53 load with the SLICER off.

## v2: live sync

- `WATCH 1` starts the pushes. Watching ends by itself 3 s after the last request of any kind (send
  `PING` about every 1 s), on a USB reset, and when the host goes away; `WATCH 0` ends it at once.
- **CHANGED** (scope, id, v14): a parameter changed on the device (knob, menu, sequencer edit of a
  `P_*`), not by the editor's own `SET`. Coalesced: each (scope, id) at most every 20 ms, with the
  latest value.
- **RELOAD** (engine, preset): the engine, a preset, a user preset or a project was loaded; re-read
  `DESC` of the engine parameters, `DUMP` and the steps. It is also sent after loads the editor asked
  for (`SET` of G_ENGSEL, `PRESET`, `PROJECT` load, `UP_LOAD`).
- **STEP_CHANGED** (index): a sequencer step changed on the device (record, clear, step edit,
  pattern load); not after the editor's own `STEP_SET`.
- Push frames have the normal header. Accept them at any time, also while waiting for a reply:
  match replies by cmd (23, 24 and 26 are never replies). The device sends at most a few per
  ~5 ms pass, and only when its USB send queue has room, so a push never delays a reply.

## v3: tracks

- The drum track: `DUMP` / `RELOAD` / `TRACK` give the engine byte NENGINES. Its `P_*` values exist
  (the pattern parameters `LEN DIV SWG GATE`, `PAN`, `MUTE` are used; the rest is ignored). `PRESET`,
  `SET` of `G_ENGSEL` and `UP_LOAD` do nothing there (`UP_LOAD` and `UP_STORE` answer rc 1). `DESC` of
  `P_E0..P_E7` describes engine 0. Its steps hold GM drum notes (up to 4 per step).
- Selecting a track with `TRACK` does not push `RELOAD` (the editor re-reads `DUMP`, the steps and the
  engine `DESC` itself); selecting one on the device does (`RELOAD` with the new track).
- Pushes are about the selected track only: `CHANGED` (scope 0) and `STEP_CHANGED` refer to it, and
  changes to other tracks (live recording from MIDI into another track, `TRACK_*` writes) push nothing.
- Level and mute are also `P_LEVEL` / `P_MUTE` of the selected track (`SET`); `TRACK_MIX` reaches the
  others. Presets and user presets change a part's sound but keep its `P_LEVEL`, `P_PAN`, `P_MUTE`.
- Projects (`PROJECT`) save and load all four tracks and the selection (project format 2; a format 1
  project from older firmware loads into track 1).
- Older firmware (no NTRK in `INFO`): one instrument; skip the track UI.

## v4: any track's parameters

- **Finding out:** send `WATCH 3`. v4 firmware answers 3; v3 (0.8) firmware answers 1, does not know
  cmds 31 / 32 (no reply) and never pushes `TRACK_CHANGED`. `WATCH 1` behaves exactly as in v2 / v3
  (reply 1, no `TRACK_CHANGED`). Match the `WATCH` reply by bit 0.
- `TRACK_PARAM` clamps like `SET` scope 0: to the range of that parameter; the engine parameters
  `P_E0..P_E7` to the ranges of that track's engine (the drum track: engine 0, as `DESC`). A parameter
  with a fixed range (min = max) keeps its value. A track ≥ NTRK or an id ≥ P_COUNT gets no reply.
  For the selected track it is the same as `SET` scope 0. The drum track's level is still `G_DRLVL`
  (`SET` scope 1 or `TRACK_MIX`); its `P_LEVEL` is not used.
- `TRACK_CHANGED` is never about the selected track (its changes stay `CHANGED` scope 0). Coalesced like
  `CHANGED` (each track and id at most every 20 ms, latest value), and not sent for the editor's own
  `TRACK_PARAM` / `TRACK_MIX` writes. After a selection change (`RELOAD`, or the editor's `TRACK`) the
  device takes the current values as known.

## v5 (Jangada): backup / restore

From SLOOP 2.3 and Felucca 1.0.1 (the same commands as SLOOP's v6, numbered as SLOOP numbers them; 33 is
SLOOP's `DRUM_STEP`, not in Jangada). `INFO` ends with 5. The editor and the installer use it through
`web/fm1backup.js`.

Requests name **objects**, never flash addresses:

| id | object | bytes |
| --- | --- | --- |
| 0 | the working project (as it is now) | "JNG1" (`project.c` `proj_to_jng`, as the autosave stores it) |
| 1 | the settings | `persist_t` "PER2": palette, low cut, zoom (reserved since Jangada: written 0, 0 / 1 accepted), the panel calibration (`panel_t`), the lights word (LIGHTS / KEYS / NOTES / USB AUDIO); one without the lights word (Jangada 0.2) is restored too, with the lights off |
| 2..5 | the projects 1..4 | "JNG1"; length 0 = empty slot |
| 6..7 | the user preset banks (presets 1..16, 17..32) | `up_bank_t` "UPB2" (`upreset.c`, keyed); length 0 = empty |
| 8..9 | (v6) the FM6 patch bank 1, B1..B16 and B17..B32 | `fm6_half_t` "FM6B" (`fm6_bank.c`): magic, version 1, 16 slots, the used bits, the half (0 / 1), 16 packed 128-byte records; 2064 bytes, length 0 = empty |
| 10..11 | (v7) the FM6 patch bank 2, B33..B48 and B49..B64 | the same `fm6_half_t`, the half 2 / 3 |
| 32..34 | the user sample slots USR1..3 | header + ADPCM data as in flash (512 + data length); 0 = empty |

Numbers are 5 × 7 bit, LSB first (u35); data is pack7. Objects 0..7 are at most 3840 bytes (one storage
object), a sample slot at most 80 KiB.

| cmd (v5) | Request args | Reply args |
| --- | --- | --- |
| 34 BK_LIST | — | rc (0 ok, 4 no flash), count (15; 13 from v6 firmware, without 10 / 11; 11 from v5 firmware, without 8..11), then per object: id, length u35, CRC-32 u35 (zlib). Takes a snapshot of the working project for `BK_GET` |
| 35 BK_GET | id, offset u35, count (2 × 7 bit, 1..256) | id, rc (0 ok, 1 arguments, 5 the snapshot is gone: `BK_LIST` again), offset u35, count, pack7 data |
| 36 BK_PUT | op 0 begin: id 0..11 (v6: 0..9, v5: 0..7), length u35, CRC-32 u35 · op 1 data: id, offset u35, pack7 (≤ 256 bytes, in order) · op 2 commit: id · op 3 abort: id | op, id, rc: 0 ok, 1 arguments, 2 not a valid object, 3 stop the song first, 4 flash, 5 no begin for this object (or a USB reset, or more than 15 s since the last request) |

- **Reading.** `BK_LIST` once, then each object from offset 0 in order (object 0 first: the snapshot of
  the working project lives in the device's project buffer, and reading another project, or a project
  save / load on the panel, replaces it: `BK_GET` of object 0 then answers 5). Check each object against
  the CRC of `BK_LIST`; a mismatch means it changed during the backup: start again.
- **Writing.** `BK_PUT` stages one object in RAM; the commit checks the CRC, then the object as a load
  checks it — projects: "JNG1" (or Felucca's FUN3 / FUN2 / FUN1, converted) with its size and sum, stored
  as "JNG1"; banks: magic, record size, slot count, key count (other keys are mapped as at boot);
  settings: its size (with or without the lights word), magic, palette, low cut, a permutation of the
  buttons and knobs; an FM6 bank half: its size, magic, version, slot count, which half it is, every byte
  7-bit — and writes it through the
  usual A/B commit (a cut-off restore leaves the old object or the new one, never half). The working
  project (0) is loaded at once instead of written. Every commit needs the song stopped (rc 3): a flash
  erase stops the audio for a moment. The autosave waits while a backup runs.
- **Samples** are restored with `SMP_BEGIN` / `SMP_WRITE` / `SMP_END` (the header is the first 480 bytes
  of the object, the data from byte 512), an empty slot with `SMP_ERASE`. An interrupted sample restore
  leaves that slot empty.
- **The file** (`jangada-backup-YYYY-MM-DD.json`): `{format: "jangada-backup", version: 1, firmware,
  created, objects: [{id, size, crc, data (base64)}]}`, the 15 objects in the order above (a file of Jangada
  0.5: the 13 without the FM6 bank 2, it restores and leaves the device's bank 2 as it is; of Jangada 0.3 / 0.4:
  the 11 without the FM6 bank, it restores and leaves the device's banks as they are; into v6 firmware the editor
  leaves 10 / 11 out, into v5 firmware 8..11). It is checked whole (every size and CRC, the sample headers as the device
  reads them) before anything is written; restore order: the projects and banks (the FM6 bank too), the
  samples, the settings, the working project last.
- **Projects** carry each track's FM6 patch since Jangada 0.5: "JNG1" byte 11 counts tagged sections after the
  tracks (tag, length u16 LE, data), section 1 = NTRK × the 128-byte packed FM6 record; an unknown section is
  skipped. A project without it (byte 11 = 0, Jangada 0.4) loads with each track's PTCH patch.

## FM6 patches (68-71, Jangada 0.5; after Felucca 1.0)

The FM6 engine (engine 9 in Jangada) plays a 6-operator patch per track; its EDIT parameters are macros on top of it
(ALG 0 = the patch's algorithm, 1..32 another; FB, MLVL, MRAT, MEG, VMOD offsets; DTUN; PTCH 0..71 = F1..F8 the
factory patches, then B1..B32 the bank 1 and B33..B64 the bank 2: setting PTCH loads that patch into the track; the
used bank slots are in the device's PRESETS list too). The patch itself only travels through these commands. `INFO`
advertises `46 vv nfactory nbank` after the protocol version (this firmware: `46 03 08 40`, two banks of 32; `46 02
08 20` is Jangada 0.5.x, one bank; `46 01` is Jangada 0.5: one bank, without PUT target 3 and `FM6_COMMIT`, which it
answers with rc 1 and not at all); firmware without it (Jangada 0.4 and before) does not answer 68..72. A bank
index (target 1 / 3, ERASE) is 0..nbank-1.

A patch is the 128-byte packed record of the generic 6-operator voice (the 32-voice bank's record; every byte is
7-bit, so it travels as it is, no pack7). Operators come sixth first: per operator 17 bytes (R1..R4, L1..L4,
break point, left / right depth, curves `LC | RC << 2`, `RS | DET << 3`, `AMS | KVS << 2`, output level,
`MODE | FC << 1`, fine), then pitch EG rates and levels (102..109), algorithm 0..31 (110), `FB | OKS << 3`,
LFO speed, delay, PMD, AMD, `SYNC | WAVE << 1 | PMS << 4`, transpose (24 = none), the name (10 ASCII bytes).
The device stores every value clamped into its range.

| cmd | Request args | Reply args |
| --- | --- | --- |
| 68 FM6_GET | target, index | target, index, rc, then (rc 0) the 128 bytes |
| 69 FM6_PUT | target, index, the 128 bytes | target, index, rc (target 0, a track: that track's patch from now on, PTCH as it is: the device does not reload PTCH's patch over it; target 3: staged, see FM6_COMMIT) |
| 70 FM6_LIST | — | nfactory, nbank, then per slot (factory first): used (0/1), name string ("" if empty) |
| 71 FM6_ERASE | bank index | index, rc |
| 72 FM6_COMMIT | — | rc: the staged bank half (PUT target 3) to flash, one write (`46 02` firmware) |

target: 0 a track's own patch (index 0..3: what it plays and what its project, the autosave and a backup keep; a
PUT is heard at once and keeps PTCH as it is), 1 a bank slot (index 0..63 = B1..B64; a PUT writes flash, allow
1 s; tracks playing that slot reload it), 2 a factory patch (0..7, GET only), 3 a bank slot *staged* (PUT only,
`46 02` firmware): the record goes into a copy of its half (B1..B16, B17..B32, B33..B48 or B49..B64) in the device's RAM, the half's
other slots as they are in flash, and `FM6_COMMIT` writes the half whole: one flash erase (one pause of the
audio) for up to 16 slots instead of one each. Stage the slots of one half, commit, then the other half: a slot
of the other half while one is staged answers rc 5 (commit first). A staging lapses 15 s after its last record,
on a USB reset, when the device uses that RAM for something else (a project save or load from the panel, a
backup request) or when a plain bank PUT / ERASE writes: the commit then answers rc 5 and writes nothing (stage
again). rc: 0 ok, 1 arguments (an unknown target, an index out of range, a record that is not 128 bytes), 2 an
empty bank slot (GET) or a flash error (PUT, ERASE, COMMIT), 3 the song plays (PUT / ERASE of the bank, COMMIT:
a flash erase stops the audio for a moment; stop it first; a staging survives it), 4 a backup holds the
device's buffer (its snapshot being read, or a restore being staged: `LIST` / `BK_GET` / `BK_PUT` within the
last 15 s, until the restore's commit or abort; a bank write then would corrupt them, so PUT / ERASE / COMMIT
of the bank, and a DX7 bank dump, are refused: wait, then try again), 5 nothing staged (COMMIT: or the staging
lapsed) or another half is staged (PUT target 3). The banks are in flash (`fm6_bank.c`: B1..B16 at 0xE5000 /
0xE6000, B17..B32 at 0xE7000 / 0xE8000, B33..B48 at 0x93000 / 0x94000, B49..B64 at 0x95000 / 0x96000) and in a full
backup (ids 8, 9, 10, 11).

The web editor (6-OP FM tab) reads and writes these, and imports / exports the DX7-format SysEx files: a single
voice `F0 43 0n 00 01 1B`, the 155-byte unpacked voice, checksum, `F7` (163 bytes), and 32 voices
`F0 43 0n 09 20 00`, 32 x 128 packed, checksum, `F7` (4104 bytes); the checksum is the two's complement of the
data's sum, 7 bits. Raw 155 / 4096-byte files are read too. A file of one 32-voice bank can go into B1..B32 (or
B33..B64, the editor's bank choice) at once: 16 `FM6_PUT`s of target 3 and a `FM6_COMMIT` per half (two flash writes; on `46 01` firmware 32 `FM6_PUT`s
of target 1, one flash write each).

**The same patches as DX7 SysEx** (Jangada 0.5, `firmware/src/fm6_sysex.c`, after Melodee): the device also takes,
on any channel n, a voice `F0 43 0n 00 01 1B ..` (into the FM6 track: the selected one when it plays FM6, else
track n + 1, else the first FM6 track), a bank of 32 `F0 43 0n 09 20 00 ..` (not while the song plays: into the bank
of that FM6 track's PTCH, B1..B32 or B33..B64; with none on a bank slot the device asks `FM6 BANK 1? SAVE=YES` on
its screen, OCT- / OCT+ choose bank 1 / 2, SAVE writes, any other button or 15 s drop it; a voice or a bank dump
request meanwhile is not taken), parameter changes `F0 43 1n gg pp dd F7` (voice parameter `(gg & 3) << 7 | pp`, 0..154; 155 and the
function group are ignored) and dump requests `F0 43 2n 00 F7` / `F0 43 2n 09 F7` (answered on channel n; the bank
of the FM6 track's PTCH, else bank 1). These
frames never use the editor's frame buffer (the first byte 43 tells them apart), so Dexed and the editor can be
open together.

## Notes for the editor

- **One request at a time.** Wait for the reply, about 10–50 ms, before sending the next.
  The device holds only one incoming SysEx frame.
- **Following the device.** With v2 firmware, `WATCH` and `PING` (above). Older firmware pushes
  nothing (no reply to `PING`): poll `DUMP` about every 300–500 ms while the page is visible.
- **Port.** The device's MIDI port is named "Felucca" (USB 1209:0001). Updates use the same
  port with other SysEx (the `F0 22 24 35 …` keys, `00 59 …` frames); never send those
  from the editor.
- **Safety.** Only `PROJECT` save, the sample-slot commands, `UP_PUT` / `UP_STORE` / `UP_ERASE` and the `BK_PUT`
  commit write flash, and only in the firmware's own storage; never the app or the update area.
