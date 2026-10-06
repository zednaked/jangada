/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada (after Felucca 1.0.1 tests/backup_test.c): the editor's backup and restore
 * (firmware/src/editor_backup.c: LIST / GET / PUT, editor protocol v5) against a simulated NOR flash:
 * every object read back byte for byte, a restore of everything into an empty FM-1 (projects as
 * "JNG1", the working project loaded, the settings, the user preset banks), the CRC checked before
 * anything is written, malformed objects refused, the song playing, USB resets and timeouts, the
 * snapshot gone after another object, and the autosave held while a backup runs; the FM6 patch bank
 * (ids 8, 9) and the tracks' FM6 patches in the working project (Jangada 0.5).
 * Build: cc -w -Ibuild/gen -Ifirmware/src tests/backup_test.c -lm */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "libc.c"
#undef memset
#undef memcpy
#undef memcmp
static unsigned char host_samples[3][0x14000];
#define SMP_USER_XIP(k) host_samples[k]
static struct { volatile uint32_t notes, buttons; } fm1_in;
static volatile uint32_t fm1_ms;
static void lcd_sync(void) {}
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c) { (void)x; (void)y; (void)w; (void)h; (void)c; }
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *p) { (void)x; (void)y; (void)w; (void)h; (void)p; }
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
static uint32_t fm1_ticks(void) { return fm1_ms * 1000; }
static void fm1_wdt_feed(void) {}
static void fm1_irq_off(void) {}
static void fm1_irq_on(void) {}
static void fm1_led_key(uint32_t k, int on) { (void)k; (void)on; }
static int32_t fm1_enc_take(uint32_t i) { (void)i; return 0; }
static uint32_t fm1_input_edges(int x) { (void)x; return 0; }
static uint32_t fm1_input_note_edges(void) { return 0; }
#include "gfx.c"
#include "core.h"
#include "engines.c"
#include "mod.c"
#include "drums.c"
#include "params.c"
#include "voice.c"
#include "slicer.c"
#include "fx.c"
#include "usb.c"
#include "midi_uart.c"
#include "seq.c"
struct felucca_dbg { uint32_t in_audio, late, halves, last_us, stage; } felucca_dbg;
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;
#include "panel.c"
#include "ui.c"
#include "icons.c"
#include "ui_draw.c"
#include "ui_menu.c"
#define FM1_NCOL 8
static const int8_t FM1_KEYMAP[6][FM1_NCOL];
#define FM1_TICKS_PER_US 1
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_bg[FM1_NCOL];
static uint16_t fm1_led_bg_ns;
#include "ui_input.c"

/* the flash: storage.c over a RAM image of the 1 MiB part */
#define FELUCCA_FLASH 1
static uint8_t nor[0x100000], flash_ok = 1;
static uint32_t erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); erases++; return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n) { memcpy(nor + off, src, n); return 0; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static uint32_t fl_jedec_ram(void) { return 0x856014u; }
static void fl_plain_window_init(void) {}
#define FL_FAR(fn) (fn)
#include "storage.c"
#include "upreset.c"
#include "project.c"
#include "fm6_bank.c"

/* editor.c's part the backup uses */
enum { ED_BK_LIST = 34, ED_BK_GET, ED_BK_PUT };
static uint8_t rep[4096];
static uint32_t rep_n;
static void ed_b(uint32_t v) { if (rep_n < sizeof rep) rep[rep_n++] = (uint8_t)(v & 127u); }
static uint32_t ed_unpack7(const uint8_t *a, uint32_t na, uint8_t *out, uint32_t max)
{
    uint32_t n = 0;
    while (na && n < max) {
        uint32_t m = *a++, j;
        na--;
        for (j = 0; j < 7u && na && n < max; j++, na--)
            out[n++] = (uint8_t)(*a++ | ((m >> j) & 1u) << 7);
    }
    return n;
}
static uint8_t ed_smp_buf[512];
#include "editor_backup.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("backup: %-80s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

/* power-on: the default sounds, empty patterns (ui_menu.c INIT calls it too) */
static void felucca_init(void)
{
    uint32_t i;
    fm6_init();
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);
    }
    song.sel = 0;
}

/* power-on with an empty flash */
static void power_on(void)
{
    uint32_t i;
    memset(nor, 0xFF, sizeof nor);
    memset(host_samples, 0xFF, sizeof host_samples);
    memset(proj_slot, 0, sizeof proj_slot);
    memset(up_bank, 0, sizeof up_bank);
    memset(&persist_saved, 0, sizeof persist_saved);
    memset(&settings, 0, sizeof settings);
    panel = PANEL_DEFAULT;
    settings_init();
    felucca_init();
    song.playing = 0;
    transport_req = 0;
    for (i = 0; i < SMP_USER_SLOTS; i++)
        smp_user_scan(i);
    ed_bk_cur = ED_BK_NONE;
    ed_bk_put = 0;
    usb.resets = 0;
    autosave_hold = 0;
    fm1_ms = 1000;
    erases = 0;
}

static void call(uint32_t cmd, const uint8_t *a, uint32_t n)
{
    rep_n = 0;
    ed_backup(cmd, a, n);
}
static void put32(uint8_t *a, uint32_t v) { for (uint32_t i = 0; i < 5u; i++) a[i] = (uint8_t)((v >> (7u * i)) & 127u); }
static uint32_t get32(const uint8_t *a) { return ed_bk_r32(a); }

/* the archive as the editor keeps it */
typedef struct { uint32_t id, len, crc; uint8_t data[0x14000]; } obj_t;
static obj_t arc[ED_BK_N];

static uint32_t list(void)                          /* -> rc; arc[].id / len / crc */
{
    uint32_t i;
    call(ED_BK_LIST, 0, 0);
    if (rep[0])
        return rep[0];
    if (rep[1] != ED_BK_N || rep_n != 2u + ED_BK_N * 11u)
        return 99;
    for (i = 0; i < ED_BK_N; i++) {
        const uint8_t *o = rep + 2 + i * 11u;
        arc[i].id = o[0];
        arc[i].len = get32(o + 1);
        arc[i].crc = get32(o + 6);
    }
    return 0;
}
static uint32_t get(uint32_t id, uint32_t off, uint32_t n, uint8_t *out)   /* -> rc */
{
    uint8_t a[8] = {(uint8_t)id};
    put32(a + 1, off);
    a[6] = (uint8_t)(n & 127u);
    a[7] = (uint8_t)(n >> 7);
    call(ED_BK_GET, a, 8);
    if (rep[1])
        return rep[1];
    if (rep[0] != id || get32(rep + 2) != off || (rep[7] | rep[8] << 7) != n || ed_unpack7(rep + 9, rep_n - 9u, out, n) != n)
        return 98;
    return 0;
}
static uint32_t capture(void)                       /* LIST, then every object in order -> rc */
{
    uint32_t i, off, rc = list();
    for (i = 0; !rc && i < ED_BK_N; i++) {
        for (off = 0; !rc && off < arc[i].len; off += 256u)
            rc = get(arc[i].id, off, arc[i].len - off > 256u ? 256u : arc[i].len - off, arc[i].data + off);
        if (!rc && st_crc32(arc[i].data, arc[i].len) != arc[i].crc)
            rc = 97;
    }
    return rc;
}

static uint32_t put_begin(uint32_t id, uint32_t len, uint32_t crc)
{
    uint8_t a[12] = {0, (uint8_t)id};
    put32(a + 2, len);
    put32(a + 7, crc);
    call(ED_BK_PUT, a, sizeof a);
    return rep[2];
}
static uint32_t put_chunk(uint32_t id, uint32_t off, const uint8_t *p, uint32_t n)
{
    uint8_t a[16 + 300];
    uint32_t k = 7;
    a[0] = 1;
    a[1] = (uint8_t)id;
    put32(a + 2, off);
    while (n) {
        uint32_t g = n > 7u ? 7u : n, m = 0, i;
        for (i = 0; i < g; i++)
            m |= (uint32_t)(p[i] >> 7) << i;
        a[k++] = (uint8_t)m;
        for (i = 0; i < g; i++)
            a[k++] = p[i] & 127u;
        p += g;
        n -= g;
    }
    call(ED_BK_PUT, a, k);
    return rep[2];
}
static uint32_t put_end(uint32_t id, uint32_t op)
{
    uint8_t a[2] = {(uint8_t)op, (uint8_t)id};
    call(ED_BK_PUT, a, 2);
    return rep[2];
}
static uint32_t put_all(uint32_t id, const uint8_t *p, uint32_t len, uint32_t crc)
{
    uint32_t off, rc = put_begin(id, len, crc);
    for (off = 0; !rc && off < len; off += 256u)
        rc = put_chunk(id, off, p + off, len - off > 256u ? 256u : len - off);
    return rc ? rc : put_end(id, 2);
}
static obj_t *obj(uint32_t id)
{
    uint32_t i;
    for (i = 0; i < ED_BK_N; i++)
        if (arc[i].id == id)
            return &arc[i];
    return 0;
}

/* a valid user sample slot k: one zone over n ADPCM bytes */
static void sample_slot(uint32_t k, uint32_t n)
{
    smp_user_hdr_t *h = (smp_user_hdr_t *)(void *)host_samples[k];
    uint32_t i;
    memset(host_samples[k], 0xFF, sizeof host_samples[k]);
    for (i = 0; i < n; i++)
        host_samples[k][SMP_USER_DATA + i] = (uint8_t)(i * 7u + k);
    memset(h, 0, sizeof *h);
    h->magic = SMP_USER_MAGIC;
    h->version = 1;
    h->nz = 1;
    memcpy(h->name, "TEST", 4);
    h->data_len = n;
    h->crc = st_crc32(host_samples[k] + SMP_USER_DATA, n);
    h->zone[0].n = 2u * n;
    h->zone[0].le = 2u * n - 1u;
    h->zone[0].rate = 65536u;
    h->zone[0].hi = 127;
    smp_user_scan(k);
}

int main(void)
{
    static obj_t src[ED_BK_N];
    uint32_t i, rc;
    int16_t bpm;

    power_on();
    rc = list();
    check("LIST: 13 objects; the working project (with its FM6 patches) and the settings, the rest empty",
          !rc && ED_BK_N == 13u && arc[0].id == 0 && arc[0].len == JNG_SIZE(P_COUNT, G_COUNT) + JNG_FM6_SIZE &&
          arc[1].len == sizeof(persist_t) && arc[2].len == 0 && arc[6].len == 0 && arc[8].id == 8 && arc[8].len == 0 &&
          arc[9].id == 9 && arc[9].len == 0 && arc[10].id == 32 && arc[10].len == 0 && arc[12].id == 34);
    check("LIST: the autosave waits while a backup runs", (int32_t)(autosave_hold - fm1_ms) > 0);

    /* something on every kind of object */
    song.g[G_BPM] = 133;
    trk[0].step[3].n = 1;
    trk[0].step[3].note[0] = 67;
    trk[0].step[3].time = ST_NOTE;
    project_save(1);                                 /* project 2 */
    song.g[G_BPM] = 97;
    project_save(3);                                 /* project 4 */
    song.g[G_BPM] = 121;                             /* the working project */
    trk[1].p[P_LEVEL] = 77;
    settings.palette = 2;
    settings.lowcut = 1;
    panel.dir[EN_K2] = -1;
    lights_from_word(2u | 1u << 8 | 1u << 11);       /* LIGHTS level 2, NOTES on, USB AUDIO FULL */
    {
        up_rec_t r;
        memset(&r, 0, sizeof r);
        r.used = UP_USED;
        r.ver = UP_VER;
        r.engine = 1;
        r.np = P_COUNT;
        memcpy(r.name, "BACKUP ME", 9);
        for (i = 0; i < P_COUNT; i++)
            r.p[i] = UP_DEF;
        r.p[P_LEVEL] = 99;
        r.note[0] = 60;
        up_put(17, &r);                              /* bank 2 */
    }
    sample_slot(1, 3000);
    {   /* the FM6 bank: B3 = F2, B20 = F5; the working project's track 2 plays an edited patch */
        uint8_t v[FP_SIZE + 1u];
        fm6_bank_put(2, FM6_FACTORY[1]);
        fm6_bank_put(19, FM6_FACTORY[4]);
        fm6_unpack(FM6_FACTORY[6], v);
        v[FP_ALG] = 21;
        fm6_set_patch(1, v);
    }
    rc = capture();
    check("capture: every object read back, each matching its CRC from LIST", !rc);
    check("capture: projects 2 and 4 stored as JNG1, 1 and 3 empty",
          obj(3)->len == JNG_SIZE(P_COUNT, G_COUNT) + JNG_FM6_SIZE && obj(5)->len == obj(3)->len && !obj(2)->len && !obj(4)->len &&
          !memcmp(obj(3)->data, "JNG1", 4));
    check("capture: project 2 is what the flash holds", !memcmp(obj(3)->data, nor + 0x97000 + 2u * 4096u + 256u, obj(3)->len) ||
                                                       !memcmp(obj(3)->data, nor + 0x97000 + 3u * 4096u + 256u, obj(3)->len));
    check("capture: the user preset bank 2 and the sample slot USR2",
          !obj(6)->len && obj(7)->len == sizeof(up_bank_t) && obj(33)->len == SMP_USER_DATA + 3000u && !obj(32)->len &&
          !memcmp(obj(33)->data, host_samples[1], obj(33)->len));
    {
        persist_t p;
        memcpy(&p, obj(1)->data, sizeof p);
        check("capture: the settings (palette, low cut, the panel calibration, the lights word)",
              obj(1)->len == sizeof(persist_t) && p.magic == PERSIST_MAGIC && p.palette == 2 && p.lowcut == 1 &&
              p.panel.dir[EN_K2] == -1 && p.lights == lights_word());
    }
    {
        uint8_t b[16];
        check("GET of the working project after another object: rc 5 (LIST again)", get(0, 0, 16, b) == 5);
        check("GET past the end: rc 1", get(3, obj(3)->len - 8u, 16, b) == 1);
        check("GET of an unknown object: rc 1", get(10, 0, 16, b) == 1 && get(31, 0, 16, b) == 1);
    }
    check("capture: the FM6 bank, both halves ('FM6B', B3 and B20 used)",
          obj(8)->len == sizeof(fm6_half_t) && obj(9)->len == sizeof(fm6_half_t) && !memcmp(obj(8)->data, "FM6B", 4) &&
          ((fm6_half_t *)(void *)obj(8)->data)->used == 1u << 2 && ((fm6_half_t *)(void *)obj(9)->data)->used == 1u << 3);
    memcpy(src, arc, sizeof arc);

    /* the change while backing up: the CRC of LIST does not match any more */
    rc = list();
    {
        static uint8_t b[4096];
        song.g[G_BPM] = 60;
        project_save(1);                             /* (reuses proj_io: the snapshot is gone) */
        rc = get(0, 0, 256, b);
        check("a project saved on the panel during a backup: the editor sees it (CRC or rc 5)",
              rc == 5 || st_crc32(b, 256) != st_crc32(src[0].data, 256));
    }

    /* everything into an empty FM-1 */
    power_on();
    for (i = 2; i < ED_BK_N; i++)                    /* projects, banks (the samples: SMP_* commands) */
        if (src[i].id < 32u && (rc = put_all(src[i].id, src[i].data, src[i].len, src[i].crc)) != 0)
            break;
    if (!rc)
        rc = put_all(1, src[1].data, src[1].len, src[1].crc);
    if (!rc)
        rc = put_all(0, src[0].data, src[0].len, src[0].crc);
    check("restore: every object accepted", !rc);
    {
        uint8_t pk[FM6_PACKED], v[FP_SIZE + 1u];
        int ok = fm6_bank_used == (1u << 2 | 1u << 19) && !fm6_bank_get(2, pk) && !memcmp(pk, FM6_FACTORY[1], FM6_PACKED) &&
                 !fm6_bank_get(19, pk) && !memcmp(pk, FM6_FACTORY[4], FM6_PACKED) && fm6_bank_get(3, pk);
        fm6_bank_used = 0;
        fm6_bank_boot();                                 /* power-off: from flash */
        check("restore: the FM6 bank (B3, B20) in use and in flash", ok && fm6_bank_used == (1u << 2 | 1u << 19));
        fm6_unpack(FM6_FACTORY[6], v);
        v[FP_ALG] = 21;
        fm6_sanitize(v);
        check("restore: the working project's edited FM6 patch on track 2", !memcmp(fm6_patch[1], v, FP_SIZE));
    }
    check("restore: projects 2 and 4 in RAM and in flash; 1 and 3 empty",
          project_used(1) && project_used(3) && !project_used(0) && !project_used(2) && proj_slot[1].g[G_BPM] == 133 &&
          proj_slot[3].g[G_BPM] == 97);
    {
        memset(proj_slot, 0, sizeof proj_slot);      /* power-off: from flash */
        for (i = 0; i < 4u; i++)
            proj_fetch(i);
        check("restore: the projects load from flash after a power-off",
              project_used(1) && project_used(3) && proj_slot[1].g[G_BPM] == 133 && proj_slot[1].t[0].step[3].note[0] == 67);
    }
    check("restore: the working project is loaded", song.g[G_BPM] == 121 && trk[1].p[P_LEVEL] == 77 && trk[0].step[3].note[0] == 67);
    check("restore: the settings (palette, low cut, calibration, lights) in use and in flash",
          settings.palette == 2 && settings.lowcut == 1 && fx_lowcut == 1 && panel.dir[EN_K2] == -1 &&
          persist_saved.palette == 2 && lights_lvl == 2 && lights_notes == 1 && usb_full == 1 &&
          persist_saved.lights == lights_word());
    {
        char nm[13];
        up_name(17, nm);
        check("restore: the user preset bank (U18 'BACKUP ME')", up_used(17) && !strcmp(nm, "BACKUP ME") && up_rec(17)->p[P_LEVEL] == 99);
        memset(up_bank, 0, sizeof up_bank);
        up_boot();
        check("restore: the user preset bank loads from flash", up_used(17) && !up_used(0));
    }
    rc = capture();
    check("restore -> backup again: the same bytes, object for object",
          !rc && !memcmp(arc[3].data, src[3].data, src[3].len) && !memcmp(arc[7].data, src[7].data, src[7].len) &&
          !memcmp(arc[1].data, src[1].data, src[1].len) && arc[3].crc == src[3].crc && arc[0].len == src[0].len);

    /* refusals: nothing written */
    power_on();
    bpm = song.g[G_BPM];
    rc = put_all(3, src[3].data, src[3].len, src[3].crc ^ 1u);
    check("a damaged object (CRC): rc 2, nothing written", rc == 2 && !erases && !project_used(1));
    {
        static uint8_t b[4096];
        memcpy(b, src[3].data, src[3].len);
        b[40] ^= 0x10;
        rc = put_all(3, b, src[3].len, st_crc32(b, src[3].len));
        check("a project that is not one (its sum): rc 2, nothing written", rc == 2 && !erases && !project_used(1));
        rc = put_all(0, b, src[3].len, st_crc32(b, src[3].len));
        check("a working project that is not one: rc 2, nothing changed", rc == 2 && song.g[G_BPM] == bpm);
        memcpy(b, src[1].data, src[1].len);
        ((persist_t *)(void *)b)->panel.btn[3] = ((persist_t *)(void *)b)->panel.btn[4];
        rc = put_all(1, b, src[1].len, st_crc32(b, src[1].len));
        check("settings with a button twice in the calibration: rc 2", rc == 2 && !erases);
        memcpy(b, src[1].data, src[1].len);
        ((persist_t *)(void *)b)->palette = 99;
        rc = put_all(1, b, src[1].len, st_crc32(b, src[1].len));
        check("settings with a palette out of range: rc 2", rc == 2 && !erases);
        memcpy(b, src[7].data, src[7].len);
        ((up_bank_t *)(void *)b)->rsize = 192;
        rc = put_all(7, b, src[7].len, st_crc32(b, src[7].len));
        check("a user preset bank of another shape: rc 2", rc == 2 && !erases);
    }
    {   /* a backup of Jangada 0.2: the settings without the lights word */
        lights_from_word(2u);
        rc = put_all(1, src[1].data, PERSIST_SIZE_V02, st_crc32(src[1].data, PERSIST_SIZE_V02));
        check("settings of Jangada 0.2 (no lights word): restored, the lights off, stored at today's size",
              !rc && settings.palette == 2 && lights_lvl == LIGHTS_OFF && !usb_full && persist_saved.lights == lights_word() &&
              st_load(OBJ_SETTINGS, &persist_saved, sizeof persist_saved) == (int)sizeof(persist_t));
        erases = 0;
    }
    check("BEGIN with a wrong length (settings, a bank): rc 1",
          put_begin(1, 12, 0) == 1 && put_begin(6, 100, 0) == 1 && put_begin(0, 0, 0) == 1 && put_begin(8, 10, 0) == 1);
    check("DATA without a BEGIN: rc 5", put_chunk(3, 0, src[3].data, 16) == 5);
    put_begin(3, src[3].len, src[3].crc);
    check("DATA at the wrong offset: rc 1", put_chunk(3, 256, src[3].data, 256) == 1);
    check("DATA past the length: rc 1", put_chunk(3, 0, src[3].data, 256) == 0 && put_chunk(3, 256, src[3].data, 256) == 0 &&
                                        put_begin(6, 0, 0) == 0 && put_chunk(6, 0, src[3].data, 16) == 1);
    put_begin(3, src[3].len, src[3].crc);
    put_chunk(3, 0, src[3].data, 256);
    usb.resets++;
    check("a USB reset during a restore: rc 5", put_chunk(3, 256, src[3].data + 256, 256) == 5);
    put_begin(3, src[3].len, src[3].crc);
    fm1_ms += ED_BK_HOLD + 1u;
    check("more than 15 s since the last chunk: rc 5", put_chunk(3, 0, src[3].data, 256) == 5);
    song.playing = 1;
    rc = put_all(3, src[3].data, src[3].len, src[3].crc);
    check("the song playing: rc 3, nothing written", rc == 3 && !erases && !project_used(1));
    song.playing = 0;
    check("abort: then a commit has no BEGIN", put_begin(3, src[3].len, src[3].crc) == 0 && put_end(3, 3) == 0 && put_end(3, 2) == 5);

    /* an empty object empties: a project and a bank */
    rc = put_all(3, src[3].data, src[3].len, src[3].crc);
    rc |= put_all(7, src[7].data, src[7].len, src[7].crc);
    check("restore of single objects", !rc && project_used(1) && up_used(17));
    rc = put_all(3, src[3].data, 0, 0) | put_all(7, src[7].data, 0, 0);
    memset(proj_slot, 0, sizeof proj_slot);
    proj_fetch(1);
    memset(up_bank, 0, sizeof up_bank);
    up_boot();
    check("an empty project / bank in the backup empties the slot (RAM and flash)", !rc && !project_used(1) && !up_used(17));

    {   /* a backup of Jangada 0.4 (11 objects, no FM6 bank): it restores, the bank stays */
        uint32_t used;
        rc = put_all(8, src[8].data, src[8].len, src[8].crc) | put_all(9, src[9].data, src[9].len, src[9].crc);
        used = fm6_bank_used;
        for (i = 2; !rc && i < 8u; i++)
            rc = put_all(src[i].id, src[i].data, src[i].len, src[i].crc);
        check("a backup without ids 8 / 9 restores and keeps the FM6 bank", !rc && used && fm6_bank_used == used);
    }
    {   /* FM6 bank halves: refused when malformed; an empty one empties it */
        static uint8_t b[4096];
        memcpy(b, src[8].data, src[8].len);
        ((fm6_half_t *)(void *)b)->half = 1;             /* the other half's */
        erases = 0;
        rc = put_all(8, b, src[8].len, st_crc32(b, src[8].len));
        check("an FM6 bank half stored as the other one: rc 2, nothing written", rc == 2 && !erases);
        memcpy(b, src[9].data, src[9].len);
        ((fm6_half_t *)(void *)b)->v[3][40] = 0x80;      /* not 7-bit */
        rc = put_all(9, b, src[9].len, st_crc32(b, src[9].len));
        check("an FM6 bank half with a byte above 127: rc 2", rc == 2 && !erases);
        check("BEGIN of an FM6 bank half with a wrong length: rc 1", put_begin(9, 2000, 0) == 1);
        rc = put_all(9, b, 0, 0);
        fm6_bank_used = 0;
        fm6_bank_boot();
        check("an empty FM6 bank half in the backup empties B17..B32 (RAM and flash)", !rc && fm6_bank_used == 1u << 2);
    }

    {   /* a project's FM6 patches (proj_apply): its own; one without (Jangada 0.4): its PTCH's patch */
        uint8_t v[FP_SIZE + 1u], w[FP_SIZE + 1u];
        set_engine_of(&trk[1], ENGI_FM6);
        trk[1].p[P_E7] = 2;                              /* F3 */
        fm6_poll();
        proj_capture(&autosave_buf);
        memcpy(autosave_buf.fm6[1], FM6_FACTORY[5], FM6_PACKED);   /* its own patch: F6's */
        proj_apply(&autosave_buf);
        fm6_poll();
        fm6_unpack(FM6_FACTORY[5], v);
        check("a project with FM6 patches: the track plays its own (PTCH F3 kept)",
              !memcmp(fm6_patch[1], v, FP_SIZE) && trk[1].p[P_E7] == 2);
        autosave_buf.has_fm6 = 0;
        proj_apply(&autosave_buf);
        fm6_poll();
        fm6_unpack(FM6_FACTORY[2], w);
        check("a project without them (Jangada 0.4): the track plays its PTCH's patch (F3)", !memcmp(fm6_patch[1], w, FP_SIZE));
        transport_req = 0;
    }

    /* without flash */
    flash_ok = 0;
    check("no flash: LIST rc 4", list() == 4);
    flash_ok = 1;

    printf(fails ? "BACKUP TESTS FAILED (%d)\n" : "backup tests passed\n", fails);
    return fails ? 1 : 0;
}
