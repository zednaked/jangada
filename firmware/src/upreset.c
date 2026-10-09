/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 32
 * slots in two storage.c objects (OBJ_UPRESET0/1, 0xDC000..0xDFFFF), 16
 * records each, mirrored in RAM so browsing never reads flash. A record:
 * engine, name, the instrument parameters, a 16-step pattern.
 *
 * Jangada: a bank ("UPB3", Jangada 0.9) stores the stable keys of its values (keys.h) once, and
 * each record its values in that order, one signed byte each (every track parameter is
 * -128..127, project.c JNG2), so parameters can be added or moved. In RAM every record is in
 * today's P_* order (np = P_COUNT); values the stored data did not have are UP_DEF until used
 * (up_values: the default for the record's engine). Older banks are converted when loaded
 * (up_bank_load): "UPB2" (Jangada 0.1 .. 0.8.2, two bytes a value, room for 88: full at 0.9)
 * and Felucca's "UPB1" (192-byte records mapped by count: the last 8 values P_E0..P_E7, the
 * first np - 8 P_LEVEL.. in order). Any other shape reads as empty.
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h. */
#define UP_PER_BANK 16u
#define UP_PMAX 128u                             /* KEY_MAX: room for every key */
#define UP_USED 0xA5u
#define UP_VER 3u
#define UP_BANK_MAGIC 0x33425055u                /* "UPB3" */
#define UP_DEF ((int8_t)-128)                    /* not stored: the engine's / TP default (a value is -127..127) */
typedef struct {
    uint8_t used, ver, engine, np;               /* UP_USED, UP_VER, engine, values stored */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    int8_t p[UP_PMAX];                           /* in the bank's key order (RAM: P_* order) */
    uint8_t note[16], flags[16];                 /* note 0 = rest; flags 1 accent, 2 slide, 4 tie */
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    uint8_t np, rsv[3];                          /* keys in use */
    uint8_t key[UP_PMAX];                        /* the stable key of each value of a record */
    up_rec_t r[UP_PER_BANK];
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 176, "user preset record layout");
_Static_assert(P_COUNT <= UP_PMAX && P_COUNT < KEY_MAX, "user preset record: P_COUNT");
static int8_t up_v8(int32_t v) { return (int8_t)(v < -127 ? -127 : v > 127 ? 127 : v); }   /* a value into a record (never UP_DEF) */

/* Jangada 0.1 .. 0.8.2's bank ("UPB2"), read and converted (up_bank_load) */
#define UP_V2_MAGIC 0x32425055u                  /* "UPB2" */
#define UP_V2_PMAX 88u
#define UP_V2_DEF ((int16_t)-32768)
typedef struct {
    uint8_t used, ver, engine, np;
    char name[12];
    int16_t p[UP_V2_PMAX];
    uint8_t note[16], flags[16];
} up_rec_v2_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    uint8_t np, rsv[3];
    uint8_t key[UP_V2_PMAX];
    up_rec_v2_t r[UP_PER_BANK];
} up_bank_v2_t;
_Static_assert(sizeof(up_rec_v2_t) == 224 && sizeof(up_bank_v2_t) == 3684, "Jangada 0.8.2's user preset bank");

/* Felucca's bank, read and converted (up_bank_load) */
#define UP_V1_MAGIC 0x31425055u                  /* "UPB1" */
#define UP_V1_PMAX 72u
typedef struct {
    uint8_t used, ver, engine, np;
    char name[12];
    int16_t p[UP_V1_PMAX];
    uint8_t note[16], flags[16];
} up_rec_v1_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec_v1_t r[UP_PER_BANK];
} up_bank_v1_t;
_Static_assert(sizeof(up_rec_v1_t) == 192 && sizeof(up_bank_v1_t) == 3080, "Felucca's user preset bank");
static up_bank_t up_bank[UP_SLOTS / UP_PER_BANK];
static uint8_t up_foreign[UP_SLOTS / UP_PER_BANK];   /* Jangada: stored, but not readable here: kept, not overwritten */

static up_rec_t *up_rec(uint32_t k) { return &up_bank[k / UP_PER_BANK].r[k % UP_PER_BANK]; }

/* a record to use: its name printable ASCII, then only 0s (Jangada, after SLOOP 2.3: a damaged name
 * reached the screen and the editor as it was) */
static int up_valid(const up_rec_t *r)
{
    uint32_t i, end = 0;
    if (r->used != UP_USED || r->ver != UP_VER || r->engine >= NENGINES || r->np != P_COUNT || !r->name[0])
        return 0;
    for (i = 0; i < sizeof r->name; i++) {
        uint32_t c = (uint8_t)r->name[i];
        if (!c)
            end = 1;
        else if (end || c < 32u || c > 126u)
            return 0;
    }
    return 1;
}

static int up_used(uint32_t k) { return k < UP_SLOTS && up_valid(up_rec(k)); }

static void up_bank_fresh(up_bank_t *bk)        /* the header of a bank in today's layout */
{
    uint32_t i;
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
    bk->np = P_COUNT;
    bk->rsv[0] = bk->rsv[1] = bk->rsv[2] = 0;
    memset(bk->key, KEY_NONE, sizeof bk->key);
    for (i = 0; i < P_COUNT; i++)
        bk->key[i] = P_KEY[i];
}

/* a Felucca record (mapped by count, see the top) -> today's */
static void up_from_v1(up_rec_t *d, const up_rec_v1_t *c)
{
    uint32_t i, nc;
    memset(d, 0, sizeof *d);
    if (c->used != UP_USED || c->ver != 1u || c->np < 8u || c->np > UP_V1_PMAX || !c->name[0])
        return;                                  /* empty or not readable: an empty slot */
    nc = c->np - 8u;
    d->used = UP_USED;
    d->ver = UP_VER;
    d->engine = c->engine;
    d->np = P_COUNT;
    memcpy(d->name, c->name, sizeof d->name);
    for (i = 0; i < P_COUNT; i++)
        d->p[i] = UP_DEF;
    for (i = 0; i < nc; i++)                     /* P_LEVEL..: keys 0.. as they were */
        if (key_param(i) < P_COUNT)
            d->p[key_param(i)] = up_v8(c->p[i]);
    for (i = 0; i < 8u; i++)
        d->p[P_E0 + i] = up_v8(c->p[nc + i]);
    memcpy(d->note, c->note, sizeof d->note);
    memcpy(d->flags, c->flags, sizeof d->flags);
}

/* values vals[n] (two bytes, UP_V2_DEF: not stored) under keys[n] -> d->p in today's P_* order */
static void up_keyed(up_rec_t *d, const uint8_t *keys, const int16_t *vals, uint32_t n)
{
    int16_t v[P_COUNT];
    uint32_t i;
    key_map(keys, vals, n, v, UP_V2_DEF);
    for (i = 0; i < P_COUNT; i++)
        d->p[i] = v[i] == UP_V2_DEF ? UP_DEF : up_v8(v[i]);
    for (; i < UP_PMAX; i++)
        d->p[i] = 0;
    d->np = P_COUNT;
}

/* bank b from its stored bytes raw (len, -1 = none; not up_bank[b] itself): today's "UPB3" as it is (mapped
 * to today's keys when stored with others), Jangada 0.8.2's "UPB2" and Felucca's "UPB1" converted; any other
 * shape -> empty (up_foreign: a save would wipe its 16). 1 = a bank this build reads (or none) */
static void up_pat_norm(uint8_t *note, uint8_t *flags);
static int up_bank_kind(const void *raw, int len)  /* 3 UPB3, 2 UPB2, 1 UPB1, 0 not a bank */
{
    const up_bank_t *b3 = (const up_bank_t *)raw;
    const up_bank_v2_t *b2 = (const up_bank_v2_t *)raw;
    const up_bank_v1_t *b1 = (const up_bank_v1_t *)raw;
    if (len == (int)sizeof *b3 && b3->magic == UP_BANK_MAGIC && b3->rsize == sizeof(up_rec_t) &&
        b3->nslot == UP_PER_BANK && b3->np && b3->np <= UP_PMAX)
        return 3;
    if (len == (int)sizeof *b2 && b2->magic == UP_V2_MAGIC && b2->rsize == sizeof(up_rec_v2_t) &&
        b2->nslot == UP_PER_BANK && b2->np && b2->np <= UP_V2_PMAX)
        return 2;
    if (len == (int)sizeof *b1 && b1->magic == UP_V1_MAGIC && b1->rsize == sizeof(up_rec_v1_t) &&
        b1->nslot == UP_PER_BANK)
        return 1;
    return 0;
}
static int up_bank_load(uint32_t b, const void *raw, int len)
{
    up_bank_t *bk = &up_bank[b];
    uint32_t i, k, kind = len > 0 ? (uint32_t)up_bank_kind(raw, len) : 0u;
    memset(bk, 0, sizeof *bk);
    up_foreign[b] = 0;
    if (len <= 0)
        return 1;
    if (!kind) {
        up_foreign[b] = 1;
        return 0;
    }
    if (kind == 1u) {
        const up_bank_v1_t *v1 = (const up_bank_v1_t *)raw;
        for (i = 0; i < UP_PER_BANK; i++)
            up_from_v1(&bk->r[i], &v1->r[i]);
    } else if (kind == 2u) {
        const up_bank_v2_t *v2 = (const up_bank_v2_t *)raw;
        for (i = 0; i < UP_PER_BANK; i++) {
            const up_rec_v2_t *o = &v2->r[i];
            up_rec_t *r = &bk->r[i];
            if (o->used != UP_USED || o->ver != 2u || o->np != v2->np)
                continue;                            /* empty or not readable: an empty slot */
            r->used = UP_USED;
            r->ver = UP_VER;
            r->engine = o->engine;
            memcpy(r->name, o->name, sizeof r->name);
            up_keyed(r, v2->key, o->p, v2->np);
            memcpy(r->note, o->note, sizeof r->note);
            memcpy(r->flags, o->flags, sizeof r->flags);
        }
    } else {
        uint32_t np, same;
        memcpy(bk, raw, sizeof *bk);
        np = bk->np;
        for (i = 0, same = np == P_COUNT; same && i < np; i++)
            same = bk->key[i] == P_KEY[i];
        if (!same) {                                 /* stored with other keys (another version): to today's order */
            uint8_t key[UP_PMAX];
            memcpy(key, bk->key, sizeof key);
            for (i = 0; i < UP_PER_BANK; i++) {
                up_rec_t *r = &bk->r[i];
                int16_t v[UP_PMAX];
                if (r->used != UP_USED || r->ver != UP_VER || r->np != np)
                    continue;
                for (k = 0; k < np; k++)
                    v[k] = r->p[k] == UP_DEF ? UP_V2_DEF : r->p[k];
                up_keyed(r, key, v, np);
            }
        }
    }
    for (i = 0; i < UP_PER_BANK; i++)                /* the patterns as the parser leaves them */
        for (k = 0; k < 16u; k++)
            up_pat_norm(&bk->r[i].note[k], &bk->r[i].flags[k]);
    up_bank_fresh(bk);
    return 1;
}

/* the record's values in today's P_* order; def = the defaults for what it does not hold */
static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        out[i] = r->p[i] == UP_DEF ? def[i] : r->p[i];
}

static int up_name_ok(const uint8_t *s, uint32_t n)   /* 1..12 printable ASCII */
{
    uint32_t i;
    if (!n || n > 12u)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 32u || s[i] > 126u)
            return 0;
    return 1;
}

static void up_name(uint32_t k, char *b)       /* upper case, 0-terminated: b holds 13 */
{
    const up_rec_t *r = up_rec(k);
    uint32_t i;
    for (i = 0; i < 12u && r->name[i]; i++)
        b[i] = r->name[i] >= 'a' && r->name[i] <= 'z' ? (char)(r->name[i] - 32) : r->name[i];
    b[i] = 0;
}

static void up_pat_norm(uint8_t *note, uint8_t *flags)   /* tie: no note; rest: no flags */
{
    *note &= 127u;
    if (*flags & 4u) {
        *note = 0;
        *flags = 4;
    } else {
        *flags = *note ? (uint8_t)(*flags & SF_STEP) : 0u;   /* Jangada: RTCH / CHNC kept */
    }
}

static void up_pat_from(up_rec_t *r, const step_t *st)   /* the first 16 steps -> the pattern */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        r->note[i] = st[i].time == ST_NOTE && st[i].n ? st[i].note[0] : 0u;
        r->flags[i] = st[i].time == ST_TIE ? 4u : st[i].flags;
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
}

static int up_pat_empty(const up_rec_t *r)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (r->note[i])
            return 0;
    return 1;
}

/* UP_PUT arguments: slot, engine, name, P_COUNT x v14, 16 x (note, flags) -> *r (values not yet
 * clamped); 0 ok, 1 bad arguments. *slot gets the slot byte when there is one. */
static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot)
{
    uint32_t i, n, k;
    if (na < 3u)
        return 1;
    *slot = a[0];
    for (n = 0; 2u + n < na && a[2 + n]; n++)
        ;
    k = 3u + n;                                  /* after the name's 0 */
    if (a[0] >= UP_SLOTS || a[1] >= NENGINES || 2u + n >= na || !up_name_ok(a + 2, n) ||
        na < k + 2u * P_COUNT + 32u)
        return 1;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
    r->ver = UP_VER;
    r->engine = a[1];
    r->np = P_COUNT;
    for (i = 0; i < n; i++)
        r->name[i] = (char)a[2 + i];
    for (i = 0; i < P_COUNT; i++, k += 2u)
        r->p[i] = up_v8((int32_t)((a[k] & 127u) | (a[k + 1] & 127u) << 7) - 8192);
    for (i = 0; i < 16u; i++, k += 2u) {
        r->note[i] = a[k];
        r->flags[i] = a[k + 1];
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
    return 0;
}

#ifndef UP_HOST
static const param_desc_t *up_desc(uint32_t e, uint32_t i)
{
    return i >= P_E0 && i < P_E0 + NEDIT ? &ENGINES[e]->edit[i - P_E0] : &TP[i];
}

static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        def[i] = up_desc(r->engine, i)->def;
    up_params(r, v, def);
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)clamp(v[i], up_desc(r->engine, i)->min, up_desc(r->engine, i)->max);
}

static void up_boot(void)                      /* persist_boot: the banks from flash */
{
#if FELUCCA_FLASH
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
    {
        uint32_t len = 0;
        const uint8_t *raw = flash_ok ? st_view(OBJ_UPRESET0 + b, &len) : 0;   /* (in place: an old bank is larger) */
        up_bank_load(b, raw, raw ? (int)len : -1);
    }
#endif
}

/* record k = *r (0: erase), then the bank to flash: 0 ok, 2 flash error, 3 no flash (kept in RAM) */
static int up_put(uint32_t k, const up_rec_t *r)
{
    up_bank_t *bk = &up_bank[k / UP_PER_BANK];
    if (up_foreign[k / UP_PER_BANK])
        return 2;                                    /* Jangada: not over a bank this build cannot read */
    up_bank_fresh(bk);
    if (r)
        *up_rec(k) = *r;
    else
        memset(up_rec(k), 0, sizeof(up_rec_t));
    if (!r) {
        uint32_t i;
        for (i = 0; i < NTRK; i++)
            if (trk[i].user == k + 1u)
                trk[i].user = 0;
    }
    up_gen++;
#if FELUCCA_FLASH
    if (flash_ok)
        return st_save(OBJ_UPRESET0 + k / UP_PER_BANK, bk, sizeof *bk) ? 2 : 0;
#endif
    return 3;
}

static void up_slot_label(char *b, uint32_t k)  /* "U07" */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}

/* the selected part's sound -> slot k; name 0 or "": engine name + slot number ("ANALOG 07").
 * 1 = the drum track is selected (it has no sound to store) */
static int up_store(uint32_t k, const char *name)
{
    up_rec_t r;
    uint32_t i;
    if (is_drum(TSEL))
        return 1;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.ver = UP_VER;
    r.engine = TSEL->eng_req;
    r.np = P_COUNT;
    if (name && name[0]) {
        for (i = 0; i < 12u && name[i]; i++)
            r.name[i] = name[i];
    } else {
        char b[16], l[4];
        str_cpy(b, ENGINES[TSEL->eng_req]->name, 9);
        up_slot_label(l, k);
        str_cpy(b + str_len(b), " ", 2);
        str_cpy(b + str_len(b), l + 1, 3);
        for (i = 0; i < 12u && b[i]; i++)
            r.name[i] = b[i];
    }
    for (i = 0; i < P_COUNT; i++)
        r.p[i] = up_v8(TSEL->p[i]);
    up_pat_from(&r, TSEL->step);
    return up_put(k, &r);
}

/* slot k -> the selected part's sound: engine and every parameter except its mix (LEVEL,
 * PAN, MUTE: the TRACKS faders) and its pattern parameters (param_kept); the pattern, with
 * the record's LEN / DIV / SWING / GATE, only into an empty sequencer (as factory presets).
 * 0 ok, 1 empty (or the drum track is selected) */
static int up_load(uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT], pat[P_SGATE - P_SLEN + 1];
    uint32_t i;
    track_t *t = TSEL;
    if (!up_used(k) || is_drum(t))
        return 1;
    r = up_rec(k);
    up_values(r, v);
    for (i = P_SLEN; i <= P_SGATE; i++)
        pat[i - P_SLEN] = v[i];
    for (i = 0; i < P_COUNT; i++)                       /* (LEN etc. of a kept pattern changed too, and */
        if (param_kept(i))                              /* a preset pattern then counted as edited) */
            v[i] = t->p[i];
    panic_req |= (uint8_t)(1u << song.sel);
    fm1_irq_off();                                      /* the audio ISR must not see half a sound */
    t->eng_req = r->engine;
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = v[i];
    t->preset = 0;
    fm1_irq_on();
    if (!up_pat_empty(r) && seq_replaceable(t)) {
        load_pat16(t, r->note, r->flags);
        for (i = P_SDIV; i <= P_SGATE; i++)
            t->p[i] = pat[i - P_SLEN];
        t->p[P_SLEN] = (int16_t)clamp(pat[0], 1, 16);   /* (the pattern has 16 steps) */
        pat_sig[song.sel] = seq_sig(t);
    }
    t->user = (uint8_t)(k + 1u);
    sync_reload = 1;
    ui.force = 1;
    return 0;
}

static uint32_t up_count(void)                 /* used slots */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

static uint32_t up_nth(uint32_t n)             /* slot of the n-th used one (n < up_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && !n--)
            return k;
    return 0;
}

static uint32_t up_rank(uint32_t slot)         /* used slots before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

/* SAVE > USER page actions, with the message in the top bar */
static void up_ui(uint32_t op, uint32_t k)     /* 0 load, 1 erase, 2 save */
{
    char l[4];
    int rc;
    up_slot_label(l, k);
    if (op != 1u && is_drum(TSEL)) {
        ui_message("DRUM TRACK: NO SOUND");
        return;
    }
    if (op < 2u && !up_used(k)) {
        ui_message("EMPTY SLOT");
        return;
    }
    if (op == 0u) {
        up_load(k);
        ui_say("LOADED ", l);
        return;
    }
    rc = op == 1u ? up_put(k, 0) : up_store(k, 0);
    if (rc == 3)
        ui_message(op == 1u ? "ERASED (RAM)" : "SAVED (RAM)");
    else if (rc)
        ui_message(op == 1u ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op == 1u ? "ERASED " : "SAVED ", l);
    ui.force = 1;
}
#endif
