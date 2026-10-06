/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Jangada: after Felucca 1.0 (fm6_bank.c, Leo Kuroshita) and Melodee (fm6_store.c, Kerem Kilic / Ellic Studio:
 * a whole 32-voice bank) */
/* The FM6 patch bank (eng_fm6.c PTCH B1..B32): 32 packed 128-byte patches, as a 32-voice bank holds them, in two
 * storage.c objects (A/B pairs in FL_FM6: B1..B16 OBJ_FM6BANK0 at 0xE5000 / 0xE6000, B17..B32 OBJ_FM6BANK1 at
 * 0xE7000 / 0xE8000). No RAM mirror: a PTCH turn reads its record from flash (st_view: the object's current copy,
 * CRC-checked, in storage.c's own buffer); RAM keeps only which slots are used. A write builds the half in
 * project.c's proj_io (the main loop's stored-object buffer, as the editor's backup does) and commits it whole.
 * An empty slot, or a half of another layout, plays the init voice. The payload ends 2064 + 256 bytes into its
 * sector: the sector's tail stays erased (nothing there can look like an update record, ldr_core.c).
 * Written by the web editor (EDITOR_PROTOCOL.md FM6_PUT / FM6_ERASE), by a 32-voice SysEx bank (fm6_sysex.c),
 * restored by a backup (ids 8, 9). Included by felucca.c after project.c (FELUCCA_FLASH). */
#define FM6_BANK_MAGIC 0x42364D46u               /* "FM6B" */
#define FM6_HALF 16u                             /* slots per object */
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 1, FM6_HALF */
    uint32_t used;                               /* bit k: slot k of this half holds a patch */
    uint16_t half, rsv;                          /* 0: B1..B16, 1: B17..B32 */
    uint8_t v[FM6_HALF][FM6_PACKED];
} fm6_half_t;
_Static_assert(sizeof(fm6_half_t) == 2064u && sizeof(fm6_half_t) + 256u <= 0xF00u, "FM6 bank half layout");
_Static_assert(FM6_BANK_N == 2u * FM6_HALF, "two halves");
static uint32_t fm6_bank_used;                   /* bit k: B(k + 1) holds a patch */

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
    const uint8_t *b = flash_ok ? st_view(OBJ_FM6BANK0 + h, &n) : 0;
    return b && fm6_half_valid(b, n, h) ? (const fm6_half_t *)(const void *)b : 0;
}

/* eng_fm6.c fm6_bank_read: slot k's record -> pk, 0 = there is one */
static int fm6_bank_get(uint32_t k, uint8_t *pk)
{
    const fm6_half_t *h;
    if (k >= FM6_BANK_N || !((fm6_bank_used >> k) & 1u) || !(h = fm6_half_view(k / FM6_HALF)) ||
        !((h->used >> (k % FM6_HALF)) & 1u))
        return 1;
    memcpy(pk, h->v[k % FM6_HALF], FM6_PACKED);
    return 0;
}

/* the whole bank -> rec (32 x 128 bytes), an empty slot as the init voice: each half read once (a bank dump,
 * fm6_sysex.c dx_send_bank; fm6_bank_get a slot at a time read the whole half 32 times) */
static void fm6_bank_get_all(uint8_t *rec)
{
    uint32_t h, i;
    for (h = 0; h < 2u; h++) {
        const fm6_half_t *b = fm6_half_view(h);
        for (i = 0; i < FM6_HALF; i++) {
            uint32_t k = h * FM6_HALF + i;
            if (b && ((b->used >> i) & 1u) && ((fm6_bank_used >> k) & 1u))
                memcpy(rec + k * FM6_PACKED, b->v[i], FM6_PACKED);
            else
                memcpy(rec + k * FM6_PACKED, FM6_INIT, FM6_PACKED);
        }
    }
}

static void fm6_bank_scan(void)                  /* which slots are used, from flash */
{
    uint32_t h;
    fm6_bank_used = 0;
    for (h = 0; h < 2u; h++) {
        const fm6_half_t *b = fm6_half_view(h);
        if (b)
            fm6_bank_used |= b->used << (h * FM6_HALF);
    }
    fm6_bank_read = fm6_bank_get;
}

static void fm6_bank_boot(void) { fm6_bank_scan(); }   /* power-on, after persist_boot */

/* tracks whose PTCH loaded a slot of the mask get it again (fm6_poll), as a slot's new patch */
static void fm6_bank_reload(uint32_t mask)
{
    uint32_t t;
    for (t = 0; t < NTRK; t++)
        if (fm6_slot[t] >= FM6_NFACTORY && fm6_slot[t] < FM6_NSLOT && ((mask >> (fm6_slot[t] - FM6_NFACTORY)) & 1u))
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
    uint32_t m = 0xFFFFu << (h * FM6_HALF);
    if (st_save(OBJ_FM6BANK0 + h, w, sizeof *w))
        return 2;
    fm6_bank_used = (fm6_bank_used & ~m) | (w->used << (h * FM6_HALF));
    fm6_bank_reload(m);
    return 0;
}

static int fm6_bank_busy(void)                   /* a flash erase stops the audio a moment: not while playing */
{
    if (!flash_ok)
        return 1;
    if (song.playing || transport_req) {
        ui_message("STOP TO SAVE");
        return 1;
    }
    return 0;
}

/* slot k = the packed record pk (0: erase it), then that half to flash: 0 ok, 1 bad slot, 2 flash error, no
 * flash or the song plays (nothing changed) */
static int fm6_bank_put(uint32_t k, const uint8_t *pk)
{
    fm6_half_t *w;
    if (k >= FM6_BANK_N)
        return 1;
    if (fm6_bank_busy())
        return 2;
    w = fm6_half_edit(k / FM6_HALF);
    if (pk)
        fm6_half_set(w, k % FM6_HALF, pk);
    else {
        memset(w->v[k % FM6_HALF], 0, FM6_PACKED);
        w->used &= ~(1u << (k % FM6_HALF));
    }
    return fm6_half_commit(k / FM6_HALF, w);
}

/* the whole bank = 32 packed records (a 32-voice SysEx bank's 4096 bytes, 7-bit), every slot used: 0 ok, 2 flash
 * error, no flash or the song plays. Each half commits by itself */
static int fm6_bank_put_all(const uint8_t *rec)
{
    uint32_t h, i;
    if (fm6_bank_busy())
        return 2;
    for (h = 0; h < 2u; h++) {
        fm6_half_t *w = fm6_half_edit(h);
        for (i = 0; i < FM6_HALF; i++)
            fm6_half_set(w, i, rec + (h * FM6_HALF + i) * FM6_PACKED);
        if (fm6_half_commit(h, w))
            return 2;
    }
    return 0;
}
