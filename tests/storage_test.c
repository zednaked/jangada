/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of firmware/src/storage.c against a simulated NOR flash:
 * erase -> 0xFF, program can only clear bits, page writes must not wrap. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint8_t nor[0x100000];
static int fail_after = -1;            /* torn-write injection: stop after N programs */

static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    if (fail_after == 0)
        return -9;
    if (fail_after > 0)
        fail_after--;
    if ((off & 0xFFu) + n > 256u) {
        printf("page wrap at %#x\n", off);
        exit(1);
    }
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

int main(void)
{
    char a[600], b[600], got[600];
    int bad = 0, n;
    memset(nor, 0xFF, sizeof nor);
    memset(a, 'A', sizeof a);
    memset(b, 'B', sizeof b);
    bad += check("empty flash loads nothing", st_load(OBJ_PROJECT0, got, sizeof got) < 0);
    bad += check("save A", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save B", st_save(OBJ_PROJECT0, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns B (newer seq)", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 1;                                  /* payload page 1 written, the rest torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("torn save keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    fail_after = 3;                                  /* all payload pages, header torn */
    st_save(OBJ_PROJECT0, a, sizeof a);
    fail_after = -1;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("header not written keeps B", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    bad += check("save A again", st_save(OBJ_PROJECT0, a, sizeof a) == 0);
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("load returns A", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    nor[st_sector(OBJ_PROJECT0, 0) + ST_PAYLOAD_OFF + 10] ^= 0x01;   /* bit rot in one copy */
    nor[st_sector(OBJ_PROJECT0, 1) + ST_PAYLOAD_OFF + 10] ^= 0x01;
    n = st_load(OBJ_PROJECT0, got, sizeof got);
    bad += check("both copies corrupt -> nothing", n < 0);
    bad += check("other objects untouched", st_load(OBJ_PROJECT0 + 1, got, sizeof got) < 0);
    bad += check("settings save/load",
                 st_save(OBJ_SETTINGS, "hello", 5) == 0 && st_load(OBJ_SETTINGS, got, 5) == 5 && !memcmp(got, "hello", 5));
    bad += check("CRC-32 is zlib's (check value 0xCBF43926)", st_crc32("123456789", 9) == 0xCBF43926u);
    memset(nor, 0xFF, sizeof nor);
    st_save(OBJ_PROJECT0 + 2, a, sizeof a);
    st_save(OBJ_PROJECT0 + 2, b, sizeof b);              /* B is newer, in the other copy */
    {
        st_hdr_t h;
        int cur = st_current(OBJ_PROJECT0 + 2, &h);
        nor[st_sector(OBJ_PROJECT0 + 2, (uint32_t)cur) + ST_PAYLOAD_OFF + 300] ^= 0x10;   /* rot in the newer copy */
    }
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("newer copy rotten -> the older one loads", n == (int)sizeof a && !memcmp(got, a, sizeof a));
    bad += check("save over the rotten copy", st_save(OBJ_PROJECT0 + 2, b, sizeof b) == 0);
    n = st_load(OBJ_PROJECT0 + 2, got, sizeof got);
    bad += check("... loads the new data", n == (int)sizeof b && !memcmp(got, b, sizeof b));
    nor[st_sector(OBJ_PROJECT0 + 2, 0) + 8] ^= 0x01;    /* both headers broken */
    nor[st_sector(OBJ_PROJECT0 + 2, 1) + 8] ^= 0x01;
    bad += check("both headers broken -> nothing", st_load(OBJ_PROJECT0 + 2, got, sizeof got) < 0);
    bad += check("data stays in the Felucca regions",
                 st_sector(OBJ_SETTINGS, 1) + 4096 <= 0xFF000 && st_sector(OBJ_PROJECT0 + 3, 1) + 4096 <= 0xE0000 &&
                     st_sector(OBJ_UPRESET0, 0) >= 0xDC000 && st_sector(OBJ_UPRESET0 + 1, 1) + 4096 <= 0xE0000 &&
                     st_sector(OBJ_AUTOSAVE, 0) == 0x9F000 && st_sector(OBJ_AUTOSAVE, 1) == 0xFE000);
    {   /* Jangada (after SLOOP 2.3): a record copied whole into the other sector (its slot says where it
         * was written) or into another object's sector is not taken */
        st_hdr_t h;
        int cur;
        uint32_t from, to;
        memset(nor, 0xFF, sizeof nor);
        st_save(OBJ_PROJECT0 + 1, a, sizeof a);
        cur = st_current(OBJ_PROJECT0 + 1, &h);
        from = st_sector(OBJ_PROJECT0 + 1, (uint32_t)cur);
        to = st_sector(OBJ_PROJECT0 + 1, cur ? 0u : 1u);
        memcpy(nor + to, nor + from, 4096);
        memset(nor + from, 0xFF, 4096);
        bad += check("a copy moved to the other sector -> nothing", st_load(OBJ_PROJECT0 + 1, got, sizeof got) < 0);
        memcpy(nor + st_sector(OBJ_PROJECT0, (uint32_t)cur), nor + to, 4096);
        bad += check("another object's record -> nothing", st_load(OBJ_PROJECT0, got, sizeof got) < 0);
        bad += check("objects out of range: no load, no save",
                     st_load(OBJ_COUNT, got, sizeof got) < 0 && st_save(OBJ_COUNT, a, 4) < 0);
    }
    {   /* Jangada: the flash map. Every sector of every object (A and B) and the other users of the flash
         * (the user sample slots, the update staging) are disjoint, 4 KiB aligned, inside the regions the
         * firmware may write (hal/fm1_flash.h FL_STORE_OK: FL_DATA, FL_GLOB, FL_FM6, FL_FM6B), never the app
         * (< 0x93000), the official firmware's BTIF / USR sectors (0xE9000..0xFBFFF) or key_mac (0xFF000) */
        static const uint32_t REG[4][2] = {{0x97000u, 0xE0000u}, {0xFC000u, 0xFF000u}, {0xE5000u, 0xE9000u},
                                           {0x93000u, 0x97000u}};
        uint32_t lo[2 * OBJ_COUNT + 2], hi[2 * OBJ_COUNT + 2], nr = 0, i, j, o, c, inreg = 1, apart = 1, al = 1;
        for (o = 0; o < OBJ_COUNT; o++)
            for (c = 0; c < 2u; c++) {
                lo[nr] = st_sector(o, c);
                hi[nr] = lo[nr] + ST_SECTOR;
                nr++;
            }
        lo[nr] = 0xA0000u, hi[nr++] = 0xDC000u;          /* eng_sample.c: USR1..3, 3 x 80 KiB */
        lo[nr] = 0xE0000u, hi[nr++] = 0xE5000u;          /* ota.c: the update loader's staging */
        for (i = 0; i < nr; i++) {
            int in = 0;
            for (j = 0; j < 4u; j++)
                in |= lo[i] >= REG[j][0] && hi[i] <= REG[j][1];
            inreg &= in || i == nr - 1u;                 /* (the staging is FL_OTA, its own window) */
            al &= !(lo[i] & 0xFFFu) && !(hi[i] & 0xFFFu);
            for (j = 0; j < i; j++)
                apart &= hi[i] <= lo[j] || hi[j] <= lo[i];
        }
        bad += check("flash map: every object A/B sector apart from the others", apart);
        bad += check("flash map: 4 KiB sectors inside FL_DATA / FL_GLOB / FL_FM6 / FL_FM6B", inreg && al);
        bad += check("flash map: the FM6 bank 1 at 0xE5000..0xE8FFF (B1..B16, B17..B32)",
                     st_sector(OBJ_FM6BANK0, 0) == 0xE5000u && st_sector(OBJ_FM6BANK0, 1) == 0xE6000u &&
                         st_sector(OBJ_FM6BANK0 + 1, 0) == 0xE7000u && st_sector(OBJ_FM6BANK0 + 1, 1) == 0xE8000u);
        bad += check("flash map: the FM6 bank 2 at 0x93000..0x96FFF (B33..B48, B49..B64), ids appended",
                     st_sector(OBJ_FM6BANK2, 0) == 0x93000u && st_sector(OBJ_FM6BANK2, 1) == 0x94000u &&
                         st_sector(OBJ_FM6BANK2 + 1, 0) == 0x95000u && st_sector(OBJ_FM6BANK2 + 1, 1) == 0x96000u &&
                         OBJ_FM6BANK0 == 8 && OBJ_FM6BANK2 == 10 && OBJ_COUNT == OBJ_FM6BANK2 + 2);
    }
    {   /* st_view: the current copy in place */
        uint32_t len = 0;
        const uint8_t *p;
        memset(nor, 0xFF, sizeof nor);
        bad += check("st_view of an empty object -> nothing", st_view(OBJ_FM6BANK0 + 1, &len) == 0);
        st_save(OBJ_FM6BANK0 + 1, a, sizeof a);
        st_save(OBJ_FM6BANK0 + 1, b, 100);
        p = st_view(OBJ_FM6BANK0 + 1, &len);
        bad += check("st_view returns the newest payload", p && len == 100u && !memcmp(p, b, 100));
        bad += check("... and the other bank object stays empty", st_view(OBJ_FM6BANK0, &len) == 0);
    }
    printf("%s\n", bad ? "STORAGE TEST FAILED" : "storage test passed");
    return bad != 0;
}
