/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Jangada (after SLOOP 2.3 and Felucca 1.0.1): the editor's backup and restore, editor protocol v5,
 * cmds 34-36 (web/EDITOR_PROTOCOL.md). Requests name objects, never flash addresses:
 *   0      the working project (stored as the autosave stores it: "JNG2", project.c)
 *   1      the settings (persist_t "PER2": palette, low cut, the panel calibration, the lights word;
 *          a backup of Jangada 0.2, without the lights word, restores too)
 *   2..5   the projects 1..4 ("JNG2"; length 0 = empty)
 *   6..7   the user preset banks (up_bank_t "UPB2", upreset.c; length 0 = empty)
 *   8..9   the FM6 patch bank 1, B1..B16 and B17..B32 (fm6_half_t "FM6B", fm6_bank.c; length 0 = empty;
 *          after Felucca 1.0's id 8. A backup without them, Jangada 0.3 / 0.4's, restores and keeps the bank)
 *   10..11 (v7, Jangada 0.6) the FM6 patch bank 2, B33..B48 and B49..B64 (the same layout, halves 2 and 3. A
 *          backup without them, Jangada 0.5's, restores and keeps bank 2 as it is)
 *   32..34 the user sample slots USR1..3 (header + ADPCM, as in flash; read only here: a restore
 *          writes them with SMP_BEGIN / SMP_WRITE / SMP_END, an empty one with SMP_ERASE)
 * LIST takes a snapshot of the working project; GET reads 1..256 bytes of an object; PUT stages one
 * object in RAM (begin: id, length, CRC-32; the data in order; commit), checks it at the commit as a
 * load checks it, then writes it through the usual A/B commit (storage.c st_save): a cut-off restore
 * never leaves half an object.
 *
 * RAM: the staging and snapshot buffer is project.c's proj_io (the main loop's stored-project buffer),
 * so this costs no new buffer. A project save or load from the panel meanwhile reuses it; the CRCs
 * catch that (GET: the editor checks every object against LIST; PUT: the commit checks the staged
 * bytes), the autosave waits while a backup runs (autosave_hold), and so does the FM6 bank (proj_io_bk:
 * a bank write, from the editor or a DX7 bank dump, is refused with rc 4 until 15 s after the last
 * request, or the commit / abort; its own staged half is dropped). Included by editor.c. */
#define ED_BK_RAW proj_io
#define ED_BK_HOLD 15000u                            /* ms: a backup session without a request ends */
static const uint8_t ED_BK_IDS[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 32, 33, 34};
#define ED_BK_N ((uint32_t)sizeof ED_BK_IDS)
#define ED_BK_NONE 0xFFu
#define ED_BK_STAGE 0xFEu
static uint8_t ed_bk_cur = ED_BK_NONE;              /* what proj_io holds for us: object 0..5, 8..11, staging, none */
static int ed_bk_made(uint32_t id) { return id <= 5u || (id >= 8u && id <= 11u); }   /* objects made into proj_io */
static uint8_t ed_bk_put, ed_bk_id;
static uint32_t ed_bk_curlen, ed_bk_len, ed_bk_crc, ed_bk_pos, ed_bk_ms, ed_bk_usb;

static void ed_bk_u32(uint32_t v)                   /* 5 x 7 bit, LSB first */
{
    uint32_t i;
    for (i = 0; i < 5u; i++)
        ed_b((v >> (7u * i)) & 127u);
}
static uint32_t ed_bk_r32(const uint8_t *a)
{
    return (uint32_t)a[0] | (uint32_t)a[1] << 7 | (uint32_t)a[2] << 14 | (uint32_t)a[3] << 21 | (uint32_t)a[4] << 28;
}
static void ed_bk_pack(const uint8_t *p, uint32_t n)   /* pack7: a top-bits byte, then up to 7 bytes */
{
    while (n) {
        uint32_t k = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < k; i++)
            m |= (uint32_t)(p[i] >> 7) << i;
        ed_b(m);
        for (i = 0; i < k; i++)
            ed_b(p[i] & 127u);
        p += k;
        n -= k;
    }
}
static void ed_bk_touch(void)                       /* a backup runs: the autosave and the FM6 bank keep off proj_io */
{
    ed_bk_ms = fm1_ms;
    autosave_hold = proj_io_bk = fm1_ms + ED_BK_HOLD;
}

static void ed_bk_settings(persist_t *p)            /* the settings as settings_save stores them */
{
    memset(p, 0, sizeof *p);
    p->magic = PERSIST_MAGIC;
    p->palette = settings.palette;
    p->lowcut = settings.lowcut;
    p->zoom = 0;                                    /* (reserved) */
    p->panel = panel;
    p->lights = lights_word();
}

/* object id -> its bytes (*len 0: empty); objects 1..5, 8..11 are made into proj_io. 0: no such object */
static const uint8_t *ed_bk_make(uint32_t id, uint32_t *len)
{
    *len = 0;
    fm6_stage_drop();                                /* (proj_io is the backup's now) */
    if (id == 1u) {
        ed_bk_settings((persist_t *)(void *)ED_BK_RAW);
        *len = sizeof(persist_t);
    } else if (id >= 2u && id <= 5u) {
        if (project_used(id - 2u))
            *len = proj_to_jng(&proj_slot[id - 2u], ED_BK_RAW);
    } else if (id >= 8u && id <= 11u) {                /* the FM6 bank halves 0..3 */
        const fm6_half_t *h = fm6_half_view(id - 8u);
        if (h) {
            memcpy(ED_BK_RAW, h, sizeof *h);
            *len = sizeof *h;
        }
    } else if (id == 6u || id == 7u) {
        if (up_bank[id - 6u].magic == UP_BANK_MAGIC)
            *len = sizeof up_bank[0];
        return (const uint8_t *)&up_bank[id - 6u];
    } else if (id >= 32u && id < 32u + SMP_USER_SLOTS) {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(id - 32u);
        if (usr_nz[id - 32u] && h->magic == SMP_USER_MAGIC && h->data_len <= SMP_USER_SIZE - SMP_USER_DATA)
            *len = SMP_USER_DATA + h->data_len;
        return smp_user_xip(id - 32u);
    } else {
        return 0;
    }
    ed_bk_cur = (uint8_t)id;
    ed_bk_curlen = *len;
    return ED_BK_RAW;
}

/* the staged object, checked, then written. rc 0 ok, 1 arguments, 2 not a valid object, 3 stop the
 * song first, 4 flash */
static uint32_t ed_bk_commit(void)
{
    uint8_t *raw = ED_BK_RAW;
    uint32_t id = ed_bk_id, n = ed_bk_len, i;
    if (ed_bk_pos != n || st_crc32(raw, n) != ed_bk_crc)
        return 2;
    if (song.playing || transport_req)
        return 3;                                    /* (a flash erase stops the audio for a moment) */
    if (id == 0u) {                                  /* the working project: loaded now */
        if (!proj_import(&autosave_buf, raw, (int)n))
            return 2;
        proj_apply(&autosave_buf);
        return 0;
    }
    if (id == 1u) {                                  /* as persist_boot reads it; 0.2's (no lights word) too */
        persist_t p;
        memset(&p, 0, sizeof p);
        memcpy(&p, raw, n <= sizeof p ? n : sizeof p);
        if ((n != sizeof p && n != PERSIST_SIZE_V02) || p.magic != PERSIST_MAGIC || p.palette >= NPALETTES ||
            p.lowcut > 1u || p.zoom > 1u || !panel_valid(&p.panel))
            return 2;
        if (st_save(OBJ_SETTINGS, &p, sizeof p))
            return 4;
        persist_saved = p;
        settings.magic = SETTINGS_MAGIC;
        settings.palette = p.palette;
        settings.lowcut = p.lowcut;                  /* (p.zoom: reserved, ignored) */
        panel = p.panel;
        lights_from_word(p.lights);
        palette_set(settings.palette);
        fx_lowcut = (uint8_t)(settings.lowcut != 0);
        return 0;
    }
    if (id <= 5u) {                                  /* a project: any stored format in, "JNG2" out */
        uint32_t k = id - 2u;
        if (n) {
            if (!proj_import(&autosave_buf, raw, (int)n))
                return 2;
            n = proj_to_jng(&autosave_buf, raw);
        }
        if (st_save(OBJ_PROJECT0 + k, raw, n))
            return 4;
        if (n)
            proj_slot[k] = autosave_buf;
        else
            memset(&proj_slot[k], 0, sizeof proj_slot[k]);
        return 0;
    }
    if (id >= 8u && id <= 11u) {                     /* an FM6 bank half, checked as a PTCH load checks it */
        uint32_t h = id - 8u;
        if (n && !fm6_half_valid(raw, n, h))
            return 2;
        if (n)
            return (uint32_t)fm6_half_commit(h, (const fm6_half_t *)(const void *)raw) ? 4u : 0u;
        return fm6_half_clear(h) ? 4u : 0u;
    }
    if (id <= 7u) {                                  /* a user preset bank, read as up_boot reads it */
        uint32_t b = id - 6u;
        const up_bank_t *bk = (const up_bank_t *)(const void *)raw;
        if (n && (n != sizeof *bk || bk->magic != UP_BANK_MAGIC || bk->rsize != sizeof(up_rec_t) ||
                  bk->nslot != UP_PER_BANK || !bk->np || bk->np > UP_PMAX))
            return 2;
        if (st_save(OBJ_UPRESET0 + b, raw, n))
            return 4;
        memset(&up_bank[b], 0, sizeof up_bank[b]);
        if (n)
            memcpy(&up_bank[b], raw, n);
        up_bank_check(b, (int)n);
        for (i = 0; i < NTRK; i++)                   /* (a track showing a user preset of this bank) */
            if (trk[i].user && (trk[i].user - 1u) / UP_PER_BANK == b)
                trk[i].user = 0;
        up_gen++;
        return 0;
    }
    return 1;
}

/* PUT: op 0 begin (id, length, CRC), 1 data (id, offset, pack7), 2 commit (id), 3 abort (id) -> rc */
static uint32_t ed_bk_write(const uint8_t *a, uint32_t na)
{
    uint32_t op = a[0], id = a[1], len, k, rc;
    if (op == 0u) {
        if (na != 12u || id > 11u)
            return 1;
        len = ed_bk_r32(a + 2);
        if (len > ST_PAYLOAD_MAX || (id <= 1u && !len) || (id == 1u && len != sizeof(persist_t) && len != PERSIST_SIZE_V02) ||
            ((id == 6u || id == 7u) && len && len != sizeof(up_bank_t)) || (id >= 8u && len && len != sizeof(fm6_half_t)))
            return 1;
        ed_bk_put = 1;
        ed_bk_cur = ED_BK_STAGE;                     /* (the snapshot of LIST is gone) */
        fm6_stage_drop();                            /* (and a staged FM6 bank half: proj_io is ours) */
        ed_bk_id = (uint8_t)id;
        ed_bk_len = len;
        ed_bk_crc = ed_bk_r32(a + 7);
        ed_bk_pos = 0;
        ed_bk_usb = usb.resets;
        ed_bk_touch();
        return 0;
    }
    if (!ed_bk_put || ed_bk_id != id || ed_bk_usb != usb.resets || fm1_ms - ed_bk_ms > ED_BK_HOLD) {
        ed_bk_put = 0;
        return 5;                                    /* no begin for this object (or too long ago) */
    }
    ed_bk_touch();
    if (op == 3u) {
        ed_bk_put = 0;
        proj_io_bk = 0;                              /* (the bank may write again) */
        return na == 2u ? 0u : 1u;
    }
    if (op == 2u) {
        if (na != 2u)
            return 1;
        ed_bk_put = 0;
        ed_bk_cur = ED_BK_NONE;
        proj_io_bk = 0;
        rc = ed_bk_commit();
        if (!rc) {
            sync_reload = 1;
            ui.force = 1;
        }
        return rc;
    }
    if (op != 1u || na < 9u || ed_bk_r32(a + 2) != ed_bk_pos)
        return 1;
    k = ed_unpack7(a + 7, na - 7u, ed_smp_buf, 256u);
    if (!k || k > ed_bk_len - ed_bk_pos)
        return 1;
    memcpy(ED_BK_RAW + ed_bk_pos, ed_smp_buf, k);
    ed_bk_pos += k;
    return 0;
}

/* 1: a backup command (the reply is built) */
static int ed_backup(uint32_t cmd, const uint8_t *a, uint32_t na)
{
    uint32_t i, len, rc;
    const uint8_t *p;
    if (cmd == ED_BK_LIST) {                         /* -> rc, count, per object: id, length, CRC-32 */
        uint32_t L[ED_BK_N], C[ED_BK_N];
        rc = !flash_ok ? 4u : 0u;
        for (i = 1; !rc && i < ED_BK_N; i++) {       /* the stored objects first, */
            p = ed_bk_make(ED_BK_IDS[i], &L[i]);
            C[i] = st_crc32(p, L[i]);
            fm1_wdt_feed();                          /* (a sample slot: up to 80 KiB through the CRC) */
        }
        if (!rc) {                                   /* then the working project: it stays in proj_io */
            proj_capture(&autosave_buf);
            L[0] = proj_to_jng(&autosave_buf, ED_BK_RAW);
            C[0] = st_crc32(ED_BK_RAW, L[0]);
            ed_bk_cur = 0;
            ed_bk_curlen = L[0];
            ed_bk_put = 0;
            ed_bk_touch();
        }
        ed_b(rc);
        ed_b(rc ? 0u : ED_BK_N);
        for (i = 0; !rc && i < ED_BK_N; i++) {
            ed_b(ED_BK_IDS[i]);
            ed_bk_u32(L[i]);
            ed_bk_u32(C[i]);
        }
        return 1;
    }
    if (cmd == ED_BK_GET) {                          /* id, offset, count (2 x 7 bit) -> id, rc, offset, count, data */
        uint32_t off = na == 8u ? ed_bk_r32(a + 1) : 0u, count = na == 8u ? (uint32_t)a[6] | (uint32_t)a[7] << 7 : 0u;
        p = 0;
        len = 0;
        if (na != 8u || !flash_ok)
            rc = 1;
        else if (a[0] == 0u) {                       /* the snapshot: only while proj_io still holds it */
            rc = ed_bk_cur == 0u ? 0u : 5u;
            p = ED_BK_RAW;
            len = ed_bk_curlen;
        } else if (ed_bk_made(a[0]) && ed_bk_cur == a[0] && off) {
            rc = 0;                                  /* (made by the GET at offset 0) */
            p = ED_BK_RAW;
            len = ed_bk_curlen;
        } else {
            rc = ed_bk_put ? 5u : 0u;                /* (a restore stages in proj_io) */
            if (!rc)
                p = ed_bk_make(a[0], &len);
        }
        if (!rc && (!p || !count || count > 256u || off > len || count > len - off))
            rc = 1;
        if (!rc)
            ed_bk_touch();
        ed_b(na ? a[0] : 127u);
        ed_b(rc);
        ed_bk_u32(off);
        ed_b(rc ? 0u : count & 127u);
        ed_b(rc ? 0u : count >> 7);
        if (!rc)
            ed_bk_pack(p + off, count);
        return 1;
    }
    if (cmd == ED_BK_PUT) {                          /* op, id, ... -> op, id, rc */
        rc = na < 2u ? 1u : !flash_ok ? 4u : ed_bk_write(a, na);
        ed_b(na ? a[0] : 127u);
        ed_b(na >= 2u ? a[1] : 127u);
        ed_b(rc);
        return 1;
    }
    return 0;
}
