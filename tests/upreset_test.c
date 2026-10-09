/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the user preset record (firmware/src/upreset.c, -DUP_HOST part):
 * UP_PUT parsing, a bank round trip through storage.c on a simulated NOR,
 * bank / record version checks, map-by-count, pattern <-> steps. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define __attribute__(x)
#define UP_HOST 1
#include "../firmware/src/core.h"

static uint8_t nor[0x100000];
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    for (i = 0; i < n; i++)
        nor[off + i] &= s[i];
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/upreset.c"

static int load_bank(uint32_t b)                 /* as up_boot: the stored bytes in place */
{
    uint32_t n = 0;
    const uint8_t *raw = st_view(OBJ_UPRESET0 + b, &n);
    up_bank_load(b, raw, raw ? (int)n : -1);
    return raw ? (int)n : -1;
}

static int check(const char *what, int ok)
{
    printf("%-46s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

static uint32_t put_frame(uint8_t *a, uint32_t slot, uint32_t eng, const char *name, int32_t base)
{
    uint32_t n = 0, i;
    a[n++] = (uint8_t)slot;
    a[n++] = (uint8_t)eng;
    for (i = 0; name[i]; i++)
        a[n++] = (uint8_t)name[i];
    a[n++] = 0;
    for (i = 0; i < P_COUNT; i++) {
        uint32_t u = (uint32_t)(base + (int32_t)i + 8192);
        a[n++] = u & 127u;
        a[n++] = (u >> 7) & 127u;
    }
    for (i = 0; i < 16u; i++) {
        a[n++] = (uint8_t)(i % 3u ? 40u + i : 0u);  /* rests on 0, 3, 6 .. */
        a[n++] = (uint8_t)(i == 3u ? 4u : i % 3u ? 1u : 2u);   /* step 3: tie; slide on a rest drops */
    }
    return n;
}

int main(void)
{
    uint8_t a[640];
    up_rec_t r, got;
    uint32_t n, slot = 99, i, k;
    int bad = 0, len, ok;
    int16_t v[P_COUNT], def[P_COUNT];
    memset(nor, 0xFF, sizeof nor);

    n = put_frame(a, 5, 2, "Bass One", -40);
    bad += check("UP_PUT frame < 640 bytes", 5u + n + 1u < 640u);
    bad += check("UP_PUT parses", up_parse(a, n, &r, &slot) == 0 && slot == 5u && r.engine == 2u &&
                                      up_valid(&r) && !memcmp(r.name, "Bass One", 8) && !r.name[8]);
    {   /* Jangada (after SLOOP 2.3): a damaged name makes the record unreadable */
        up_rec_t d = r;
        d.name[3] = 7;
        ok = !up_valid(&d);
        d = r;
        d.name[10] = 'X';                            /* text after the end (8 letters, then 0s) */
        ok &= !up_valid(&d);
        d = r;
        d.engine = NENGINES;
        ok &= !up_valid(&d);
        bad += check("damaged record (name, text after its end, engine) -> invalid", ok);
    }
    ok = 1;
    for (i = 0; i < P_COUNT; i++)
        ok &= r.p[i] == (int16_t)(-40 + (int32_t)i);
    bad += check("UP_PUT values (negative v14 too)", ok);
    bad += check("pattern: rest drops flags, tie has no note",
                 r.note[0] == 0 && r.flags[0] == 0 && r.note[1] == 41 && r.flags[1] == 1 && r.note[3] == 0 &&
                     r.flags[3] == 4);
    bad += check("UP_PUT short frame -> args", up_parse(a, n - 1u, &r, &slot) == 1);
    n = put_frame(a, 32, 0, "X", 0);
    bad += check("UP_PUT slot 32 -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, NENGINES, "X", 0);
    bad += check("UP_PUT bad engine -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, 0, "", 0);
    bad += check("UP_PUT empty name -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, 0, "THIRTEEN CHRS", 0);
    bad += check("UP_PUT 13-char name -> args", up_parse(a, n, &r, &slot) == 1);
    n = put_frame(a, 0, 0, "TWELVE CHARS", 0);
    bad += check("UP_PUT 12-char name ok", up_parse(a, n, &r, &slot) == 0 && !memcmp(r.name, "TWELVE CHARS", 12));
    {
        char nm[13];
        *up_rec(31) = r;
        memcpy(up_rec(31)->name, "Low case", 9);
        up_name(31, nm);
        bad += check("name shown upper case", !strcmp(nm, "LOW CASE"));
    }

    /* bank round trip through storage.c */
    n = put_frame(a, 17, 3, "Keys", 7);
    up_parse(a, n, &r, &slot);
    up_bank_fresh(&up_bank[1]);
    *up_rec(17) = r;
    bad += check("bank fits one object", sizeof(up_bank_t) <= ST_PAYLOAD_MAX);
    bad += check("bank save", st_save(OBJ_UPRESET0 + 1, &up_bank[1], sizeof up_bank[1]) == 0);
    memset(up_bank, 0, sizeof up_bank);
    len = load_bank(1);
    got = *up_rec(17);
    bad += check("bank load: the record is back", up_used(17) && !memcmp(&got, &r, sizeof r));
    bad += check("other slots empty", !up_used(16) && !up_used(18) && !up_used(0));
    len = load_bank(0);
    bad += check("bank 0 never written -> empty", len < 0 && !up_used(0) && up_bank[0].magic == 0);
    bad += check("banks in 0xDC000..0xDFFFF", st_sector(OBJ_UPRESET0, 0) == 0xDC000u &&
                                                   st_sector(OBJ_UPRESET0 + 1, 1) == 0xDF000u &&
                                                   st_sector(OBJ_PROJECT0 + 3, 1) + 4096u <= 0xA0000u);
    {
        static up_bank_t c;
        c = up_bank[1];
        c.rsize = 190;                                      /* another record layout */
        up_bank_load(1, &c, (int)sizeof c);
        bad += check("bank with another record size -> empty, kept (foreign)", !up_used(17) && up_foreign[1]);
    }
    up_bank_fresh(&up_bank[1]);
    *up_rec(17) = r;
    up_rec(17)->ver = UP_VER + 1u;
    bad += check("record with another version -> empty", !up_used(17));

    for (i = 0; i < P_COUNT; i++)
        def[i] = (int16_t)(1000 + i);
    {   /* Felucca banks ("UPB1"): converted in place when loaded */
        static up_bank_v1_t f;
        static const uint32_t NP[2] = {57, 53};    /* Felucca 0.9 (SLICER), and before it */
        uint32_t t;
        for (t = 0; t < 2u; t++) {
            uint32_t np = NP[t], nc = np - 8u;
            memset(&f, 0, sizeof f);
            f.magic = UP_V1_MAGIC;
            f.rsize = sizeof(up_rec_v1_t);
            f.nslot = UP_PER_BANK;
            for (k = 0; k < UP_PER_BANK; k += 5u) {   /* slots 0, 5, 10, 15 */
                up_rec_v1_t *o = &f.r[k];
                o->used = UP_USED;
                o->ver = 1;
                o->engine = (uint8_t)(k % NENGINES);
                o->np = (uint8_t)np;
                memcpy(o->name, "OLD", 3);
                for (i = 0; i < np; i++)
                    o->p[i] = (int16_t)(k + i - 60);
                o->note[0] = 60;
                o->flags[0] = SF_ACCENT;
            }
            memset(up_bank, 0, sizeof up_bank);
            st_save(OBJ_UPRESET0, &f, sizeof f);
            load_bank(0);
            ok = up_bank[0].magic == UP_BANK_MAGIC && up_bank[0].np == P_COUNT;
            for (k = 0; k < UP_PER_BANK; k++) {
                if (k % 5u) {
                    ok &= !up_used(k);
                    continue;
                }
                ok &= up_used(k) && up_rec(k)->engine == k % NENGINES && !memcmp(up_rec(k)->name, "OLD", 4) &&
                      up_rec(k)->note[0] == 60 && up_rec(k)->flags[0] == SF_ACCENT;
                up_params(up_rec(k), v, def);
                for (i = 0; i < nc; i++)                 /* P_LEVEL.. in order */
                    ok &= v[i] == (int16_t)(k + i - 60);
                for (i = nc; i < P_E0; i++)              /* added since (SLICER, the matrix): defaults */
                    ok &= v[i] == def[i];
                for (i = 0; i < 8u; i++)                 /* the engine's 8 */
                    ok &= v[P_E0 + i] == (int16_t)(k + nc + i - 60);
                for (i = 8u; i < NEDIT; i++)             /* E9..: defaults */
                    ok &= v[P_E0 + i] == def[P_E0 + i];
            }
            bad += check(t ? "Felucca bank (np 53): converted, SLICER defaults" : "Felucca bank (np 57): converted", ok);
        }
    }
    {   /* a bank stored with other keys (a later build): mapped by key */
        memset(up_bank, 0, sizeof up_bank);
        up_bank_fresh(&up_bank[0]);
        up_bank[0].np = 3;
        up_bank[0].key[0] = P_KEY[P_E0];
        up_bank[0].key[1] = P_KEY[P_LEVEL];
        up_bank[0].key[2] = 120;                         /* a key this build does not know */
        up_rec(2)->used = UP_USED;
        up_rec(2)->ver = UP_VER;
        up_rec(2)->engine = 1;
        up_rec(2)->np = 3;
        memcpy(up_rec(2)->name, "NEW", 3);
        up_rec(2)->p[0] = 7;
        up_rec(2)->p[1] = 8;
        up_rec(2)->p[2] = 9;
        {
            static up_bank_t c;
            c = up_bank[0];
            up_bank_load(0, &c, (int)sizeof c);
        }
        up_params(up_rec(2), v, def);
        ok = up_used(2) && up_bank[0].np == P_COUNT && v[P_E0] == 7 && v[P_LEVEL] == 8;
        for (i = 0; i < P_COUNT; i++)
            if (i != P_E0 && i != P_LEVEL)
                ok &= v[i] == def[i];
        bad += check("other keys: mapped by key, unknown skipped", ok);
    }
    {   /* Jangada 0.8.2's bank ("UPB2", two bytes a value, keyed): converted when loaded, from flash as it is */
        static up_bank_v2_t o;
        memset(&o, 0, sizeof o);
        o.magic = UP_V2_MAGIC;
        o.rsize = sizeof(up_rec_v2_t);
        o.nslot = UP_PER_BANK;
        o.np = 86;                                       /* 0.8.2: P_LEVEL .. P_VLEAD, keys in that order */
        for (i = 0; i < 86u; i++)
            o.key[i] = P_KEY[i];
        o.r[4].used = UP_USED;
        o.r[4].ver = 2;
        o.r[4].engine = 2;
        o.r[4].np = 86;
        memcpy(o.r[4].name, "RUST BASS", 9);
        for (i = 0; i < 86u; i++)
            o.r[4].p[i] = (int16_t)((int32_t)i - 43);
        o.r[4].p[3] = UP_V2_DEF;                         /* not stored there: the default */
        o.r[4].p[5] = 300;                               /* out of a byte: kept at its end */
        o.r[4].note[0] = 48;
        memset(up_bank, 0, sizeof up_bank);
        bad += check("UPB2 is a bank this build reads", st_save(OBJ_UPRESET0, &o, sizeof o) == 0 &&
                                                            up_bank_kind(&o, (int)sizeof o) == 2);
        load_bank(0);
        up_params(up_rec(4), v, def);
        ok = up_used(4) && up_bank[0].magic == UP_BANK_MAGIC && !memcmp(up_rec(4)->name, "RUST BASS", 10) &&
             up_rec(4)->engine == 2 && up_rec(4)->note[0] == 48 && v[3] == def[3] && v[5] == 127;
        for (i = 0; i < 86u; i++)
            if (i != 3u && i != 5u)
                ok &= v[i] == (int16_t)((int32_t)i - 43);
        for (i = 86; i < P_COUNT; i++)                   /* added since (the INSERT): defaults */
            ok &= v[i] == def[i];
        for (k = 0; k < UP_PER_BANK; k++)
            ok &= k == 4u || !up_used(k);
        bad += check("UPB2 (Jangada 0.8.2): converted, every value, new ones default", ok);
    }
    r.np = P_COUNT;
    for (i = 0; i < P_COUNT; i++)
        r.p[i] = (int8_t)i;
    up_params(&r, v, def);
    ok = 1;
    for (i = 0; i < P_COUNT; i++)
        ok &= v[i] == (int16_t)i;
    bad += check("np == P_COUNT: as stored", ok);

    {   /* steps -> pattern (UP_STORE) */
        step_t st[NSTEP];
        memset(st, 0, sizeof st);
        for (i = 0; i < 16u; i++)
            st[i].time = ST_REST;
        st[0] = (step_t){{60, 64, 67, 0}, 3, ST_NOTE, SF_ACCENT | SF_SLIDE, 100};
        st[1] = (step_t){{0}, 0, ST_TIE, 0, 0};
        st[2] = (step_t){{50}, 0, ST_NOTE, SF_ACCENT, 0};   /* n = 0: empty */
        st[20] = (step_t){{70}, 1, ST_NOTE, 0, 90};         /* beyond 16: not stored */
        up_pat_from(&r, st);
        bad += check("steps -> pattern", r.note[0] == 60 && r.flags[0] == 3 && r.note[1] == 0 && r.flags[1] == 4 &&
                                             r.note[2] == 0 && r.flags[2] == 0 && !up_pat_empty(&r));
        memset(st, 0, sizeof st);
        up_pat_from(&r, st);
        bad += check("empty sequencer -> empty pattern", up_pat_empty(&r));
    }
    printf("%s\n", bad ? "USER PRESET TEST FAILED" : "user preset test passed");
    return bad != 0;
}
