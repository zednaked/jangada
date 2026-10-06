// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
//
// Node checks of the web pages' JS (no browser, no hardware). Run from the repo root:
//   node web/test_web.mjs
// - editor.html: the protocol section (between PROTO-BEGIN/END) against its mock device (v1 commands,
//   the user preset bank / librarian, library files, live pushes, older-firmware fallback, the v3 tracks
//   and the mixer), its tab layout and ja/en strings,
//   and the user-sample pipeline byte for byte against tools/sampleio.py; the CHOP helpers (hits, TAP snap,
//   grid, keep / leave out, own lengths, Fit to slot, the slot, WAV and ZIP writers)
// - fm1pkg.js: productOf and logicalImage on build/felucca.fwsc (skipped without a build)
// - fm1ota.js: a full install and an unplug during the write against a simulated FM-1; the return to the
//   official V15 (only the exact file; its loader resumed, another firmware's loader never written)
// - index_pkg.html: every text in pt / en / ja, the script compiles with the modules inlined
// - the 6-OP FM tab's module (Jangada 0.5): the init voice and factory patches == build/gen/felucca_fm6.h, pack /
//   unpack, DX7-format SysEx in and out; FM6_GET / PUT / LIST / ERASE against the mock, the bank in the backup
// - fm1backup.js (Jangada, v5): a complete backup of the editor's mock device and its restore into an
//   empty one, through the editor's own Link (web/test_backup.mjs checks the module alone)

import { execFileSync } from "node:child_process";
import { existsSync, mkdtempSync, readFileSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import vm from "node:vm";
import { logicalImage, productOf, validateStockPackage, sha256hex, STOCK_V15_SHA256, STOCK_V15_SIZE, STOCK_V15_PRODUCT } from "./fm1pkg.js";
import { Updater, OUR_LOADER, pack7, unpack7 } from "./fm1ota.js";
import * as BK from "./fm1backup.js";

let failed = 0;
const ok = (cond, what) => { console.log(`${what.padEnd(64)} ${cond ? "ok" : "FAIL"}`); if (!cond) failed++; };
const eq = (a, b) => a.length === b.length && a.every((v, i) => v === b[i]);
const py = (code, ...args) => execFileSync("python3", ["-c", code, ...args], { maxBuffer: 1 << 26 });
const HERE = new URL(".", import.meta.url).pathname;

/* ------------------------------------------------------------ editor protocol --- */
const html = readFileSync(join(HERE, "editor.html"), "utf8");
const proto = html.slice(html.indexOf("/*PROTO-BEGIN*/"), html.indexOf("/*PROTO-END*/"));
const E = vm.runInNewContext(proto + `
;({ frame, unframe, parse, req, Link, parseWav, resample, normalize, rootFromName, buildSlot, makeMockDevice, CMD, SMP,
   UP, bank, capturePatch, auditionPatch, startWatch, libraryFile, readLibraryFile, paramKeys, patternFromSteps, stepsFromPattern, upName,
   mixer, GM_DRUM, drumName, parseNotes, FM6,
   CHOP, chopNovelty, chopHits, chopSnap, chopGrid, chopEqual, chopList, chopPick, chopFit, chopZones, wavFile, zipStore })`,
{ setTimeout, clearTimeout, setInterval, clearInterval, console, TextEncoder });

async function editorMock() {
  const m = E.makeMockDevice();
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const link = new E.Link((d) => out.send(d), { timeout: 300 });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = async (r, o) => link.request(r, o);
  const info = E.parse[E.CMD.INFO](await rq(E.req.info()));
  ok(info.nengines === 10 && info.engines[9] === "FM6" && info.engines[5] === "VOICE" && info.engines[6] === "TRIO" && info.engines[7] === "WHEEL" && info.engines[8] === "GRAIN" && info.pcount === 83 && info.pe0 === 64 && info.engines[4] === "SAMPLE",
    "editor: INFO");
  let descs = 0;
  for (let i = 0; i < info.pcount; i++) if (E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))).label) descs++;
  ok(descs === info.pcount, "editor: DESC for every parameter");
  {
    /* the SLICER (core.h P_SLCR..P_SLDEPTH = 45..48, just before P_E0): the mock as params.c has it,
       and a factory preset turns it off as ui.c apply_preset_to does */
    const pc = readFileSync(join(HERE, "../firmware/src/params.c"), "utf8");
    const sd = [];
    for (let i = 45; i < 49; i++) sd.push(E.parse[E.CMD.DESC](await rq(E.req.desc(0, i))));
    ok(sd.map((d) => d.label).join() === "SLCR,PAT,RATE,DEPTH" && sd[0].names.join() === "OFF,GATE,STUT"
      && sd[2].names.join() === "1/8,1/16,1/32,8T,16T,32T" && sd[2].def === 1 && sd[1].min === 1 && sd[1].max === 16 && sd[3].def === 127
      && /\[P_SLCR\] = PE\("SLCR", N_SLCR, 0\)/.test(pc) && /\[P_SLPAT\] = PD\("PAT", F_INT, 1, 16, 1\)/.test(pc)
      && /\[P_SLRATE\] = PE\("RATE", N_SLDIV, 1\)/.test(pc) && /\[P_SLDEPTH\] = PD\("DEPTH", F_PCT, 0, 127, 127\)/.test(pc)
      && /N_SLDIV\[\] = \{"1\/8", "1\/16", "1\/32", "8T", "16T", "32T"\}/.test(pc),
      "editor: SLICER parameters 45..48 (mock == params.c)");
    await rq(E.req.set(0, 45, 2));
    await rq(E.req.set(0, 46, 7));
    const on = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    await rq(E.req.preset(0, 1));
    const off = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
    ok(on.p[45] === 2 && on.p[46] === 7 && off.p[45] === 0 && off.p[46] === 1, "editor: a factory preset turns the SLICER off");
  }
  const scale = E.parse[E.CMD.DESC](await rq(E.req.desc(0, 26)));
  const scaleNames = ["CHR", "MAJ", "MIN", "DOR", "MIX", "PEN", "MPEN", "HARM", "PHRY", "LYD", "LOC", "MEL", "BLUES", "WHOLE", "DIMHW", "DIMWH", "NORD"];
  ok(scale.label === "SCL" && scale.max === 16 && eq(scale.names, scaleNames.slice(0, 16)), "editor: scale names exposed (16 a DESC; NORD, the 17th: by number)");
  const scaleSet = E.parse[E.CMD.SET](await rq(E.req.set(0, scale.id, 16)));
  ok(scaleSet.value === 16, "editor: new scale selection is not clamped to the old range");
  const dump = E.parse[E.CMD.DUMP](await rq(E.req.dump()), info);
  ok(dump.p.length === info.pcount && dump.g.length === info.gcount, "editor: DUMP");
  const set = E.parse[E.CMD.SET](await rq(E.req.set(0, 3, 500)));
  ok(set.value === 127, "editor: SET clamps to the range");
  const st = E.parse[E.CMD.STEP_SET](await rq(E.req.stepSet(5, { n: 2, notes: [60, 64], time: 0, flags: 1, vel: 100 })));
  ok(st.n === 2 && st.notes[1] === 64 && st.vel === 100, "editor: STEP_SET");
  const pj = E.parse[E.CMD.PROJECT](await rq(E.req.project(1, 2), { timeout: 4000, retries: 0 }));
  ok(pj.used === 1, "editor: PROJECT save");
  /* sample upload as smpUpload() does it */
  const s = Int16Array.from({ length: 3000 }, (_, i) => Math.round(8000 * Math.sin(i / 7)));
  const { hdr, data } = E.buildSlot("test", [{ s, root: 60 }]);
  let rc = E.parse[E.CMD.SMP_BEGIN](await rq(E.req.smpBegin(1), { timeout: 1000, retries: 0 })).rc;
  for (let off = 0; off < data.length && !rc; off += 256) {
    rc = E.parse[E.CMD.SMP_WRITE](await rq(E.req.smpWrite(1, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 })).rc;
  }
  rc = rc || E.parse[E.CMD.SMP_END](await rq(E.req.smpEnd(1, hdr), { timeout: 2000, retries: 0 })).rc;
  const si = E.parse[E.CMD.SMP_INFO](await rq(E.req.smpInfo()));
  ok(rc === 0 && si.slots[1].zones === 1 && si.slots[1].name === "TEST", "editor: sample upload (CRC checked by the mock)");
  /* a device that never answers */
  const dead = new E.Link(() => {}, { timeout: 30 });
  const err = await dead.request(E.req.info(), { retries: 1 }).then(() => null, (e) => e.message);
  ok(/^timeout/.test(err || ""), "editor: no reply -> timeout after the retries");
  link.close();
  m.stop();
}

/* ------------------------------------- editor protocol v2: librarian + live --- */
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const js = (x) => JSON.stringify(x);
/* a mock + Link pair; counts frames sent per cmd and timeouts reported */
function attachMock(opt, linkOpt = {}) {
  const m = E.makeMockDevice({ auto: false, ...opt });
  const inp = [...m.access.inputs.values()][0], out = [...m.access.outputs.values()][0];
  const sent = {}, ev = { timeouts: 0, unknown: [], pushes: [] };
  const link = new E.Link((d) => { sent[d[4]] = (sent[d[4]] || 0) + 1; out.send(d); }, {
    timeout: 300, onTimeout: () => ev.timeouts++, onUnknown: (f) => ev.unknown.push(f),
    onPush: (f) => ev.pushes.push({ ...f, pending: link.cur ? link.cur.cmd : 0 }), ...linkOpt });
  inp.onmidimessage = (e) => link.receive(e.data);
  const rq = (r, o) => link.request(r, o);
  return { m, link, rq, sent, ev, done: () => { link.close(); m.stop(); } };
}
const emptyStep = { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 };

async function editorLibrarian() {
  const { m, rq, done } = attachMock({});
  const C = E.CMD;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const pdesc = [];
  for (let i = 0; i < info.pcount; i++) pdesc.push(E.parse[C.DESC](await rq(E.req.desc(0, i))));
  const keys = E.paramKeys(pdesc, info.pe0, info.pcount);
  ok(keys[6] === "PIT" && keys[13] === "PIT#2" && keys[info.pe0] === "E0" && new Set(keys).size === keys.length, "librarian: parameter keys unique (label#n, E0..E7)");

  const b = await E.bank.list(rq);
  ok(b.total === 32 && b.slots.length === 32 && b.slots[1].used && b.slots[1].name === "GLASS BELL" && !b.slots[3].used
    && b.slots[31].slot === 31, "librarian: UP_LIST, 32 slots in 2 frames");

  const cap = (await E.capturePatch(rq, info, "acid test")).patch;
  ok(cap.engine === 0 && cap.p.length === info.pcount && cap.pattern && cap.pattern[0][0] === 45 && cap.pattern[0][1] === 1,
    "librarian: capture = DUMP + first 16 steps");
  let rc = await E.bank.put(rq, 10, cap);
  const g = await E.bank.get(rq, info, 10);
  ok(rc === 0 && g.used && g.name === "acid test" && g.engine === 0 && eq(g.p, cap.p) && js(g.pattern) === js(cap.pattern),
    "librarian: UP_PUT -> UP_GET round trip");
  const bad = E.parse[C.UP_PUT](await rq(E.req.upPut(60, cap), { timeout: 2500, retries: 0 }));
  ok(bad.rc === 1, "librarian: UP_PUT to a slot past the bank -> rc 1");
  ok(E.upName("") === "PATCH" && E.upName("abcdefghijklmnop") === "abcdefghijkl" && E.upName("Bäss") === "Bss", "librarian: device names (ASCII, 1..12)");

  await rq(E.req.set(0, 1, 33));
  rc = await E.bank.store(rq, 11, "STORED");
  const g2 = await E.bank.get(rq, info, 11);
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(rc === 0 && g2.name === "STORED" && g2.engine === d1.engine && eq(g2.p, d1.p) && g2.p[1] === 33, "librarian: UP_STORE keeps the current sound");

  /* another engine, empty sequencer, then UP_LOAD brings sound and pattern back */
  await rq(E.req.preset(2, 0));
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  rc = await E.bank.load(rq, 10);
  const d2 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  ok(rc === 0 && d2.engine === 0 && eq(d2.p, cap.p) && js(E.patternFromSteps(st)) === js(cap.pattern), "librarian: UP_LOAD applies sound + pattern (empty sequencer)");

  rc = await E.bank.erase(rq, 11);
  const b2 = await E.bank.list(rq);
  const rcEmpty = await E.bank.load(rq, 11);
  ok(rc === 0 && !b2.slots[11].used && b2.slots[10].used && rcEmpty === 1, "librarian: UP_ERASE, UP_LOAD of an empty slot -> rc 1");

  /* audition: a PHASE patch with a pattern; the sequencer is empty, so its pattern is written */
  const bass = await E.bank.get(rq, info, 2);   /* PHASE RESO, with the ACID pattern */
  for (let i = 0; i < info.nstep; i++) await rq(E.req.stepSet(i, emptyStep));
  const flashBefore = js(m.state.bank);
  let a = await E.auditionPatch(rq, info, bass, { gEng: 20, slen: 29 });
  const d3 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const st3 = [];
  for (let i = 0; i < 16; i++) st3.push(E.parse[C.STEP_GET](await rq(E.req.stepGet(i))));
  ok(a.wrote && d3.engine === bass.engine && eq(d3.p, bass.p) && js(E.patternFromSteps(st3)) === js(bass.pattern) && js(m.state.bank) === flashBefore,
    "librarian: audition = G_ENGSEL + SETs + pattern, no flash write");
  /* with notes in the sequencer the pattern is left alone */
  await rq(E.req.stepSet(20, { n: 1, notes: [50, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  const bell = await E.bank.get(rq, info, 1);
  a = await E.auditionPatch(rq, info, { ...bell, pattern: [[72, 0], [74, 0]] }, { gEng: 20, slen: 29 });
  const s0 = E.parse[C.STEP_GET](await rq(E.req.stepGet(0)));
  const d4 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(!a.wrote && s0.notes[0] === bass.pattern[0][0] && d4.engine === 1 && eq(d4.p, bell.p), "librarian: audition keeps a sequencer that has notes");

  /* ties as the firmware keeps them: no note; a rest has no flags */
  const tied = [[60, 1], [61, 4], [0, 4], [0, 3], [64, 2]];
  const norm = E.patternFromSteps(E.stepsFromPattern(tied));
  rc = await E.bank.put(rq, 12, { ...cap, name: "TIES", pattern: tied });
  const g3 = await E.bank.get(rq, info, 12);
  ok(rc === 0 && js(norm.slice(0, 5)) === js([[60, 1], [0, 4], [0, 4], [0, 0], [64, 2]]) && js(g3.pattern) === js(norm),
    "librarian: pattern ties / rests normalised like the firmware");
  rc = await E.bank.store(rq, 13, "");
  ok(rc === 0 && (await E.bank.get(rq, info, 13)).name === `${info.engines[d4.engine]} 14`, "librarian: UP_STORE with no name -> automatic name");

  /* library files */
  const ctx = { keys, engines: info.engines, firmware: info.version, pe0: info.pe0 };
  const pts = [cap, { ...bass, engineName: info.engines[bass.engine], tags: ["bass", "device"] }];
  const file = JSON.parse(JSON.stringify(E.libraryFile("library", pts, ctx)));
  ok(file.format === "felucca-library" && file.version === 1 && file.pCount === info.pcount && file.paramLabels.length === info.pcount && file.engines.length === 10,
    "library file: versioned, with P_COUNT, labels and engines");
  const back = E.readLibraryFile(file, ctx);
  ok(back.patches.length === 2 && !back.skipped && eq(back.patches[0].p, cap.p) && eq(back.patches[1].p, bass.p)
    && js(back.patches[1].pattern) === js(bass.pattern) && back.patches[1].tags.join() === "bass,device" && back.patches[1].engineName === "PHASE",
    "library file: write -> read round trip");
  /* a future firmware: one more parameter at id 5, engines in another order and one of them gone */
  const keys2 = [...keys.slice(0, 5), "NEW", ...keys.slice(5)];
  const eng2 = ["PHASE", "ANALOG", "SAMPLE"];
  const fut = E.readLibraryFile(file, { keys: keys2, engines: eng2 });
  const p0 = fut.patches[0].p;
  ok(fut.patches.length === 2 && p0.length === info.pcount + 1 && p0[5] === null && p0[6] === cap.p[5] && p0[info.pcount] === cap.p[info.pcount - 1]
    && fut.patches[0].engine === 1 && fut.patches[1].engine === 0, "library file: other ids / engine order mapped by label and name");
  const lost = E.readLibraryFile({ ...file, patches: [{ ...file.patches[0], engineName: "WAVETABLE" }] }, ctx);
  ok(lost.patches.length === 0 && lost.skipped === 1, "library file: a patch for an unknown engine is skipped");
  const bankFile = E.libraryFile("bank", [{ ...g, engineName: "ANALOG", slot: 10 }], ctx);
  ok(bankFile.kind === "bank" && bankFile.patches[0].slot === 10 && E.readLibraryFile(bankFile, ctx).patches[0].slot === 10, "library file: bank export keeps slot numbers");
  const old = E.readLibraryFile({ format: "felucca-patch", version: 1, engine: 0, preset: 4, engineName: "ANALOG", presetName: "ACID", p: d2.p, steps: st }, ctx);
  ok(old.patches.length === 1 && eq(old.patches[0].p, d2.p) && js(old.patches[0].pattern) === js(cap.pattern), "library file: reads the old \"Save to file\" format");
  let threw = false;
  try { E.readLibraryFile({ format: "something" }, ctx); } catch (e) { threw = true; }
  ok(threw, "library file: unknown format -> error");
  done();
}

async function editorLive() {
  const C = E.CMD;
  const { m, link, rq, sent, ev, done } = attachMock({ watchMs: 250 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(await E.startWatch(rq), "live: WATCH on");

  /* a push between a request and its reply */
  const pend = rq(E.req.dump());
  const kn = m.sim.knob(9, 4);
  const dump = E.parse[C.DUMP](await pend, info);
  const ch = ev.pushes.find((f) => f.cmd === C.CHANGED);
  const cv = ch && E.parse[C.CHANGED](ch.a);
  ok(dump.p.length === info.pcount && ch && ch.pending === C.DUMP && cv.scope === 0 && cv.id === 9 && cv.value === kn.value && !ev.unknown.length,
    "live: CHANGED while DUMP waits -> push handler, reply still matched");
  const rl = m.sim.reload();
  m.sim.step(3);
  const pend2 = rq(E.req.stepGet(7));
  const s7 = E.parse[C.STEP_GET](await pend2);
  await sleep(10);
  const r = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(s7.index === 7 && r && E.parse[C.RELOAD](r.a).preset === rl.preset && sc && E.parse[C.STEP_CHANGED](sc.a).index === 3,
    "live: RELOAD and STEP_CHANGED routed");
  const nr = ev.pushes.filter((f) => f.cmd === C.RELOAD).length;
  await rq(E.req.preset(1, 2));
  await rq(E.req.stepSet(9, { n: 1, notes: [62, 0, 0, 0], time: 0, flags: 0, vel: 90 }));
  await sleep(10);
  ok(ev.pushes.filter((f) => f.cmd === C.RELOAD).length === nr + 1 && !ev.pushes.some((f) => f.cmd === C.STEP_CHANGED && f.a[0] === 9),
    "live: RELOAD after an editor PRESET too, nothing after its STEP_SET");

  /* PING keeps the watch on; without requests it ends */
  for (let i = 0; i < 4; i++) { await sleep(120); await rq(E.req.ping()); }
  let n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0 + 1, "live: PING keeps WATCH on");
  await sleep(320);
  n0 = ev.pushes.length;
  m.sim.knob();
  await sleep(10);
  ok(ev.pushes.length === n0, "live: WATCH ends by itself without requests");

  /* a slider drag: 40 values at once -> one SET in flight + one coalesced, the last value wins */
  const before = sent[C.SET] || 0;
  const all = [];
  for (let v = 0; v < 40; v++) all.push(rq(E.req.set(0, 9, v * 3), { key: "0:9" }));
  await Promise.all(all);
  ok((sent[C.SET] || 0) - before === 2 && m.state.p[9] === 117 && link.idle, "live: drag SETs coalesce (2 frames for 40 values, latest kept)");
  ok(ev.timeouts === 0, "live: no timeouts");
  done();

  /* older firmware: no reply to WATCH -> false without a "no reply" message; no bank */
  const o = attachMock({ legacy: true });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const w = await o.rq(E.req.watch(1), { timeout: 60, retries: 0, quiet: true }).then(() => true, () => false);
  const sw = await E.startWatch(o.rq);
  const bl = await E.bank.list((rr, oo) => o.rq(rr, { ...oo, timeout: 60, quiet: true })).then(() => "listed", (e) => e.message);
  ok(!w && !sw && /^timeout/.test(bl) && o.ev.timeouts === 0, "live: older firmware -> WATCH unanswered (fall back to polling), no bank");
  o.done();
}

/* ------------------------------------------------------ editor protocol v3: tracks --- */
async function editorTracks() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const tr = E.parse[C.TRACK](await rq(E.req.track()));
  ok(info.ntrk === 4 && tr.sel === 0 && tr.ntrk === 4 && tr.tracks[0].engine === 0 && tr.tracks[1].engine === 1
    && tr.tracks[3].engine === info.nengines, "tracks: INFO NTRK, TRACK lists 4 (track 4 = drums, engine NENGINES)");
  /* the v1 commands follow the selected track */
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const t1 = E.parse[C.TRACK](await rq(E.req.track(1)));
  const d1 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  await rq(E.req.set(0, 1, 77));
  const td0 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(0)), info);
  const td1 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(1)), info);
  ok(t1.sel === 1 && d1.engine === 1 && d0.engine === 0 && td1.p[1] === 77 && td0.p[1] === d0.p[1] && td0.p[1] !== 77,
    "tracks: TRACK selects; DUMP / SET act on it, TRACK_DUMP reads any track");
  /* steps of a track that is not selected */
  const w = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5, { n: 2, notes: [60, 67, 0, 0], time: 0, flags: 1, vel: 99 })));
  const g2 = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const s1 = E.parse[C.STEP_GET](await rq(E.req.stepGet(5)));
  ok(w.track === 2 && g2.n === 2 && g2.notes[1] === 67 && g2.vel === 99 && s1.n === 0, "tracks: TRACK_STEP set / get on another track");
  /* level / mute; the drum level is GLO > DRUMS LEVEL */
  const mx = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0, 90, 1)));
  const mxd = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(3, 64, 0)));
  const mx2 = E.parse[C.TRACK_MIX](await rq(E.req.trackMix(0)));
  const tr2 = E.parse[C.TRACK](await rq(E.req.track()));
  ok(mx.level === 90 && mx.mute === 1 && mx2.level === 90 && mxd.level === 64 && m.state.g[25] === 64
    && tr2.tracks[0].level === 90 && tr2.tracks[0].mute === 1, "tracks: TRACK_MIX level / mute (drums: G_DRLVL)");
  /* the drum track: no sound to store or load */
  await rq(E.req.track(3));
  const dd = E.parse[C.DUMP](await rq(E.req.dump()), info);
  const us = E.parse[C.UP_STORE](await rq(E.req.upStore(20, "X"), { timeout: 2500, retries: 0 }));
  const ul = E.parse[C.UP_LOAD](await rq(E.req.upLoad(1), { timeout: 2500, retries: 0 }));
  ok(dd.engine === info.nengines && us.rc === 1 && ul.rc === 1, "tracks: drum track selected -> DUMP engine NENGINES, UP_STORE / UP_LOAD rc 1");
  /* pushes carry the selected track */
  await rq(E.req.track(0));
  ok(await E.startWatch(rq), "tracks: WATCH on");
  m.sim.track(2);
  m.sim.step(4);
  await sleep(10);
  const rl = ev.pushes.find((f) => f.cmd === C.RELOAD), sc = ev.pushes.find((f) => f.cmd === C.STEP_CHANGED);
  ok(rl && E.parse[C.RELOAD](rl.a).track === 2 && sc && E.parse[C.STEP_CHANGED](sc.a).track === 2 && E.parse[C.STEP_CHANGED](sc.a).index === 4,
    "tracks: RELOAD / STEP_CHANGED carry the selected track");
  /* projects keep all four tracks */
  await rq(E.req.project(1, 3), { timeout: 4000, retries: 0 });
  await rq(E.req.trackStep(2, 5, { n: 0, notes: [0, 0, 0, 0], time: 2, flags: 0, vel: 0 }));
  await rq(E.req.track(0));
  await rq(E.req.project(0, 3), { timeout: 4000, retries: 0 });
  const back = E.parse[C.TRACK_STEP](await rq(E.req.trackStep(2, 5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track())).sel;
  ok(back.n === 2 && back.notes[0] === 60 && sel === 2, "tracks: PROJECT save / load keeps every track and the selection");
  /* older firmware: no NTRK in INFO, no RELOAD track byte */
  const o = attachMock({ legacy: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  ok(oi.ntrk === 0 && E.parse[C.RELOAD]([0, 4]).track === 0 && E.parse[C.STEP_CHANGED]([7]).track === 0, "tracks: older firmware parses (no tracks)");
  o.done();
  done();
}

/* ------------------------------------------------ editor v3: the mixer (Tracks tab) --- */
async function editorMixer() {
  const C = E.CMD;
  const { m, rq, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const PAN = 39, MUTE = 40;
  const m0 = await E.mixer.read(rq, info, { pan: PAN });
  ok(m0.ntrk === 4 && m0.tracks.length === 4 && m0.tracks[1].pan === -24 && m0.tracks[2].pan === 20 && m0.tracks[3].engine === info.nengines
    && m0.tracks.every((x) => Number.isInteger(x.level) && (x.mute === 0 || x.mute === 1)), "mixer: read = TRACK + pan of every track (TRACK_DUMP)");
  /* level / mute of a track that is not selected, and of the drum track (G_DRLVL) */
  const a = await E.mixer.setMix(rq, 2, 70, 1);
  const b = await E.mixer.setMix(rq, 3, 200, 0);
  const m1 = await E.mixer.read(rq, info, { pan: PAN });
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  ok(a.level === 70 && a.mute === 1 && b.level === 127 && m.state.g[25] === 127 && m1.tracks[2].level === 70 && m1.tracks[2].mute === 1
    && td2.p[0] === 70 && td2.p[MUTE] === 1 && m1.sel === 0, "mixer: TRACK_MIX level / mute round trip (drums: G_DRLVL, clamped)");
  /* pan of another track: selected for the SET, the selection put back, no RELOAD pushed */
  ok(await E.startWatch(rq), "mixer: WATCH on");
  const pushes = ev.pushes.length;
  /* (the v3 path: firmware 0.8 has no TRACK_PARAM) */
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40);
  const p0 = await E.mixer.setPan(rq, 0, 0, PAN, 99);
  await sleep(10);
  const m2 = await E.mixer.read(rq, info, { pan: PAN });
  const d0 = E.parse[C.DUMP](await rq(E.req.dump()), info);
  ok(p2 === -40 && p0 === 63 && m2.sel === 0 && m2.tracks[2].pan === -40 && m2.tracks[0].pan === 63 && d0.p[PAN] === 63 && m2.tracks[1].pan === -24
    && ev.pushes.length === pushes, "mixer: pan of any track via SET (other track selected for a moment, then back; no push)");
  /* the device's TRACKS page: level of the selected track pushes CHANGED; REC arm shows in TRACK */
  m.sim.level(33);
  m.sim.arm(1);
  await sleep(10);
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  const m3 = await E.mixer.read(rq, info, { pan: PAN });
  ok(ch && ch.scope === 0 && ch.id === 0 && ch.value === 33 && m3.tracks[0].level === 33 && m3.tracks[1].armed === 1 && m3.tracks[0].armed === 0,
    "mixer: device-side level (CHANGED push) and REC arm read back");
  await rq(E.req.track(3));
  m.sim.level(90);
  await sleep(10);
  const chd = E.parse[C.CHANGED](ev.pushes.filter((f) => f.cmd === C.CHANGED).pop().a);
  ok(chd.scope === 1 && chd.id === 25 && chd.value === 90, "mixer: drum level on the device pushes G_DRLVL");
  /* the drum track's steps: GM notes, shown and typed by name */
  const st = [];
  for (let i = 0; i < 16; i++) st.push(E.parse[C.TRACK_STEP](await rq(E.req.trackStep(3, i))));
  const names = st[0].notes.slice(0, st[0].n).map(E.drumName).join(" ");
  ok(names === "KICK CHH" && st[4].notes.slice(0, 2).map(E.drumName).join(" ") === "SNARE CHH" && E.drumName(20) === "20",
    "mixer: the mock drum track holds a GM pattern (KICK CHH ...)");
  ok(JSON.stringify(E.parseNotes("kick CHH 49")) === "[36,42,49]" && JSON.stringify(E.parseNotes("C4 SNARE")) === "[60,38]" && E.parseNotes("KICKS") === null
    && Object.keys(E.GM_DRUM).length === 47 && new Set(Object.values(E.GM_DRUM)).size === 47, "mixer: GM drum names parse (unique, 35..81)");
  done();
}

/* ------------------------------------- editor v4: TRACK_PARAM and TRACK_CHANGED --- */
async function editorTrackParam() {
  const C = E.CMD, PAN = 39, MUTE = 40;
  const { m, rq, sent, ev, done } = attachMock({ watchMs: 1000 });
  const info = E.parse[C.INFO](await rq(E.req.info()));
  const w1 = E.parse[C.WATCH](await rq(E.req.watch(1)));
  m.sim.param(2, PAN, 11);
  await sleep(10);
  ok(w1.on === 1 && !ev.pushes.some((f) => f.cmd === C.TRACK_CHANGED), "v4: WATCH 1 answers 1 as before (no TRACK_CHANGED pushes)");
  ok(await E.startWatch(rq) === 3, "v4: WATCH 3 -> 3 (TRACK_PARAM / TRACK_CHANGED known)");
  const tracks0 = sent[C.TRACK] || 0, pushes = ev.pushes.length;
  const g = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, PAN)));
  const p2 = await E.mixer.setPan(rq, 2, 0, PAN, -40, true);
  const p1 = await E.mixer.setPan(rq, 1, 0, PAN, 99, true);
  const alg = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(1, info.pe0, 50)));   /* track 2 is DIGITAL: ALG 0..7 */
  const lv = E.parse[C.TRACK_PARAM](await rq(E.req.trackParam(3, 0, -5)));
  const sel = E.parse[C.TRACK](await rq(E.req.track()));
  const td2 = E.parse[C.TRACK_DUMP](await rq(E.req.trackDump(2)), info);
  await sleep(10);
  ok(g.track === 1 && g.id === PAN && g.value === -24 && p2 === -40 && p1 === 63 && alg.value === 7 && lv.value === 0 && td2.p[PAN] === -40
    && sel.sel === 0 && (sent[C.TRACK] || 0) === tracks0 + 1 && ev.pushes.length === pushes,
    "v4: TRACK_PARAM get / set on other tracks (clamped as SET, selection kept, no push)");
  const bad = await rq(E.req.trackParam(4, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(bad === "none", "v4: TRACK_PARAM of track 5: no reply");
  /* device-side changes: CHANGED for the selected track, TRACK_CHANGED for the others */
  m.sim.param(2, PAN, 30);
  m.sim.param(3, MUTE, 1);
  m.sim.param(0, PAN, -7);
  await sleep(10);
  const tc = ev.pushes.filter((f) => f.cmd === C.TRACK_CHANGED).map((f) => E.parse[C.TRACK_CHANGED](f.a));
  const ch = ev.pushes.filter((f) => f.cmd === C.CHANGED).map((f) => E.parse[C.CHANGED](f.a)).pop();
  ok(tc.length === 2 && tc[0].track === 2 && tc[0].id === PAN && tc[0].value === 30 && tc[1].track === 3 && tc[1].id === MUTE && tc[1].value === 1
    && ch && ch.scope === 0 && ch.id === PAN && ch.value === -7 && !ev.unknown.length, "v4: TRACK_CHANGED pushes for the other tracks, CHANGED for the selected one");
  done();
  /* firmware 0.8 (v3): WATCH 3 answers 1, TRACK_PARAM unanswered: the editor keeps the select / restore path */
  const o = attachMock({ v3: true, watchMs: 1000 });
  E.parse[C.INFO](await o.rq(E.req.info()));
  const on = await E.startWatch(o.rq);
  const tp = await o.rq(E.req.trackParam(1, PAN), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  const pv = await E.mixer.setPan(o.rq, 2, 0, PAN, 5, false);
  ok(on === 1 && tp === "none" && pv === 5 && o.ev.timeouts === 0 && !o.ev.unknown.length, "v4: v3 firmware -> WATCH 1, no TRACK_PARAM (pan by select / restore)");
  o.done();
}

/* ------------------------------------- v5 (Jangada): backup -> restore --- */
async function editorBackup() {
  const A = attachMock({});
  const info = E.parse[E.CMD.INFO](await A.rq(E.req.info()));
  ok(info.proto === BK.BACKUP_PROTO_FM6, "backup: INFO ends with the protocol version (6: the FM6 bank too)");
  /* something in every kind of object: projects 2 and 4, a user preset in bank 2, a sample in USR3 */
  await A.rq(E.req.set(1, 0, 133));
  await A.rq(E.req.project(1, 1), { timeout: 4000, retries: 0 });
  await A.rq(E.req.set(1, 0, 97));
  await A.rq(E.req.project(1, 3), { timeout: 4000, retries: 0 });
  await A.rq(E.req.set(1, 0, 121));
  await A.rq(E.req.upStore(20, "BACKUP ME"));
  const s = Int16Array.from({ length: 4000 }, (_, i) => Math.round(9000 * Math.sin(i / 5)));
  const { hdr, data } = E.buildSlot("keep", [{ s, root: 60 }]);
  await A.rq(E.req.smpBegin(2), { timeout: 1000, retries: 0 });
  for (let off = 0; off < data.length; off += 256) await A.rq(E.req.smpWrite(2, E.SMP.DATA_OFF + off, data.subarray(off, off + 256)), { timeout: 1000 });
  await A.rq(E.req.smpEnd(2, hdr), { timeout: 2000, retries: 0 });
  let prog = 0;
  const file = await BK.captureBackup(A.rq, info.version, (d, n) => { prog = d / n; });
  const text = JSON.stringify(file, null, 1);
  ok(file.format === "jangada-backup" && file.objects.length === 13 && prog === 1 && file.objects[3].size > 0 && file.objects[4].size === 0 &&
     file.objects[7].size > 0 && file.objects[12].size === E.SMP.DATA_OFF + data.length && file.objects[10].size === 0 &&
     file.objects[8].id === 8 && file.objects[9].id === 9,
     "backup: the mock FM-1 into one file (13 objects with the FM6 bank, empty ones as 0)");
  ok(/^jangada-backup-\d{4}-\d\d-\d\d\.json$/.test(BK.backupName()), "backup: the file is jangada-backup-DATE.json");
  /* into an empty FM-1: what comes back is what went in */
  const B = attachMock({});
  const sent = () => (B.sent[E.CMD.BK_PUT] || 0) + (B.sent[E.CMD.SMP_BEGIN] || 0) + (B.sent[E.CMD.SMP_ERASE] || 0);
  const damaged = JSON.parse(text); damaged.objects[3].data = damaged.objects[3].data.replace(/^./, (c) => (c === "A" ? "B" : "A"));
  const refused = await BK.restoreBackup(B.rq, damaged).then(() => null, (e) => e);
  ok(refused && refused.code === "bkBad" && sent() === 0, "backup: a damaged file is refused before anything is written");
  const sloop = JSON.parse(text); sloop.format = "sloop-backup";
  ok(await BK.restoreBackup(B.rq, sloop).then(() => false, (e) => e.code === "bkBad") && sent() === 0, "backup: another firmware's backup is refused");
  await BK.restoreBackup(B.rq, text);
  const back = await BK.captureBackup(B.rq, "B");
  ok(back.objects.every((o, i) => o.crc === file.objects[i].crc && o.data === file.objects[i].data), "backup: restore into an empty FM-1, then a backup of it: the same file");
  const bpm = E.parse[E.CMD.GET](await B.rq(E.req.get(1, 0))).value;
  const u = E.parse[E.CMD.UP_GET](await B.rq(E.req.upGet(20)), info);
  const smp = E.parse[E.CMD.SMP_INFO](await B.rq(E.req.smpInfo()));
  const pj = E.parse[E.CMD.PROJECT](await B.rq(E.req.project(2, 3)));
  ok(bpm === 121 && u.used && u.name === "BACKUP ME" && smp.slots[2].zones === 1 && smp.slots[2].name === "KEEP" && pj.used === 1,
     "backup: the working project, a user preset, a sample and a project are back");
  /* a song playing on the device (the mock's commit answers 3) / a firmware without the backup */
  const C = attachMock({ noBackup: true });
  const ci = E.parse[E.CMD.INFO](await C.rq(E.req.info()));
  const none = await BK.captureBackup((r, o) => C.rq(r, { ...o, timeout: 60, retries: 0 }), "x").then(() => "ok", (e) => e.message);
  ok(ci.proto === 0 && /^timeout/.test(none), "backup: firmware without it: INFO has no version, LIST no reply");
  /* a v5 device (Jangada 0.4: no FM6 bank in the backup): its 11-object file, and ours into it */
  const D = attachMock({ v5: true });
  const di = E.parse[E.CMD.INFO](await D.rq(E.req.info()));
  const f5 = await BK.captureBackup(D.rq, di.version);
  await BK.restoreBackup(D.rq, text, () => {}, { ids: BK.backupIds(di.proto) });
  await BK.restoreBackup(B.rq, f5, () => {}, { ids: BK.backupIds(6) });
  ok(di.proto === 5 && f5.objects.length === 11 && !(D.sent[E.CMD.BK_PUT] === undefined),
     "backup: v5 firmware: 11 objects; a v6 file restores without the FM6 bank, a v5 file into v6");
  A.done(); B.done(); C.done(); D.done();
}

/* ------------------------------------------- FM6 patches (Jangada 0.5) --- */
async function editorFm6() {
  const F6 = E.FM6;
  /* the module's tables == the firmware's (tools/gen_fm6_patches.py -> build/gen/felucca_fm6.h) */
  const hp = join(HERE, "..", "build", "gen", "felucca_fm6.h");
  if (existsSync(hp)) {
    const h = readFileSync(hp, "utf8");
    const nums = (t) => [...t.matchAll(/\d+/g)].map((m) => +m[0]);
    const init = nums(/FM6_INIT\[128\] = \{([^}]*)\}/.exec(h)[1]);
    const fac = [...h.matchAll(/\{\/\* [^*]* \*\/([^}]*)\}/g)].map((m) => nums(m[1]));
    ok(eq(init, F6.INIT_PK) && fac.length === F6.FACTORY_PK.length && fac.every((r, k) => eq(r, F6.FACTORY_PK[k])),
       "FM6: init voice and F1..F8 == the firmware's (felucca_fm6.h)");
  } else console.log("FM6: no build/gen/felucca_fm6.h: table check skipped");
  ok(F6.FACTORY_PK.every((r) => eq(F6.pack(F6.unpack(r)), r)) && F6.name(F6.init()) === "INIT VOICE",
     "FM6: pack(unpack(x)) == x for every factory patch, the init voice's name");
  /* SysEx: one voice (163 bytes) and a bank of 32 (4104 bytes), checksums, raw files */
  const v = F6.factory(3), one = F6.singleSysex(v, 5);
  ok(one.length === 163 && one[0] === 0xF0 && one[1] === 0x43 && one[2] === 5 && one[5] === 0x1B && one[162] === 0xF7 &&
     (one.slice(6, 162).reduce((a, x) => a + x, 0) & 127) === 0, "FM6: a voice as SysEx (VCED, 163 bytes, the checksum zeroes the sum)");
  const r1 = F6.parseSysex(one);
  ok(r1.voices.length === 1 && eq(r1.voices[0].v, v) && !r1.badSum && !r1.bank && r1.voices[0].name === "BRASS SECT", "FM6: ... read back");
  const voices = Array.from({ length: 32 }, (_, k) => (k < 8 ? F6.factory(k) : k === 9 ? null : F6.setName(F6.init(), "V" + k)));
  const bnk = F6.bankSysex(voices);
  const r2 = F6.parseSysex(bnk);
  ok(bnk.length === 4104 && bnk[3] === 9 && bnk[4] === 0x20 && r2.bank && r2.voices.length === 32 && !r2.badSum &&
     eq(r2.voices[2].v, F6.factory(2)) && r2.voices[9].name === "INIT VOICE" && r2.voices[20].name === "V20",
     "FM6: a bank of 32 as SysEx (VMEM, 4104 bytes), empty slots as the init voice, read back");
  const bad = bnk.slice(); bad[100] ^= 1;
  const raw = F6.parseSysex(bnk.slice(6, 6 + 4096)), raw1 = F6.parseSysex(Array.from(v));
  ok(F6.parseSysex(bad).badSum === 1 && raw.bank && raw.voices.length === 32 && raw1.voices.length === 1 &&
     F6.parseSysex(Uint8Array.from([...one, ...bnk])).bank === false && F6.parseSysex([1, 2, 3]).voices.length === 0,
     "FM6: a wrong checksum counted, raw 4096 / 155-byte files, a mixed file is no bank, junk reads nothing");
  /* the mock: FM6 patches through cmds 68..71 */
  const { m, rq, done } = attachMock({});
  const C = E.CMD, T = F6.TARGET;
  const info = E.parse[C.INFO](await rq(E.req.info()));
  ok(info.proto === 6 && info.fm6 && info.fm6.factory === 8 && info.fm6.bank === 32, "FM6: INFO advertises 46 01 8 32 after the version");
  let l = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  ok(l.factory === 8 && l.bank === 32 && l.slots.length === 40 && l.slots[0].used && l.slots[0].name === "TINE EP" && !l.slots[8].used,
     "FM6: LIST: F1..F8 by name, B1..B32 empty");
  ok(E.parse[C.FM6_PUT](await rq(E.req.fm6Put(T.BANK, 4, F6.FACTORY_PK[6]))).rc === 0, "FM6: PUT into B5");
  l = E.parse[C.FM6_LIST](await rq(E.req.fm6List()));
  const g = E.parse[C.FM6_GET](await rq(E.req.fm6Get(T.BANK, 4)));
  ok(l.slots[12].used && l.slots[12].name === F6.name(F6.factory(6)) && g.rc === 0 && eq(g.packed, F6.FACTORY_PK[6]) &&
     E.parse[C.FM6_GET](await rq(E.req.fm6Get(T.BANK, 5))).rc === 2, "FM6: B5 listed and read back, B6 empty (rc 2)");
  /* a track: PTCH B5 plays that patch; a PUT to the track is its own, PTCH as it is */
  const FM6_E = info.engines.indexOf("FM6"), PTCH = info.pe0 + 7;
  await rq(E.req.track(1));
  await rq(E.req.set(1, 20, FM6_E));
  await rq(E.req.set(0, PTCH, 8 + 4));
  const tg = E.parse[C.FM6_GET](await rq(E.req.fm6Get(T.TRACK, 1)));
  ok(tg.rc === 0 && eq(tg.packed, F6.FACTORY_PK[6]), "FM6: PTCH B5 on track 2 -> its patch");
  const mine = F6.pack(F6.setName(F6.factory(1), "MINE"));
  await rq(E.req.fm6Put(T.TRACK, 1, mine));
  const tg2 = E.parse[C.FM6_GET](await rq(E.req.fm6Get(T.TRACK, 1)));
  const pt = E.parse[C.GET](await rq(E.req.get(0, PTCH)));
  ok(eq(tg2.packed, mine) && pt.value === 12, "FM6: a patch sent to the track stays its own (PTCH still B5)");
  m.sim.play(true);
  const busy = E.parse[C.FM6_PUT](await rq(E.req.fm6Put(T.BANK, 6, mine))).rc, busyE = E.parse[C.FM6_ERASE](await rq(E.req.fm6Erase(4))).rc;
  m.sim.play(false);
  ok(busy === 3 && busyE === 3, "FM6: a bank write while the song plays: rc 3");
  ok(E.parse[C.FM6_PUT](await rq(E.req.fm6Put(T.FACTORY, 0, mine))).rc === 1 && E.parse[C.FM6_PUT](await rq(E.req.fm6Put(T.BANK, 32, mine))).rc === 1 &&
     E.parse[C.FM6_GET](await rq(E.req.fm6Get(T.TRACK, 9))).rc === 1, "FM6: a factory PUT, B33, track 10: rc 1");
  /* the bank and the track's own patch go through a backup into an empty FM-1 */
  const file = await BK.captureBackup(rq, info.version);
  const B = attachMock({});
  await BK.restoreBackup(B.rq, file, () => {}, { ids: BK.backupIds(6) });
  const gb = E.parse[C.FM6_GET](await B.rq(E.req.fm6Get(T.BANK, 4))), gt = E.parse[C.FM6_GET](await B.rq(E.req.fm6Get(T.TRACK, 1)));
  ok(file.objects[8].size > 0 && gb.rc === 0 && eq(gb.packed, F6.FACTORY_PK[6]) && gt.rc === 0 && eq(gt.packed, mine),
     "FM6: backup -> restore: the bank (B5) and the track's own patch");
  ok(E.parse[C.FM6_ERASE](await rq(E.req.fm6Erase(4))).rc === 0 && E.parse[C.FM6_GET](await rq(E.req.fm6Get(T.BANK, 4))).rc === 2,
     "FM6: ERASE B5");
  /* firmware without FM6 patches (Jangada 0.4): no tag, no reply */
  const o = attachMock({ v5: true });
  const oi = E.parse[C.INFO](await o.rq(E.req.info()));
  const none = await o.rq(E.req.fm6List(), { timeout: 60, retries: 0, quiet: true }).then(() => "reply", () => "none");
  ok(oi.fm6 === null && none === "none", "FM6: older firmware: no INFO tag, LIST unanswered (the tab stays hidden)");
  done(); B.done(); o.done();
}

/* ------------------------------------------------- editor tabs and strings --- */
function editorTabs() {
  const tabs = [...html.matchAll(/<button role="tab" data-tab="(\w+)"/g)].map((x) => x[1]);
  const panels = [...html.matchAll(/<section class="panel" id="p-(\w+)" data-tab="(\w+)"/g)].map((x) => [x[1], x[2]]);
  const TABS = JSON.parse((/const TABS = (\[[^\]]*\]);/.exec(html) || [])[1] || "[]");
  ok(tabs.length === 8 && js(tabs) === js(TABS) && js(panels.map((x) => x[1])) === js(TABS) && panels.every(([a, b]) => a === b),
    `editor: ${tabs.length} tabs, one panel each (${tabs.join(" ")})`);
  ok(/localStorage\.setItem\(TAB_KEY/.test(html) && /try \{ localStorage/.test(html) && /history\.replaceState\([^)]*"#" \+ name\)/.test(html)
    && /addEventListener\("hashchange"/.test(html), "editor: last tab in localStorage (try/catch) and in the URL hash");
  /* every string key in both languages */
  const tb = html.slice(html.indexOf("const TEXT = {"), html.indexOf("\n};", html.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const ja = new Set(Object.keys(TEXT.ja)), en = new Set(Object.keys(TEXT.en));
  const used = new Set([...html.matchAll(/data-t="(\w+)"|\bt\("(\w+)"\)|sayK\("(\w+)"|hint = "(\w+)"/g)].map((x) => x[1] || x[2] || x[3] || x[4]));
  for (const k of ["needDevice", "smpNone", "bankConnect", "bankNone", "selectedTrack", "selectTrack", "drumHelp", "notesHelp", "live", "polling"]) used.add(k);
  const miss = [...used].filter((k) => !ja.has(k) || !en.has(k));
  const odd = [...ja].filter((k) => !en.has(k)).concat([...en].filter((k) => !ja.has(k)));
  ok(!miss.length && !odd.length, `editor: every string in ja and en (${used.size} used${miss.length ? ", missing " + miss : ""}${odd.length ? ", one language only " + odd : ""})`);
  /* the page script parses (the browser's view of it) */
  const script = html.slice(html.indexOf("<script>") + 8, html.lastIndexOf("</script>"));
  let err = null;
  try { new vm.Script(script); } catch (e) { err = e.message; }
  ok(!err, "editor: page script compiles" + (err ? ` (${err})` : ""));
  ok(!/#[0-9a-f]{3,6}\b/i.test(html.slice(html.indexOf("[hidden]") - 6000, html.indexOf("[hidden]")).replace(/:root[^}]*\}/g, "")),
    "editor: no colours beyond the black / white tokens in the new styles");
}

/* ------------------------------------------------- editor icons (Fukiai) --- */
function editorIcons() {
  const blk = html.slice(html.indexOf("const GLYPH = {"), html.indexOf("};", html.indexOf("const GLYPH = {")));
  const names = new Set([...blk.matchAll(/(\w+): 0x[0-9A-F]{4}/g)].map((m) => m[1]));
  const used = new Set([...html.matchAll(/data-ic="(\w+)"|ic: "(\w+)"|: "((?:waveform|function|symbol|control|port|ui|note)_\w+)"/g)].map((m) => m[1] || m[2] || m[3]));
  const missing = [...used].filter((n) => !names.has(n));
  ok(names.size > 0 && !missing.length, `editor: every icon name is in GLYPH (${used.size} used${missing.length ? ", missing " + missing : ""})`);
  const ttf = existsSync(join(HERE, "fukiai.ttf")) && readFileSync(join(HERE, "fukiai.ttf"));
  ok(ttf && ttf.readUInt32BE(0) === 0x00010000 && existsSync(join(HERE, "FUKIAI-LICENSE.txt")) && html.includes('href="FUKIAI-LICENSE.txt"'),
    "editor: fukiai.ttf and FUKIAI-LICENSE.txt next to editor.html");
  ok(/html:not\(\.fk\) \.ic \{ display: none; \}/.test(html) && html.includes('classList.add("fk")'), "editor: icons hidden until the font has loaded");
}

/* ------------------------------------------- user samples: JS == sampleio.py --- */
function wav(sr, ch, bits, float, frames, f) {
  const bps = bits / 8, data = Buffer.alloc(frames * ch * bps);
  for (let i = 0; i < frames; i++) for (let c = 0; c < ch; c++) {
    const v = f(i, c), o = (i * ch + c) * bps;
    if (float) data.writeFloatLE(v, o);
    else if (bits === 8) data[o] = Math.max(0, Math.min(255, Math.round(v * 127 + 128)));
    else if (bits === 16) data.writeInt16LE(Math.round(v * 32000), o);
    else if (bits === 24) data.writeIntLE(Math.round(v * 8000000), o, 3);
  }
  const fmt = Buffer.alloc(16);
  fmt.writeUInt16LE(float ? 3 : 1, 0); fmt.writeUInt16LE(ch, 2); fmt.writeUInt32LE(sr, 4);
  fmt.writeUInt32LE(sr * ch * bps, 8); fmt.writeUInt16LE(ch * bps, 12); fmt.writeUInt16LE(bits, 14);
  const chunk = (id, b) => Buffer.concat([Buffer.from(id), Buffer.from(Uint32Array.of(b.length).buffer), b]);
  const body = Buffer.concat([Buffer.from("WAVE"), chunk("fmt ", fmt), chunk("data", data)]);
  return Buffer.concat([Buffer.from("RIFF"), Buffer.from(Uint32Array.of(body.length).buffer), body]);
}

function samplesMatch() {
  const dir = mkdtempSync(join(tmpdir(), "felucca-web-"));
  const files = [
    ["tone_A4.wav", wav(44100, 1, 16, false, 9000, (i) => Math.sin(i * 0.0627) * Math.exp(-i / 4000))],
    ["pad C3.wav", wav(48000, 2, 24, false, 7000, (i, c) => Math.sin(i * (c ? 0.031 : 0.0313)) * 0.7)],
    ["Bb2 float.wav", wav(22050, 1, 32, true, 5000, (i) => ((i % 97) / 48 - 1) * 0.5)],
    ["BD1 lofi.wav", wav(96000, 1, 8, false, 12000, (i) => Math.sin(i * 0.01) * Math.exp(-i / 3000))],
  ].map(([n, b]) => { const p = join(dir, n); writeFileSync(p, b); return p; });
  const zones = files.map((p) => {
    const w = E.parseWav(readFileSync(p));
    const s = E.normalize(E.resample(w.x, w.sr, E.SMP.RATE));
    return { s, root: E.rootFromName(p.split("/").pop().replace(/\.[^.]*$/, "")) };
  });
  const js = E.buildSlot("Mix ä 12345", zones);
  execFileSync("python3", [join(HERE, "../tools/fm1_sample_upload.py"), "build", "Mix ä 12345", join(dir, "slot"), ...files]);
  const pyHdr = readFileSync(join(dir, "slot.hdr")), pyData = readFileSync(join(dir, "slot.bin"));
  ok(eq(js.hdr, pyHdr) && eq(js.data, pyData), `samples: editor == sampleio.py (${files.length} WAV formats, ${js.data.length} B)`);
}

/* ------------------------------------------- CHOP (pure helpers; Jangada, after SLOOP 2.3) --- */
function chopTests() {
  const R = E.SMP.RATE, N = R * 4;
  /* a break: 8 hits (kick-like and snare-like, loud and ghost) on a quiet noise floor */
  let seed = 7;
  const rnd = () => ((seed = (seed * 1103515245 + 12345) >>> 0) / 2 ** 32) * 2 - 1;
  const x = new Float64Array(N);
  for (let i = 0; i < N; i++) x[i] = rnd() * 0.002;
  const HITS = [0.10, 0.52, 0.93, 1.31, 1.80, 2.26, 2.70, 3.33].map((t) => Math.round(t * R));
  const AMP = [1, 0.8, 0.25, 0.9, 1, 0.3, 0.85, 0.7];
  HITS.forEach((h, k) => {
    for (let i = 0; i < R * 0.3 && h + i < N; i++) {
      const env = Math.exp(-i / (k % 2 ? 1500 : 3000)) * AMP[k];
      x[h + i] += env * (k % 2 ? rnd() * 0.8 : Math.sin(2 * Math.PI * 60 * i / R) * 0.9 + rnd() * 0.1);
    }
  });
  const nov = E.chopNovelty(x), hits = E.chopHits(x, nov, 5);
  const near = (a, b, ms) => Math.abs(a - b) <= ms * R / 1000;
  ok(hits.length === HITS.length && hits.every((h, i) => near(h, HITS[i] - E.CHOP.PRE, 3)),
    `chop: hits found (${hits.length} of ${HITS.length}, each within 3 ms of its attack)`);
  ok(E.chopHits(x, nov, 1).length < HITS.length && E.chopHits(x, nov, 1).length >= 4, "chop: low sensitivity keeps the hard hits only");
  const late = E.chopSnap(x, nov, HITS[3] + 0.035 * R), early = E.chopSnap(x, nov, HITS[4] - 0.03 * R), none = E.chopSnap(x, nov, 1.1 * R);
  ok(near(late, HITS[3] - E.CHOP.PRE, 3) && near(early, HITS[4] - E.CHOP.PRE, 3) && none === Math.round(1.1 * R),
    "chop: a TAP 35 ms late / 30 ms early lands on its hit; away from hits it stays");
  const grid = E.chopGrid(0, Math.round(R * 60 / 90 * 16), 90, 1);
  ok(grid.length === 16 && grid[1] === Math.round(R * 60 / 90) && E.chopEqual(100, 900, 4).join() === "100,300,500,700",
    "chop: grid (16 beats at 90 BPM) and equal parts");
  const list = E.chopList([10, 50, 400], 1000, 100);
  ok(list.map((c) => `${c.start}-${c.end}`).join() === "10-50,50-150,400-500", "chop: chops end at the next marker or the max length");
  const chops = E.chopList(hits, N), zones = E.chopZones(x, chops, 60, 0);
  const peak = (s) => s.reduce((a, v) => Math.max(a, Math.abs(v)), 0);
  ok(zones.length === 8 && zones.every((z, i) => z.root === 60 + i && z.lo === z.root && z.hi === z.root && z.s[0] === 0)
    && Math.abs(peak(zones[2].s) / peak(zones[0].s) - 0.25) < 0.05 && zones[0].fname === "CHOP01_C4.wav" && zones[1].fname === "CHOP02_C#4.wav",
    "chop: one key each from C4, levels kept (a ghost stays quiet), faded in");
  const one = E.chopZones(x, chops, 60, 1, 3);
  ok(one.length === 1 && one[0].root === 60 && one[0].lo === 0 && one[0].hi === 127 && one[0].fname === "CHOP04_C4.wav",
    "chop: one chop over the whole keyboard");
  const slot = E.buildSlot("BREAK", zones), v = new DataView(slot.hdr.buffer);
  ok(slot.hdr[6] === 8 && slot.hdr[32 + 25] === 60 && slot.hdr[32 + 26] === 60 && slot.hdr[32 + 7 * 28 + 25] === 67 && v.getInt16(32 + 20, true) === 60 * 16
    && slot.data.length <= E.SMP.MAX_DATA, "chop: slot header (8 zones, one key each)");
  const w = E.parseWav(E.wavFile(zones[1].s));
  ok(w.sr === R && w.x.length === zones[1].s.length && Math.abs(w.x[100] * 32768 - zones[1].s[100]) < 2, "chop: WAV writer round trip");
  const dir = mkdtempSync(join(tmpdir(), "jangada-chop-")), zp = join(dir, "c.zip");
  writeFileSync(zp, E.zipStore(zones.slice(0, 3).map((z) => ({ name: "BREAK/" + z.fname, data: E.wavFile(z.s) }))));
  const r = py(`import sys, zipfile
z = zipfile.ZipFile(sys.argv[1]); assert z.testzip() is None
print(",".join(i.filename + ":" + str(i.file_size) for i in z.infolist()))`, zp).toString().trim();
  ok(r === zones.slice(0, 3).map((z) => `BREAK/${z.fname}:${44 + z.s.length * 2}`).join(), "chop: ZIP of the WAVs (Python reads it)");

  /* keep / leave out, own lengths, fit: a recording longer than a slot */
  const opt = [null, { off: true }, { len: 30 }];
  const kl = E.chopList([10, 50, 400], 1000, 100, opt);
  ok(kl.map((c) => `${c.start}-${c.end}${c.off ? "x" : ""}/${c.full}`).join() === "10-50/50,50-150x/400,400-430/1000"
    && E.chopList([10, 400], 1000, 0, [{ len: 9999 }])[0].end === 400,
    "chop: options (left out, own length over the max, never past the next marker)");
  const kz = E.chopZones(x, E.chopList(hits, N, 0, [{}, { off: true }, {}, { off: true }]), 60, 0);
  ok(kz.length === 6 && kz.map((z) => z.root).join() === "60,61,62,63,64,65" && kz[1].fname === "CHOP03_C#4.wav" && kz[2].fname === "CHOP05_D4.wav",
    "chop: left-out chops: the kept ones on consecutive keys, files keep their numbers");
  const L = R * 20, long = new Float64Array(L);
  for (let i = 0; i < L; i++) long[i] = Math.sin(i * 0.05) * 0.5;
  const marks = E.chopEqual(0, L, 40), room = E.SMP.MAX_DATA * 2;
  const all = E.chopList(marks, L), pick = E.chopPick(all);
  ok(pick.length === 16 && pick[15].i === 15 && E.chopPick(all, 1, 7)[0].i === 7, "chop: 40 chops: the first 16 kept go to the slot; mode 1 the selected");
  const lo = E.chopList(marks, L, 0, marks.map((_, i) => ({ off: i % 4 !== 0 })));   /* keep 10 of 40 (each 0.5 s) */
  ok(E.chopPick(lo).length === 10 && E.chopFit(E.chopPick(lo), room) === Infinity, "chop: 20 s recording, 10 chops kept: they fit");
  const many = E.chopPick(E.chopList(marks, L, 0, marks.map((_, i) => ({ off: i >= 16 })))), Lf = E.chopFit(many, room - many.length);
  const fitted = many.map((c) => ({ ...c, end: Math.min(c.end, c.start + Lf) }));
  let built = null;
  try { built = E.buildSlot("LONG", E.chopZones(long, fitted, 60, 0)); } catch (e) { built = null; }
  ok(Lf < R * 0.5 && Lf > R * 0.4 && built && built.data.length <= E.SMP.MAX_DATA && built.hdr[6] === 16,
    `chop: Fit to slot: 16 x 0.5 s cut to ${(Lf / R).toFixed(3)} s each, the slot builds`);
  ok(E.chopFit([{ start: 0, end: 100 }, { start: 0, end: 300 }], 250) === 150, "chop: fit keeps short chops whole, cuts the long ones");
}

/* ------------------------------------------------------- packages: JS == Python --- */
async function packages() {
  const pkg = join(HERE, "../build/felucca.fwsc");
  if (!existsSync(pkg)) {
    console.log("packages: skipped (run ./build.sh first)");
    return;
  }
  const raw = readFileSync(pkg);
  const logical = py(`import sys; raw = open(sys.argv[1], "rb").read()
sys.stdout.buffer.write(b"".join(raw[i * 48:i * 48 + 47] for i in range(20)) + raw[960:])`, pkg);
  ok(eq(logicalImage(raw), logical), "fm1pkg.js logicalImage");
  ok(/^FM-1_9\d\d$/.test(productOf(raw)), "fm1pkg.js productOf");
}

/* ------------------------------------------------- update protocol (fm1ota.js) --- */
const HS = [0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7];
const UPGRADE = [0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7];

/* an FM-1 on WebMIDI: identity, then "device asks, host answers" reads of the image */
class FakeFM1 {
  constructor(image, { unplugAfter = Infinity, loaderIdentity = "ota-FM-1_900", finalIdentity = "FM-1_900" } = {}) {
    this.image = image; this.unplugAfter = unplugAfter; this.served = 0; this.bad = 0;
    this.loaderIdentity = loaderIdentity; this.finalIdentity = finalIdentity;
    this.access = { inputs: new Map(), outputs: new Map() };
    this.boot("FM-1_015", "FM-1");
  }
  boot(identity, name) {
    this.identity = identity; this.waiting = null; this.queue = [];
    for (const m of [this.access.inputs, this.access.outputs]) { for (const p of m.values()) p.state = "disconnected"; m.clear(); }
    const id = Math.random().toString(36).slice(2);
    this.input = { id: "i" + id, name, state: "connected", onmidimessage: null, open: async () => {} };
    this.output = { id: "o" + id, name, state: "connected", open: async () => {}, send: (d) => {
      if (this.output.state !== "connected") throw new Error("InvalidStateError");
      setTimeout(() => this.rx(Array.from(d)), 1);
    } };
    this.access.inputs.set(this.input.id, this.input);
    this.access.outputs.set(this.output.id, this.output);
  }
  tx(bytes) { const i = this.input; setTimeout(() => { if (i.state === "connected" && i.onmidimessage) i.onmidimessage({ data: Uint8Array.from(bytes) }); }, 1); }
  rx(d) {
    if (eq(d, HS)) {
      const t = [...new TextEncoder().encode(this.identity)];
      const body = [0, 0x59, 0x11, 0, 0, 0, ...t, ...new Array(28 - t.length).fill(0)];
      this.tx([0xF0, ...pack7(body), 0xF7]);
    } else if (eq(d, UPGRADE)) {
      this.queue = this.identity.startsWith("ota-")
        ? [...Array.from({ length: 6 }, (_, k) => [k * 512, 512]), [0xF0000000, 8]]
        : [[0, 64], [0x40, 160], [0x1000, 512], [0xE0000000, 8]];
      this.next();
    } else if (this.waiting) {
      const u = unpack7(d.slice(1, -1));
      const [addr, len] = this.waiting;
      const got = u.slice(14, 14 + (addr >= 0xE0000000 ? 8 : len));
      const want = addr >= 0xE0000000 ? [...new TextEncoder().encode("success"), 0] : Array.from(this.image.subarray(addr, addr + len));
      if (!eq(got, want)) this.bad++;
      this.waiting = null;
      this.served++;
      if (this.served >= this.unplugAfter) { this.input.state = this.output.state = "disconnected"; return; }
      if (addr === 0xE0000000) setTimeout(() => this.boot(this.loaderIdentity, "FM-1 Update"), 300);
      else if (addr === 0xF0000000) setTimeout(() => this.boot(this.finalIdentity, "Jangada"), 300);
      else this.next();
    }
  }
  next() {
    const r = this.queue.shift();
    if (!r) return;
    this.waiting = r;
    const [addr, len] = r;
    const u = [0, 0x59, 0x30, 0, 0, 0, 0, addr & 0xFF, (addr >>> 8) & 0xFF, (addr >>> 16) & 0xFF, (addr >>> 24) & 0xFF, len & 0xFF, len >> 8, 0];
    let s = 0;
    for (let i = 6; i < 14; i++) s += u[i];
    u.push(~s & 0xFF);
    this.tx([0xF0, ...pack7(u), 0xF7]);
  }
}

async function updater() {
  const image = Uint8Array.from({ length: 0x2000 }, (_, i) => (i * 7) & 0xFF);
  const dev = new FakeFM1(image);
  const steps = [];
  const got = await new Updater(dev.access).install(image, "FM-1_900", (k) => steps.push(k));
  ok(got === "FM-1_900" && dev.bad === 0 && steps.includes("write") && steps.at(-1) === "done",
    `fm1ota.js: install: running firmware -> loader -> Felucca (${dev.served} reads)`);

  const dev2 = new FakeFM1(image, { unplugAfter: 3 });
  dev2.boot("ota-FM-1_900", "Felucca Update");
  const t0 = Date.now();
  const done = await new Updater(dev2.access).resume(image);
  ok(done === false && Date.now() - t0 < 6000, "fm1ota.js: unplugged during the write -> stops at once");

  const dev3 = new FakeFM1(image, { unplugAfter: 2 });
  const e = await new Updater(dev3.access).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e && e.code === "lost", "fm1ota.js: unplugged in step 1 -> error code 'lost'");
  const e2 = await new Updater({ inputs: new Map(), outputs: new Map() }).install(image, "FM-1_900").then(() => null, (x) => x);
  ok(e2 && e2.code === "notfound", "fm1ota.js: no device -> error code 'notfound'");

  /* the return to the official V15 (Jangada, after Felucca 1.0 / SLOOP 2.3) */
  ok(OUR_LOADER({ text: "ota-FM-1_900" }) && !OUR_LOADER({ text: "ota-FM-1_015" }), "fm1ota.js: our loader is ota-FM-1_9XX");
  const foreign = new FakeFM1(image);
  foreign.boot("ota-FM-1_015", "FM-1 Update");
  const e3 = await new Updater(foreign.access).resume(image).then(() => null, (x) => x);
  ok(e3 && e3.code === "foreign" && e3.detail === "ota-FM-1_015" && foreign.served === 0, "fm1ota.js: another firmware's loader is never resumed by Install ('foreign')");
  const back = new FakeFM1(image, { finalIdentity: "FM-1_015" });
  back.boot("ota-FM-1_015", "FM-1 Update");
  const st = [];
  const r4 = await new Updater(back.access).resume(image, (k) => st.push(k), { product: "FM-1_015" });
  ok(r4 === true && back.bad === 0 && st.at(-1) === "done", "fm1ota.js: return to official interrupted: its loader resumed, V15 checked when back");
  const wrong = new FakeFM1(image, { finalIdentity: "FM-1_900" });
  wrong.boot("ota-FM-1_015", "FM-1 Update");
  const e5 = await new Updater(wrong.access).resume(image, null, { product: "FM-1_015" }).then(() => null, (x) => x);
  ok(e5 && e5.code === "mismatch", "fm1ota.js: return to official: another firmware coming back is not 'done'");
  const e6 = await validateStockPackage(new Uint8Array(STOCK_V15_SIZE)).then(() => null, (x) => x);
  ok(e6 && /official FM-1 V15/.test(e6.message), "fm1pkg.js: a file of the V15's size that is not it is refused (SHA-256)");
  /* the official file itself, when it is on this computer (FM1_STOCK=path, or the usual places) */
  const cand = [process.env.FM1_STOCK, ...Array.from({ length: 7 }, (_, k) => join(HERE, "../".repeat(k + 1), "firmware/FM-1_v15_oficial.fwsc")),
    join(process.env.HOME || "", ".local/share/jangada/FM-1_V15_oficial.fwsc")].filter((f) => f && existsSync(f));
  if (!cand.length) { console.log("fm1pkg.js: official V15 file not found: its checks skipped (FM1_STOCK=path)"); return; }
  const raw = new Uint8Array(readFileSync(cand[0]));
  const sv = await validateStockPackage(raw);
  const bent = raw.slice(); bent[123456] ^= 1;
  const e7 = await validateStockPackage(bent).then(() => null, (x) => x);
  ok(sv.product === STOCK_V15_PRODUCT && (await sha256hex(raw)) === STOCK_V15_SHA256 && eq(sv.image, logicalImage(raw)) && e7,
    "fm1pkg.js: the official V15 file is accepted, one byte changed is not");
  const fm = new FakeFM1(sv.image, { loaderIdentity: "ota-FM-1_015", finalIdentity: "FM-1_015" });
  fm.boot("FM-1_900", "Jangada");
  const sst = [];
  const sgot = await new Updater(fm.access).install(sv.image, sv.product, (k) => sst.push(k));
  ok(sgot === "FM-1_015" && fm.bad === 0 && sst.at(-1) === "done", "fm1ota.js: Jangada -> official V15 (the official loader, FM-1_015 when back)");
}

/* ------------------------------------------------------ installer page --- */
function installerPage() {
  const page = readFileSync(join(HERE, "index_pkg.html"), "utf8");
  const tb = page.slice(page.indexOf("const TEXT = {"), page.indexOf("\n};", page.indexOf("const TEXT = {")) + 2);
  const TEXT = vm.runInNewContext(tb.replace("const TEXT =", "(") + ")");
  const keys = (l) => new Set(Object.keys(TEXT[l]).filter((k) => k !== "other"));
  const pt = keys("pt"), en = keys("en"), ja = keys("ja");
  const used = new Set([...page.matchAll(/data-t="(\w+)"|\bt\("(\w+)"\)|sayK\("(\w+)"/g)].map((x) => x[1] || x[2] || x[3]));
  const miss = [...used].filter((k) => !pt.has(k) || !en.has(k) || !ja.has(k));
  const odd = [...pt].filter((k) => !en.has(k) || !ja.has(k)).concat([...en, ...ja].filter((k) => !pt.has(k)));
  ok(!miss.length && !odd.length, `installer: every text in pt, en and ja (${used.size} used${miss.length ? ", missing " + miss : ""}${odd.length ? ", not in all " + odd : ""})`);
  const visible = Object.values(TEXT).flatMap((o) => Object.values(o)).join(" ");
  ok(!/sloop|felucca/i.test(visible) && /Jangada/.test(page.slice(0, page.indexOf("<script"))), "installer: Jangada's name in the texts (no other firmware's)");
  const strip = (f) => readFileSync(join(HERE, f), "utf8").replace(/^export\s+/gm, "").replace(/^import .*?;\n/gm, "");
  const script = page.slice(page.indexOf('<script type="module">') + 22, page.lastIndexOf("</script>"))
    .replace("/*LIB*/", ["fm1pkg.js", "fm1ota.js", "fm1backup.js"].map(strip).join("\n")).replace("/*META*/", "{}");
  let err = null;
  try { new vm.SourceTextModule(script); } catch (e) { err = e.message; }
  if (err && /SourceTextModule/.test(err)) { try { new vm.Script(`(async () => {${script}\n})`); err = null; } catch (e) { err = e.message; } }
  ok(!err, "installer: the page script compiles (fm1pkg.js, fm1ota.js, fm1backup.js inlined)" + (err ? ` (${err})` : ""));
}

await editorMock();
await editorLibrarian();
await editorLive();
await editorTracks();
await editorMixer();
await editorTrackParam();
await editorBackup();
await editorFm6();
chopTests();
editorTabs();
editorIcons();
samplesMatch();
await packages();
await updater();
installerPage();
console.log(failed ? `WEB TESTS FAILED (${failed})` : "web tests passed");
process.exit(failed ? 1 : 0);
