// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Jangada (after Felucca 1.0.1 web/test_backup.mjs): fm1backup.js against a simulated device that
// speaks protocol v5 as firmware/src/editor_backup.c: 7-bit numbers and packing, CRC-32, the LIST
// checks, a full capture, every byte checked before the first write, the restore order (the working
// project last), an abort on a refused chunk, the sample slots through SMP_*; the FM6 bank (ids 8, 9,
// protocol v6) and the backups / devices of v5 without it.
//   node web/test_backup.mjs
import { BACKUP_IDS, BACKUP_IDS_V5, BACKUP_CMD, bkU32, bkR32, bkPack, bkUnpack, bkCrc, bkManifest, readBackup, captureBackup,
  restoreBackup, backupName, backupIds } from "./fm1backup.js";

let fails = 0;
const ok = (c, what) => { console.log(`${what.padEnd(76)} ${c ? "ok" : "FAIL"}`); if (!c) fails++; };
const throws = (f, code) => { try { f(); return false; } catch (e) { return !code || e.code === code; } };
const athrows = async (f, code) => { try { await f(); return false; } catch (e) { return !code || e.code === code; } };

const rnd = (n, seed) => Uint8Array.from({ length: n }, (_, i) => (i * 131 + seed * 17 + (i >> 3)) & 255);
ok(bkR32(bkU32(0xdeadbeef)) === 0xdeadbeef && bkCrc(new TextEncoder().encode("123456789")) === 0xcbf43926, "backup: numbers (5 x 7 bit) and CRC-32 (zlib)");
const b = rnd(1000, 1);
ok(bkUnpack(bkPack(b), b.length).every((v, i) => v === b[i]), "backup: pack7 / unpack round trip");
ok(throws(() => bkUnpack([...bkPack(b.subarray(0, 14)), 0], 14)), "backup: trailing bytes refused");

/* a valid user sample slot (header as eng_sample.c reads it, ADPCM data from byte 512) */
function sampleSlot(n, seed) {
  const s = new Uint8Array(512 + n), v = new DataView(s.buffer);
  s.set(rnd(n, seed), 512);
  v.setUint32(0, 0x504d5346, true); v.setUint16(4, 1, true); s[6] = 1;
  s.set([75, 69, 69, 80], 8);
  v.setUint32(16, n, true); v.setUint32(20, bkCrc(s.subarray(512)), true);
  v.setUint32(32 + 4, 2 * n, true); v.setUint32(32 + 12, 2 * n - 1, true); v.setUint32(32 + 16, 65536, true); s[32 + 26] = 127;
  return s;
}

/* a device: objects by id, the order of the commits */
function device(objs, opt = {}) {
  const d = { objs: new Map(objs), log: [], staged: null };
  d.request = async ([cmd, a]) => {
    if (cmd === BACKUP_CMD.LIST) {
      const ids = opt.v5 ? BACKUP_IDS_V5 : BACKUP_IDS, out = [0, ids.length];
      for (const id of ids) { const v = d.objs.get(id) || new Uint8Array(0); out.push(id, ...bkU32(v.length), ...bkU32(bkCrc(v))); }
      return out;
    }
    if (cmd === BACKUP_CMD.GET) {
      const id = a[0], off = bkR32(a, 1), n = a[6] | a[7] << 7, v = d.objs.get(id);
      if (opt.changeOnGet === id && off === 0) v[0] ^= 1;
      return [id, 0, ...bkU32(off), n & 127, n >> 7, ...bkPack(v.subarray(off, off + n))];
    }
    if (cmd === BACKUP_CMD.PUT) {
      const [op, id] = a;
      if (op === 0 && opt.v5 && id > 7) return [op, id, 1];
      if (op === 0) { d.staged = { id, size: bkR32(a, 2), crc: bkR32(a, 7), bytes: [] }; return [op, id, 0]; }
      if (op === 1) {
        if (opt.failChunk === id) return [op, id, 2];
        const off = bkR32(a, 2), n = Math.min(256, d.staged.size - off);
        d.staged.bytes.push(...bkUnpack(a.slice(7), n));
        return [op, id, 0];
      }
      if (op === 2) {
        if (opt.playing && id <= 5) return [op, id, 3];
        const s = Uint8Array.from(d.staged.bytes), rc = bkCrc(s) === d.staged.crc ? 0 : 2;
        if (!rc) { d.objs.set(id, s); d.log.push(id); }
        return [op, id, rc];
      }
      if (op === 3) { d.log.push(`abort ${id}`); return [op, id, 0]; }
    }
    if (cmd === 11) { d.smp = { slot: a[0], bytes: new Uint8Array(81920) }; return [a[0], 0]; }
    if (cmd === 12) {
      const off = a[1] | a[2] << 7 | a[3] << 14, p = bkUnpack(a.slice(4), Math.min(256, ((a.length - 4) * 7) >> 3));
      d.smp.bytes.set(p, off); d.smp.end = off + p.length;
      return [a[0], a[1], a[2], a[3], 0];
    }
    if (cmd === 13) {
      d.smp.bytes.set(bkUnpack(a.slice(1), 480), 0);
      d.objs.set(32 + a[0], d.smp.bytes.slice(0, d.smp.end)); d.log.push(32 + a[0]);
      return [a[0], 0];
    }
    if (cmd === 14) { d.objs.set(32 + a[0], new Uint8Array(0)); d.log.push(32 + a[0]); return [a[0], 0]; }
    throw new Error(`unexpected ${cmd}`);
  };
  return d;
}
const objs = [[0, rnd(3398, 2)], [1, rnd(28, 3)], [3, rnd(3398, 4)], [7, rnd(3684, 5)], [9, rnd(2064, 7)], [34, sampleSlot(3000, 6)]];
const dev = device(objs);
const file = await captureBackup(dev.request, "JANGADA 0.2");
ok(file.format === "jangada-backup" && file.objects.length === 13 && file.objects[0].size === 3398 && file.objects[2].size === 0 &&
   file.objects[8].size === 0 && file.objects[9].size === 2064 && file.objects[12].size === 3512,
   "backup: the capture lists every object (13: the FM6 bank too), empty ones as 0");
ok(readBackup(JSON.stringify(file)).objects[3].bytes.every((v, i) => v === objs[2][1][i]), "backup: capture -> file -> bytes, the same");
ok(await athrows(() => captureBackup(device(objs.map(([i, v]) => [i, v.slice()]), { changeOnGet: 3 }).request, "x"), "bkStale"),
   "backup: an object that changes during the capture fails it");
ok(/^jangada-backup-\d{4}-\d\d-\d\d\.json$/.test(backupName()), "backup: the file name jangada-backup-DATE.json");

const bad = JSON.parse(JSON.stringify(file)); bad.objects[3].crc ^= 1;
ok(throws(() => readBackup(bad), "bkBad"), "backup: a damaged object (CRC) is refused");
const noRun = JSON.parse(JSON.stringify(file)); noRun.objects[0] = { ...noRun.objects[0], size: 0, crc: 0, data: "" };
ok(throws(() => readBackup(noRun), "bkBad"), "backup: a file without the working project is refused");
const short = JSON.parse(JSON.stringify(file)); short.objects.pop();
ok(throws(() => readBackup(short), "bkBad"), "backup: a file without one of the 13 objects is refused");
const other = JSON.parse(JSON.stringify(file)); other.format = "felucca-backup";
ok(throws(() => readBackup(other), "bkBad"), "backup: another firmware's backup (felucca / sloop) is refused");
ok(throws(() => readBackup("{ not json"), "bkBad"), "backup: not JSON at all is refused");
{
  const smp = JSON.parse(JSON.stringify(file)), s = sampleSlot(3000, 6);
  s[32 + 24] = 99;                                    // a step index past 88: smp_user_scan would refuse it
  smp.objects[12] = { ...smp.objects[12], crc: bkCrc(s), data: Buffer.from(s).toString("base64") };
  ok(throws(() => readBackup(smp), "bkBad"), "backup: a sample slot the device would not read is refused");
}
ok(throws(() => bkManifest([0, 3])), "backup: a short LIST is refused");
ok(throws(() => bkManifest([4, 0]), "bkFlash"), "backup: LIST rc 4 (no flash)");

const target = device([]);
const damaged = JSON.parse(JSON.stringify(file)); damaged.objects[7].crc ^= 1;
ok(await athrows(() => restoreBackup(target.request, damaged)) && target.log.length === 0, "backup: restore checks every byte before the first write");
await restoreBackup(target.request, file);
ok(target.log.at(-1) === 0 && target.log.at(-2) === 1 && target.log.indexOf(3) < target.log.indexOf(34) && target.log.indexOf(34) < target.log.indexOf(1),
   "backup: order: projects and banks, samples, settings, the working project last");
ok([0, 1, 3, 7, 34].every((id) => target.objs.get(id).every((v, i) => v === objs.find((o) => o[0] === id)[1][i])),
   "backup: the restored objects equal the source (USR3 through SMP_*)");
ok(target.log.includes(32) && target.objs.get(32).length === 0 && target.objs.get(2).length === 0, "backup: empty objects empty the device's (SMP_ERASE, an empty project)");
const failing = device([], { failChunk: 3 });
ok(await athrows(() => restoreBackup(failing.request, file), "bkWrite") && failing.log.includes("abort 3") && !failing.log.includes(0),
   "backup: a refused chunk aborts that object and stops before the working project");
ok(await athrows(() => restoreBackup(device([], { playing: true }).request, file), "bkStop"), "backup: the song playing: rc 3 -> 'stop the song first'");
ok(target.objs.get(9).every((v, i) => v === objs[4][1][i]) && target.log.indexOf(9) < target.log.indexOf(34),
   "backup: the FM6 bank (B17..B32) restored with the banks, before the samples");

/* protocol v5 (Jangada 0.3 / 0.4): 11 objects, no FM6 bank */
{
  const v5 = device(objs.filter(([id]) => id !== 9), { v5: true });
  const f5 = await captureBackup(v5.request, "JANGADA 0.4");
  ok(f5.objects.length === 11 && f5.objects.every((o, i) => o.id === BACKUP_IDS_V5[i]) && readBackup(JSON.stringify(f5)).objects.length === 11,
     "backup: a v5 device: 11 objects, and its file reads back");
  const t6 = device([[9, rnd(2064, 8)]]);
  await restoreBackup(t6.request, f5);
  ok(!t6.log.includes(8) && !t6.log.includes(9) && t6.objs.get(9).length === 2064 && t6.log.at(-1) === 0,
     "backup: a v5 file into a v6 device: restored, the FM6 bank left as it is");
  const t5 = device([], { v5: true });
  ok(await athrows(() => restoreBackup(t5.request, file)), "backup: a v6 file into a v5 device without the ids: refused (rc 1)");
  const t5b = device([], { v5: true });
  await restoreBackup(t5b.request, file, () => {}, { ids: backupIds(5) });
  ok(!t5b.log.includes(9) && t5b.log.at(-1) === 0 && backupIds(6) === BACKUP_IDS, "backup: ... with backupIds(proto 5): the FM6 bank left out");
  const mixed = JSON.parse(JSON.stringify(file)); mixed.objects.splice(9, 1);
  ok(throws(() => readBackup(mixed), "bkBad"), "backup: a file with only one FM6 bank half is refused");
}

console.log(fails ? `BACKUP WEB TESTS FAILED (${fails})` : "backup web tests passed");
process.exit(fails ? 1 : 0);
