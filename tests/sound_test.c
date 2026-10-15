/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada 1.0.1 (issue #7): TRACK_SOUND, editor protocol v8 (firmware/src/editor_sound.c): a track's sound read in
 * one frame and written back from that frame as it is: the engine and every parameter, an FM6 track's own patch
 * too; the mix and the pattern parameters kept, keys the firmware does not know skipped, missing ones the
 * engine's default, values clamped; the drum track and malformed frames refused (no reply).
 * Build: cc -w -Ibuild/gen -Ifirmware/src tests/sound_test.c -lm */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
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
static int32_t host_enc[8];                         /* detents waiting per encoder (dx7_test.c turns PRESETS) */
static uint32_t host_edges;                         /* buttons pressed since the last ui_input */
static int32_t fm1_enc_take(uint32_t i) { int32_t v = i < 8u ? host_enc[i] : 0; if (i < 8u) host_enc[i] = 0; return v; }
static uint32_t fm1_input_edges(int x) { uint32_t e = host_edges; (void)x; host_edges = 0; return e; }
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
static uint8_t fm1_led[FM1_NCOL], fm1_led_dim[FM1_NCOL], fm1_led_bg[FM1_NCOL], fm1_led_breath[FM1_NCOL];
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

/* editor.c's part the backup uses */
#include "upreset.c"
#include "project.c"
#include "fm6_bank.c"

/* editor.c's part TRACK_SOUND uses */
static uint8_t rep[1024];
static uint32_t rep_n;
static void ed_b(uint32_t v) { if (rep_n < sizeof rep) rep[rep_n++] = (uint8_t)(v & 127u); }
static void ed_v(int32_t v)
{
    uint32_t u = (uint32_t)(clamp(v, -8192, 8191) + 8192);
    ed_b(u);
    ed_b(u >> 7);
}
static int32_t ed_rv(const uint8_t *p) { return (int32_t)(p[0] | p[1] << 7) - 8192; }
static uint32_t ed_eng(const track_t *t) { return is_drum(t) ? NENGINES : t->eng_req % NENGINES; }
#include "editor_sound.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("sound: %-80s %s\n", what, ok ? "ok" : "FAIL");
    if (!ok)
        fails++;
}

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

static int call(const uint8_t *a, uint32_t n)       /* -> replied */
{
    rep_n = 0;
    return ed_sound(ED_SOUND, a, n);
}
static uint8_t dump[1024];
static uint32_t dump_n;
static void take(uint32_t k)                        /* the get's reply, as a DAW keeps it */
{
    uint8_t a[1] = {(uint8_t)k};
    call(a, 1);
    memcpy(dump, rep, rep_n);
    dump_n = rep_n;
}

int main(void)
{
    uint8_t a[1024], pk[FM6_PACKED], pk2[FM6_PACKED];
    int16_t keep[P_COUNT];
    uint32_t i, n;
    int same;
    felucca_init();

    /* get: track, engine, count, count x (key, v14) */
    take(0);
    n = (uint32_t)dump[2] | (uint32_t)dump[3] << 7;
    check("get: track 1, its engine, every parameter under its key",
          dump[0] == 0 && dump[1] == trk[0].eng_req && n == P_COUNT && dump_n == 4u + 3u * P_COUNT &&
              dump[4] == P_KEY[0] && ed_rv(dump + 5) == trk[0].p[0]);

    /* the sound changes on the device, the dump goes back: the sound is there again */
    memcpy(keep, trk[0].p, sizeof keep);
    {
        uint32_t e0 = trk[0].eng_req;
        set_engine_of(&trk[0], (e0 + 1u) % ENGI_FM6);
        trk[0].p[P_ATK] = (int16_t)(keep[P_ATK] + 7);
        trk[0].p[P_E0] = (int16_t)(trk[0].p[P_E0] ^ 1);
        trk[0].p[P_LEVEL] = 33;                        /* the mix moves too: a put keeps it */
        trk[0].p[P_SLEN] = 5;
        sync_reload = 0;
        check("put: the dump sent back as it came is taken, answered with a dump of the sound now",
              call(dump, dump_n) && rep_n == dump_n &&
                  trk[0].eng_req == e0 && trk[0].p[P_LEVEL] == 33 && trk[0].p[P_SLEN] == 5);
        same = 1;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i) && trk[0].p[i] != keep[i])
                same = 0;
        check("put: the engine and every parameter back; LEVEL and LEN as the device has them; RELOAD for the editor",
              same && sync_reload && trk[0].preset == 0 && trk[0].user == 0);
    }

    /* an FM6 track: the patch rides along */
    set_engine_of(&trk[1], ENGI_FM6);
    trk[1].p[P_E7] = 3;
    fm6_poll();
    take(1);
    memset(pk, 0, sizeof pk);
    fm6_pack(fm6_patch[1], pk);
    check("get: an FM6 track ends with its 128-byte patch",
          dump[1] == ENGI_FM6 && dump_n == 4u + 3u * P_COUNT + FM6_PACKED &&
              !memcmp(dump + 4u + 3u * P_COUNT, pk, FM6_PACKED));
    trk[1].p[P_E7] = 5;                                /* another patch on the device */
    fm6_poll();
    call(dump, dump_n);
    fm6_poll();                                        /* (the main loop: PTCH 3 must not reload over it) */
    memset(pk2, 0, sizeof pk2);
    fm6_pack(fm6_patch[1], pk2);
    check("put: the FM6 track plays the dumped patch again, PTCH as dumped", trk[1].p[P_E7] == 3 && !memcmp(pk, pk2, FM6_PACKED));
    /* an edited patch (Dexed), not PTCH's own: the dump keeps the edit */
    fm6_patch[1][0] = (uint8_t)(fm6_patch[1][0] ^ 1);
    take(1);
    memset(pk, 0, sizeof pk);
    fm6_pack(fm6_patch[1], pk);
    fm6_load_slot(1, 3);
    call(dump, dump_n);
    fm6_poll();
    memset(pk2, 0, sizeof pk2);
    fm6_pack(fm6_patch[1], pk2);
    check("put: an FM6 patch edited away from PTCH's comes back edited", !memcmp(pk, pk2, FM6_PACKED));

    /* keys: unknown skipped, missing ones the default, out of range clamped */
    a[0] = 2;
    a[1] = 0;                                          /* engine 0 */
    a[2] = 3;
    a[3] = 0;
    a[4] = 127;                                        /* a key no firmware has yet */
    a[5] = 0x10;
    a[6] = 0x40;
    a[7] = P_KEY[P_ATK];
    a[8] = 0x7F;                                       /* 8191: clamped */
    a[9] = 0x7F;
    a[10] = P_KEY[P_SUS];
    a[11] = (uint8_t)((8192 + 9) & 127);
    a[12] = (uint8_t)((8192 + 9) >> 7);
    trk[2].p[P_DEC] = (int16_t)(TP[P_DEC].def + 3);
    check("put: an unknown key skipped, a missing one the default, a value clamped",
          call(a, 13) && trk[2].eng_req == 0 && trk[2].p[P_ATK] == TP[P_ATK].max && trk[2].p[P_SUS] == 9 &&
              trk[2].p[P_DEC] == TP[P_DEC].def);

    /* refused: no reply, nothing changes */
    memcpy(keep, trk[0].p, sizeof keep);
    take(0);
    dump[1] = NENGINES;
    i = call(dump, dump_n);
    dump[1] = trk[0].eng_req;
    n = call(dump, dump_n - 1u);
    check("refused: an engine out of range, a frame one byte short",
          !i && !n && !memcmp(keep, trk[0].p, sizeof keep));
    a[0] = 3;
    a[1] = 0;
    a[2] = 0;
    a[3] = 0;
    check("refused: a put into the drum track (get still answers); a track out of range",
          is_drum(&trk[3]) && !call(a, 4) && call(a, 1) && rep[1] == NENGINES && (a[0] = NTRK, !call(a, 1)));
    take(0);
    memset(dump + dump_n, 0, FM6_PACKED);               /* a patch after the values of engine 0 */
    dump[1] = 0;
    check("refused: a patch after the values of an engine other than FM6", !call(dump, dump_n + FM6_PACKED));
    check("the frame fits the device's SysEx buffer (640) with the header, FM6 patch included",
          5u + 1u + 4u + 3u * P_COUNT + FM6_PACKED + 1u <= 640u);

    printf("sound: %s\n", fails ? "FAILED" : "all ok");
    return fails != 0;
}
