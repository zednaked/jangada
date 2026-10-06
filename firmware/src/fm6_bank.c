/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Jangada: after Felucca 1.0 (fm6_bank.c, Leo Kuroshita) and Melodee (fm6_store.c, Kerem Kilic / Ellic Studio:
 * a whole 32-voice bank) */
/* The FM6 patch banks (eng_fm6.c PTCH B1..B64): two banks of 32 packed 128-byte patches, as a 32-voice bank holds
 * them, each in two storage.c objects (A/B pairs). Bank 1 in FL_FM6: B1..B16 OBJ_FM6BANK0 at 0xE5000 / 0xE6000,
 * B17..B32 OBJ_FM6BANK1 at 0xE7000 / 0xE8000. Jangada 0.6, bank 2 in FL_FM6B: B33..B48 OBJ_FM6BANK2 at 0x93000 /
 * 0x94000, B49..B64 OBJ_FM6BANK3 at 0x95000 / 0x96000 (storage.c has why those sectors are free). The four
 * halves are numbered 0..3 (the half field of each, the backup's ids 8, 9, 10, 11). No RAM mirror of the
 * records: a PTCH turn reads its record from flash (st_view: the object's current copy, CRC-checked, in
 * storage.c's own buffer); RAM keeps which slots are used and their names (eng_fm6.c fm6_bank_used / fm6_bank_nm,
 * the PRESETS list's). A write builds the half in
 * project.c's proj_io (the main loop's stored-object buffer, as the editor's backup does) and commits it whole.
 * An empty slot, or a half of another layout, plays the init voice. The payload ends 2064 + 256 bytes into its
 * sector: the sector's tail stays erased (nothing there can look like an update record, ldr_core.c).
 * Written by the web editor (EDITOR_PROTOCOL.md FM6_PUT / FM6_ERASE, a half at a time with FM6_PUT target 3 and
 * FM6_COMMIT), by a 32-voice SysEx bank (fm6_sysex.c: into bank 1 or 2), restored by a backup (ids 8..11); not
 * while the backup holds proj_io (proj_io_bk, project.c). Included by felucca.c after project.c (FELUCCA_FLASH). */
#define FM6_BANK_MAGIC 0x42364D46u               /* "FM6B" */
#define FM6_HALF 16u                             /* slots per object */
#define FM6_NHALF (FM6_BANK_N / FM6_HALF)        /* 4: two per bank */
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 1, FM6_HALF */
    uint32_t used;                               /* bit k: slot k of this half holds a patch */
    uint16_t half, rsv;                          /* 0: B1..B16, 1: B17..B32, 2: B33..B48, 3: B49..B64 */
    uint8_t v[FM6_HALF][FM6_PACKED];
} fm6_half_t;
_Static_assert(sizeof(fm6_half_t) == 2064u && sizeof(fm6_half_t) + 256u <= 0xF00u, "FM6 bank half layout");
_Static_assert(FM6_BANK_VOICES == 2u * FM6_HALF && FM6_NHALF == 4u, "two halves a bank, two banks");

static uint32_t fm6_half_obj(uint32_t h) { return h < 2u ? OBJ_FM6BANK0 + h : OBJ_FM6BANK2 + h - 2u; }
static uint32_t fm6_half_mask(uint32_t h) { return 0xFFFFu << (h % 2u * FM6_HALF); }   /* its bits in its bank's word */

/* half h's used bits (and the names: v, or all empty) -> fm6_bank_used / fm6_bank_nm */
static void fm6_half_note(uint32_t h, uint32_t used, const uint8_t (*v)[FM6_PACKED])
{
    uint32_t i, *w = &fm6_bank_used[h / 2u];
    *w = (*w & ~fm6_half_mask(h)) | (used & 0xFFFFu) << (h % 2u * FM6_HALF);
    fm6_bank_gen++;
    for (i = 0; i < FM6_HALF; i++) {
        char *nm = fm6_bank_nm[h * FM6_HALF + i];
        if (v && ((used >> i) & 1u))
            fm6_pk_name(nm, v[i]);
        else
            nm[0] = 0;
    }
}

static int fm6_slot_used(uint32_t k)             /* bank slot k (0..63) holds a patch */
{
    return k < FM6_BANK_N && ((fm6_bank_used[k / FM6_BANK_VOICES] >> (k % FM6_BANK_VOICES)) & 1u);
}

/* a stored half (n bytes) as a load takes it: 1 = valid */
static int fm6_half_valid(const uint8_t *b, uint32_t n, uint32_t half)
{
    const fm6_half_t *h = (const fm6_half_t *)(const void *)b;
    uint32_t i;
    if (n != sizeof(fm6_half_t) || h->magic != FM6_BANK_MAGIC || h->ver != 1u || h->nslot != FM6_HALF ||
        h->half != half || (h->used >> FM6_HALF))
        return 0;
    for (i = 0; i < sizeof h->v; i++)
        if (((const uint8_t *)(const void *)h->v)[i] > 127u)
            return 0;
    return 1;
}

/* half h's current object, valid, or 0 */
static const fm6_half_t *fm6_half_view(uint32_t h)
{
    uint32_t n = 0;
    const uint8_t *b = flash_ok && h < FM6_NHALF ? st_view(fm6_half_obj(h), &n) : 0;
    return b && fm6_half_valid(b, n, h) ? (const fm6_half_t *)(const void *)b : 0;
}

/* eng_fm6.c fm6_bank_read: slot k's record -> pk, 0 = there is one */
static int fm6_bank_get(uint32_t k, uint8_t *pk)
{
    const fm6_half_t *h;
    if (!fm6_slot_used(k) || !(h = fm6_half_view(k / FM6_HALF)) ||
        !((h->used >> (k % FM6_HALF)) & 1u))
        return 1;
    memcpy(pk, h->v[k % FM6_HALF], FM6_PACKED);
    return 0;
}

/* bank bk (0, 1) whole -> rec (32 x 128 bytes), an empty slot as the init voice: each half read once (a bank dump,
 * fm6_sysex.c dx_send_bank; fm6_bank_get a slot at a time read the whole half 32 times) */
static void fm6_bank_get_all(uint32_t bk, uint8_t *rec)
{
    uint32_t h, i;
    for (h = 0; h < 2u; h++) {
        uint32_t hh = bk * 2u + h;
        const fm6_half_t *b = fm6_half_view(hh);
        for (i = 0; i < FM6_HALF; i++) {
            uint32_t k = h * FM6_HALF + i;
            if (b && ((b->used >> i) & 1u) && fm6_slot_used(hh * FM6_HALF + i))
                memcpy(rec + k * FM6_PACKED, b->v[i], FM6_PACKED);
            else
                memcpy(rec + k * FM6_PACKED, FM6_INIT, FM6_PACKED);
        }
    }
}

static void fm6_bank_scan(void)                  /* which slots are used and their names, from flash */
{
    uint32_t h;
    for (h = 0; h < FM6_NHALF; h++) {
        const fm6_half_t *b = fm6_half_view(h);
        fm6_half_note(h, b ? b->used : 0u, b ? b->v : 0);
    }
    fm6_bank_read = fm6_bank_get;
}

static void fm6_bank_boot(void) { fm6_bank_scan(); }   /* power-on, after persist_boot */

/* tracks whose PTCH loaded a slot of half h get it again (fm6_poll), as a slot's new patch */
static void fm6_bank_reload(uint32_t h)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        if (fm6_slot[t] >= FM6_NFACTORY && fm6_slot[t] < FM6_NSLOT && (fm6_slot[t] - FM6_NFACTORY) / FM6_HALF == h)
            fm6_slot[t] = 0xFFu;
}

/* a half as it will be stored -> proj_io: the current one (or an empty one), its slots sanitized */
static fm6_half_t *fm6_half_edit(uint32_t h)
{
    fm6_half_t *w = (fm6_half_t *)(void *)proj_io;
    const fm6_half_t *c = fm6_half_view(h);
    if (c)
        memcpy(w, c, sizeof *w);
    else {
        memset(w, 0, sizeof *w);
        w->magic = FM6_BANK_MAGIC;
        w->ver = 1;
        w->nslot = FM6_HALF;
        w->half = (uint16_t)h;
    }
    return w;
}

/* record pk -> slot i of the half being edited (unpack / pack: every value in its range) */
static void fm6_half_set(fm6_half_t *w, uint32_t i, const uint8_t *pk)
{
    uint8_t v[FP_SIZE + 1u];
    fm6_unpack(pk, v);
    memset(w->v[i], 0, FM6_PACKED);
    fm6_pack(v, w->v[i]);
    w->used |= 1u << i;
}

static int fm6_half_commit(uint32_t h, const fm6_half_t *w)   /* 0 ok, 2 flash */
{
    if (st_save(fm6_half_obj(h), w, sizeof *w))
        return 2;
    fm6_half_note(h, w->used, w->v);
    fm6_bank_reload(h);
    return 0;
}

/* half h emptied (a backup's empty object): 0 ok, 2 flash */
static int fm6_half_clear(uint32_t h)
{
    if (st_save(fm6_half_obj(h), proj_io, 0))
        return 2;
    fm6_half_note(h, 0, 0);
    fm6_bank_reload(h);
    return 0;
}

/* a half staged for the editor (editor_fm6.c: FM6_PUT target 3, then FM6_COMMIT): built in proj_io a record at a
 * time, written once (one erase for up to 16 slots instead of one each). The CRC of the whole half after each
 * record catches another use of proj_io meanwhile (a project save or load from the panel: nothing is written
 * then); the editor's backup drops it outright (fm6_stage_drop); a USB reset or 15 s without a record end it */
#define FM6_STAGE_HOLD 15000u
static uint8_t fm6_stage_on, fm6_stage_h;
static uint32_t fm6_stage_crc, fm6_stage_ms, fm6_stage_usb;

static void fm6_stage_drop(void) { fm6_stage_on = 0; }

static int fm6_stage_live(void)                  /* 1: a half is staged and proj_io still holds it whole */
{
    if (fm6_stage_on && (fm1_ms - fm6_stage_ms > FM6_STAGE_HOLD || fm6_stage_usb != usb.resets ||
                         st_crc32(proj_io, sizeof(fm6_half_t)) != fm6_stage_crc))
        fm6_stage_on = 0;
    return fm6_stage_on;
}

/* a bank write can go ahead: 0, else the rc of the editor protocol: 2 no flash, 3 the song plays (a flash erase
 * stops the audio a moment), 4 the editor's backup holds proj_io (its snapshot being read, or a restore being
 * staged: editor_backup.c, up to 15 s after its last request; a write would corrupt them). The top bar says */
static uint32_t fm6_bank_busy(void)
{
    if (!flash_ok)
        return 2;
    if (song.playing || transport_req) {
        ui_message("STOP TO SAVE");
        return 3;
    }
    if ((int32_t)(proj_io_bk - fm1_ms) > 0) {
        ui_message("BACKUP BUSY");
        return 4;
    }
    return 0;
}

/* slot k = the packed record pk (0: erase it), then that half to flash: 0 ok, 1 bad slot, 2 flash error or no
 * flash, 3 the song plays, 4 a backup holds proj_io (3, 4: nothing changed) */
static uint32_t fm6_bank_put(uint32_t k, const uint8_t *pk)
{
    fm6_half_t *w;
    uint32_t rc;
    if (k >= FM6_BANK_N)
        return 1;
    if ((rc = fm6_bank_busy()) != 0)
        return rc;
    fm6_stage_drop();                            /* (proj_io is taken) */
    w = fm6_half_edit(k / FM6_HALF);
    if (pk)
        fm6_half_set(w, k % FM6_HALF, pk);
    else {
        memset(w->v[k % FM6_HALF], 0, FM6_PACKED);
        w->used &= ~(1u << (k % FM6_HALF));
    }
    return fm6_half_commit(k / FM6_HALF, w) ? 2u : 0u;
}

/* bank bk (0: B1..B32, 1: B33..B64) whole = 32 packed records (a 32-voice SysEx bank's 4096 bytes, 7-bit), every
 * slot used: 0 ok, 1 no such bank, 2 flash error or no flash, 3 the song plays, 4 a backup holds proj_io. Each half
 * commits by itself */
static uint32_t fm6_bank_put_all(uint32_t bk, const uint8_t *rec)
{
    uint32_t h, i, rc;
    if (bk >= FM6_BANKS)
        return 1;
    if ((rc = fm6_bank_busy()) != 0)
        return rc;
    fm6_stage_drop();
    for (h = 0; h < 2u; h++) {
        fm6_half_t *w = fm6_half_edit(bk * 2u + h);
        for (i = 0; i < FM6_HALF; i++)
            fm6_half_set(w, i, rec + (h * FM6_HALF + i) * FM6_PACKED);
        if (fm6_half_commit(bk * 2u + h, w))
            return 2;
    }
    return 0;
}

/* record pk -> slot k of the staged half (the half's other slots as they are in flash): 0 ok, 1 bad slot, 2 no
 * flash, 4 a backup holds proj_io, 5 the other half is staged (commit it first) */
static uint32_t fm6_bank_stage(uint32_t k, const uint8_t *pk)
{
    fm6_half_t *w = (fm6_half_t *)(void *)proj_io;
    uint32_t h = k / FM6_HALF;
    if (k >= FM6_BANK_N)
        return 1;
    if (!flash_ok)
        return 2;
    if ((int32_t)(proj_io_bk - fm1_ms) > 0)
        return 4;
    if (fm6_stage_live() && fm6_stage_h != h)
        return 5;
    if (!fm6_stage_on) {
        w = fm6_half_edit(h);
        fm6_stage_h = (uint8_t)h;
        fm6_stage_usb = usb.resets;
        fm6_stage_on = 1;
    }
    fm6_half_set(w, k % FM6_HALF, pk);
    fm6_stage_crc = st_crc32(proj_io, sizeof *w);
    fm6_stage_ms = fm1_ms;
    autosave_hold = fm1_ms + FM6_STAGE_HOLD;     /* (the autosave keeps off proj_io) */
    return 0;
}

/* the staged half to flash: 0 ok, 2 flash error or no flash, 3 the song plays (the staging stays: stop, then
 * commit again), 4 a backup holds proj_io, 5 nothing staged (or it lapsed, or proj_io was used meanwhile:
 * nothing written, stage again) */
static uint32_t fm6_bank_commit(void)
{
    uint32_t rc;
    if (!fm6_stage_live())
        return 5;
    if ((rc = fm6_bank_busy()) != 0)
        return rc;
    fm6_stage_on = 0;
    return fm6_half_commit(fm6_stage_h, (const fm6_half_t *)(const void *)proj_io) ? 2u : 0u;
}
