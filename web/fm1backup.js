// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
// Jangada (after Felucca 1.0.1 and SLOOP 2.3): the complete backup of the FM-1 and its restore, editor
// protocol v5 (EDITOR_PROTOCOL.md, cmds 34-36; firmware/src/editor_backup.c). Requests name objects,
// never flash addresses. Used by the editor (import("./fm1backup.js")) and, inlined, by the installer
// (the backup before the return to the official firmware).
//
// The file: JSON {format: "jangada-backup", version: 1, firmware, created, objects: [{id, size, crc,
// data (base64)}]}, the 15 objects in BACKUP_IDS order (a file of Jangada 0.5: the 13 of BACKUP_IDS_V6,
// without the FM6 bank 2; of Jangada 0.3 / 0.4: the 11 of BACKUP_IDS_V5, without the FM6 bank). It is
// checked whole (sizes, CRCs, the sample headers) before the first write.
export const BACKUP_IDS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 32, 33, 34];   // working project, settings, projects 1-4, user banks,
                                                                   // FM6 bank 1 B1-16 / B17-32, bank 2 B33-48 / B49-64, USR1-3
export const BACKUP_IDS_V6 = BACKUP_IDS.filter((id) => id !== 10 && id !== 11);   // protocol v6 (Jangada 0.5: one FM6 bank)
export const BACKUP_IDS_V5 = BACKUP_IDS_V6.filter((id) => id !== 8 && id !== 9);   // protocol v5 (no FM6 bank)
export const BACKUP_CMD = { LIST: 34, GET: 35, PUT: 36 };
export const BACKUP_PROTO = 5;                                     // INFO's protocol byte: v5 firmware has the backup
export const BACKUP_PROTO_FM6 = 6;                                 // v6: and the FM6 bank (ids 8, 9)
export const BACKUP_PROTO_FM6B = 7;                                // v7 (Jangada 0.6): and the FM6 bank 2 (ids 10, 11)
// the object ids a device of protocol version proto keeps
export const backupIds = (proto) => (proto >= BACKUP_PROTO_FM6B ? BACKUP_IDS : proto >= BACKUP_PROTO_FM6 ? BACKUP_IDS_V6 : BACKUP_IDS_V5);
const sameIds = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
const ID_SETS = [BACKUP_IDS, BACKUP_IDS_V6, BACKUP_IDS_V5];
const knownIds = (ids) => ID_SETS.some((x) => sameIds(ids, x));
export const BACKUP_FORMAT = "jangada-backup";
const SMP = { BEGIN: 11, WRITE: 12, END: 13, ERASE: 14, DATA: 512, HDR: 480, SLOT: 81920 };
const OBJ_MAX = 3840;                                              // one flash object (storage.c ST_PAYLOAD_MAX)
const CHUNK = 256;

export const bkU32 = (n) => Array.from({ length: 5 }, (_, i) => (n >>> (i * 7)) & (i === 4 ? 15 : 127));
export const bkR32 = (a, off = 0) => {
  if (a.length < off + 5 || a[off + 4] > 15) throw new BackupError("bkBad", "invalid number");
  return (a[off] | a[off + 1] << 7 | a[off + 2] << 14 | a[off + 3] << 21 | a[off + 4] << 28) >>> 0;
};
export function bkPack(bytes) {
  const a = [];
  for (let off = 0; off < bytes.length; off += 7) {
    const chunk = bytes.subarray(off, off + 7);
    a.push(chunk.reduce((m, b, i) => m | (b >>> 7) << i, 0), ...Array.from(chunk, (b) => b & 127));
  }
  return a;
}
export function bkUnpack(a, size) {
  const out = new Uint8Array(size); let i = 0, j = 0;
  while (j < size) {
    const n = Math.min(7, size - j), mask = a[i++];
    if (mask == null || mask >>> n) throw new BackupError("bkRead", "invalid bytes");
    for (let k = 0; k < n; k++) {
      if (a[i] == null || a[i] > 127) throw new BackupError("bkRead", "short chunk");
      out[j++] = a[i++] | (mask >>> k & 1) << 7;
    }
  }
  if (i !== a.length) throw new BackupError("bkRead", "trailing bytes");
  return out;
}
export function bkCrc(bytes) {
  let c = 0xffffffff;
  for (const b of bytes) {
    c ^= b;
    for (let k = 0; k < 8; k++) c = c >>> 1 ^ (c & 1 ? 0xedb88320 : 0);
  }
  return (~c) >>> 0;
}
// code: a text key of the pages (bkBad, bkStop, bkStale, bkRead, bkWrite, bkFlash, bkOld); message: the detail
export class BackupError extends Error { constructor(code, msg) { super(msg); this.code = code; } }
const RC = { 1: "arguments", 2: "not a valid object", 3: "stop the song first", 4: "flash", 5: "the device changed: start again" };
function rcCheck(rc, code, what) {
  if (!rc) return;
  throw new BackupError(rc === 3 ? "bkStop" : rc === 4 ? "bkFlash" : rc === 5 ? "bkStale" : code, `${what}: ${RC[rc] || "rc " + rc}`);
}
const maxSize = (id) => (id >= 32 ? SMP.SLOT : OBJ_MAX);

// LIST reply -> [{id, size, crc}] (15 objects, 13 from a v6 device, 11 from a v5 one)
export function bkManifest(a) {
  rcCheck(a[0], "bkRead", "LIST");
  const ids = ID_SETS.find((x) => x.length === a[1]);
  if (!ids || a.length !== 2 + a[1] * 11)
    throw new BackupError("bkRead", "incomplete list");
  return ids.map((id, i) => {
    const p = 2 + i * 11;
    if (a[p] !== id) throw new BackupError("bkRead", "unexpected object");
    const size = bkR32(a, p + 1), crc = bkR32(a, p + 6);
    if (size > maxSize(id)) throw new BackupError("bkRead", `object ${id} too large`);
    return { id, size, crc };
  });
}

const B64 = /^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/;
const toB64 = (bytes) => { let s = ""; for (let i = 0; i < bytes.length; i += 0x8000) s += String.fromCharCode(...bytes.subarray(i, i + 0x8000)); return btoa(s); };
const fromB64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));

// a sample slot as SMP_END and eng_sample.c smp_user_scan check it
function sampleOk(b) {
  if (b.length < SMP.DATA) return false;
  const v = new DataView(b.buffer, b.byteOffset, b.byteLength), len = v.getUint32(16, true), nz = b[6];
  if (v.getUint32(0, true) !== 0x504d5346 || v.getUint16(4, true) !== 1 || !nz || nz > 16 || len !== b.length - SMP.DATA ||
      bkCrc(b.subarray(SMP.DATA)) !== v.getUint32(20, true)) return false;
  for (let z = 0; z < nz; z++) {
    const o = 32 + z * 28, off = v.getUint32(o, true), n = v.getUint32(o + 4, true), ls = v.getUint32(o + 8, true),
      le = v.getUint32(o + 12, true), rate = v.getUint32(o + 16, true);
    if (!n || off > len || Math.ceil(n / 2) > len - off || n > 2 * SMP.SLOT || ls > le || le >= n || !rate || rate > 4 * 65536 ||
        b[o + 24] > 88 || b[o + 25] > b[o + 26]) return false;
  }
  return true;
}

// a backup file (object or JSON text) -> the file with objects[i].bytes; throws BackupError("bkBad") on anything wrong
export function readBackup(file) {
  try { if (typeof file === "string") file = JSON.parse(file); } catch { file = null; }
  if (!file || file.format !== BACKUP_FORMAT || file.version !== 1 || !Array.isArray(file.objects) ||
      !knownIds(file.objects.map((o) => o && o.id))) throw new BackupError("bkBad", "not a complete Jangada backup");
  const objects = file.objects.map((o, i) => {
    const id = file.objects[i].id;
    if (!o || o.id !== id || !Number.isInteger(o.size) || o.size < 0 || o.size > maxSize(id) || !Number.isInteger(o.crc) ||
        o.crc < 0 || o.crc > 0xffffffff || typeof o.data !== "string" || o.data.length !== 4 * Math.ceil(o.size / 3) || !B64.test(o.data))
      throw new BackupError("bkBad", `object ${id}: invalid`);
    const bytes = fromB64(o.data);
    if (bytes.length !== o.size || bkCrc(bytes) !== o.crc) throw new BackupError("bkBad", `object ${id}: damaged (CRC)`);
    if (id >= 32 && o.size && !sampleOk(bytes)) throw new BackupError("bkBad", `USR${id - 31}: not a sample slot`);
    return { ...o, bytes };
  });
  if (!objects[0].size || !objects[1].size) throw new BackupError("bkBad", "no working project or settings");
  return { ...file, objects };
}

// everything from the device -> the file. request([cmd, args], {timeout, retries}) -> the reply's args.
// onProgress(done, total bytes)
export async function captureBackup(request, firmware, onProgress = () => {}) {
  const manifest = bkManifest(await request([BACKUP_CMD.LIST, []], { timeout: 5000, retries: 0 }));
  const total = manifest.reduce((n, o) => n + o.size, 0) || 1, objects = [];
  let done = 0;
  for (const o of manifest) {
    const bytes = new Uint8Array(o.size);
    for (let off = 0; off < o.size; off += CHUNK) {
      const n = Math.min(CHUNK, o.size - off);
      const a = await request([BACKUP_CMD.GET, [o.id, ...bkU32(off), n & 127, n >>> 7]], { timeout: 1500, retries: 1 });
      rcCheck(a[1], "bkRead", `GET ${o.id}+${off}`);
      if (a[0] !== o.id || bkR32(a, 2) !== off || (a[7] | a[8] << 7) !== n) throw new BackupError("bkRead", "unexpected reply");
      bytes.set(bkUnpack(a.slice(9), n), off);
      done += n;
      onProgress(done, total);
    }
    if (bkCrc(bytes) !== o.crc) throw new BackupError("bkStale", `object ${o.id} changed during the backup`);
    objects.push({ id: o.id, size: o.size, crc: o.crc, data: toB64(bytes) });
  }
  const file = { format: BACKUP_FORMAT, version: 1, firmware: String(firmware || ""), created: new Date().toISOString(), objects };
  readBackup(file);
  return file;
}

// the file -> the device. Every byte is checked before the first write; then the projects and the user
// banks, the FM6 bank, the samples, the settings, and the working project last (each object commits by
// itself: an interrupted restore leaves whole objects, some of them still the old ones). opt.ids: the
// objects the device keeps (backupIds(INFO's proto)): the others are left out (a v5 device has no FM6
// bank, a v6 one no bank 2); a file without the FM6 bank (or without bank 2) leaves the device's as it is
export async function restoreBackup(request, file, onProgress = () => {}, opt = {}) {
  const archive = readBackup(file);
  const keep = new Set(opt.ids || BACKUP_IDS);
  const objs = archive.objects.filter((o) => keep.has(o.id));
  const total = objs.reduce((n, o) => n + o.size, 0) || 1;
  let done = 0;
  const ask = (r, o = {}) => request(r, { timeout: 4000, retries: 0, ...o });
  const put = async (args, what) => { const a = await ask([BACKUP_CMD.PUT, args]); rcCheck(a[2], "bkWrite", what); };
  for (const o of [...objs.slice(2), objs[1], objs[0]]) {
    if (o.id >= 32) {
      const k = o.id - 32, name = `USR${k + 1}`;
      const smp = async (r, what, timeout = 2500) => {
        const a = await ask(r, { timeout });
        if (a[0] !== k) throw new BackupError("bkWrite", `${name}: unexpected reply`);
        if (a.at(-1)) throw new BackupError("bkWrite", `${name} ${what}: rc ${a.at(-1)}`);
      };
      if (!o.size) { await smp([SMP.ERASE, [k]], "erase", 5000); continue; }
      await smp([SMP.BEGIN, [k]], "begin");
      for (let off = SMP.DATA; off < o.size; off += CHUNK) {
        const chunk = o.bytes.subarray(off, off + CHUNK);
        await smp([SMP.WRITE, [k, off & 127, off >>> 7 & 127, off >>> 14 & 127, ...bkPack(chunk)]], "write");
        done += chunk.length;
        onProgress(done, total);
      }
      await smp([SMP.END, [k, ...bkPack(o.bytes.subarray(0, SMP.HDR))]], "end");
      done += SMP.DATA;
      onProgress(done, total);
      continue;
    }
    await put([0, o.id, ...bkU32(o.size), ...bkU32(o.crc)], `begin ${o.id}`);
    try {
      for (let off = 0; off < o.size; off += CHUNK) {
        const chunk = o.bytes.subarray(off, off + CHUNK);
        await put([1, o.id, ...bkU32(off), ...bkPack(chunk)], `data ${o.id}+${off}`);
        done += chunk.length;
        onProgress(done, total);
      }
      await put([2, o.id], `commit ${o.id}`);
    } catch (e) {
      await put([3, o.id], "abort").catch(() => {});
      throw e;
    }
  }
  return archive;
}

export const backupName = (date = new Date()) => `jangada-backup-${date.toISOString().slice(0, 10)}.json`;

// the installer's own connection (no editor page owns the port then): F0 7D 46 4C cmd args F7
export class BackupConnection {
  constructor(input, output) {
    this.input = input; this.output = output; this.pending = null;
    input.onmidimessage = (e) => {
      const d = e.data;
      if (d.length < 6 || d[0] !== 240 || d[1] !== 125 || d[2] !== 70 || d[3] !== 76 || d[d.length - 1] !== 247) return;
      if (this.pending && this.pending.cmd === d[4]) {
        const p = this.pending; this.pending = null; clearTimeout(p.timer); p.resolve(Array.from(d.slice(5, -1)));
      }
    };
  }
  async request([cmd, args], opt = {}) {
    if (this.pending || this.closed) throw new BackupError("bkRead", "connection closed or busy");
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending = null; reject(new BackupError("bkRead", "no reply: keep the FM-1 connected")); }, opt.timeout || 1500);
      this.pending = { cmd, timer, resolve, reject };
      try { this.output.send([240, 125, 70, 76, cmd, ...args, 247]); }
      catch (e) { clearTimeout(timer); this.pending = null; reject(e); }
    });
  }
  // INFO -> {version, proto}; proto < BACKUP_PROTO: no backup in this firmware
  async info() {
    const a = await this.request([1, []], { timeout: 800 });
    let i = 0, version = "";
    while (i < a.length && a[i]) version += String.fromCharCode(a[i++]);
    i++;
    const ne = a[i];
    i += 5;
    for (let e = 0; e < ne; e++) { while (i < a.length && a[i]) i++; i++; }
    const ntrk = i < a.length ? a[i++] : 0, proto = i < a.length ? a[i] : 0;
    return { version, ntrk, proto };
  }
  close() {
    this.closed = true; this.input.onmidimessage = null;
    if (this.pending) { const p = this.pending; this.pending = null; clearTimeout(p.timer); p.reject(new BackupError("bkRead", "connection closed")); }
  }
}
