/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada 1.0.1 (issue #7, lfrancis): a track's sound in one frame, for a DAW or a librarian that keeps a synth's
 * edit buffer with the song and sends it back later (editor protocol v8, cmd 96, web/EDITOR_PROTOCOL.md):
 *   get  track                                        -> track, engine, count (2 x 7 bit), count x (key, v14),
 *                                                        then (an FM6 track) its 128-byte packed patch
 *   put  track, engine, count, count x (key, v14) [, patch]   -> the same as get, after the sound is in
 * The reply of a get is a put as it is: send it back and the track has that sound again. The values travel under
 * their stable keys (keys.h), as the user presets and projects keep them, so a dump survives firmware updates:
 * unknown keys are skipped, missing ones take the engine's default, every value is clamped to its range. A put is
 * a preset load (upreset.c up_load): the mix and the pattern parameters (param_kept) stay as they are, the pattern
 * is never touched. An FM6 put without the patch takes PTCH's. The drum track, an engine out of range or a frame
 * of the wrong length get no reply. Included by editor.c. */
enum { ED_SOUND = 96 };                          /* Jangada's own numbers start at 96 (editor.c) */

static void ed_sound_out(uint32_t k)
{
    const track_t *t = &trk[k];
    uint8_t pk[FM6_PACKED];
    uint32_t i;
    ed_b(k);
    ed_b(ed_eng(t));
    ed_b(P_COUNT & 127u);
    ed_b(P_COUNT >> 7);
    for (i = 0; i < P_COUNT; i++) {
        ed_b(P_KEY[i]);
        ed_v(t->p[i]);
    }
    if (is_drum(t) || t->eng_req != ENGI_FM6)
        return;
    memset(pk, 0, sizeof pk);
    fm6_pack(fm6_patch[k], pk);
    for (i = 0; i < FM6_PACKED; i++)
        ed_b(pk[i]);
}

/* a: engine, count, the pairs [, the patch] -> trk[k]; 0 ok, 1 refused */
static int ed_sound_in(uint32_t k, const uint8_t *a, uint32_t na)
{
    track_t *t = &trk[k];
    int16_t v[P_COUNT];
    uint32_t e, n, i, j, end;
    if (na < 3u || is_drum(t) || a[0] >= NENGINES)
        return 1;
    e = a[0];
    n = (uint32_t)a[1] | (uint32_t)a[2] << 7;
    end = 3u + 3u * n;
    if (na != end && !(e == ENGI_FM6 && na == end + FM6_PACKED))
        return 1;
    for (i = 0; i < P_COUNT; i++)
        v[i] = up_desc(e, i)->def;
    for (j = 0; j < n; j++) {
        const uint8_t *q = a + 3u + 3u * j;
        for (i = 0; i < P_COUNT && P_KEY[i] != q[0]; i++)
            ;
        if (i < P_COUNT)
            v[i] = (int16_t)clamp(ed_rv(q + 1), up_desc(e, i)->min, up_desc(e, i)->max);
    }
    for (i = 0; i < P_COUNT; i++)
        if (param_kept(i))
            v[i] = t->p[i];
    panic_req |= (uint8_t)(1u << k);
    fm1_irq_off();                                      /* the audio ISR must not see half a sound */
    t->eng_req = (uint8_t)e;
    for (i = 0; i < P_COUNT; i++)
        t->p[i] = v[i];
    t->preset = 0;
    fm1_irq_on();
    t->user = 0;
    if (na > end) {                                     /* the patch, PTCH as it is (as FM6_PUT target 0) */
        uint8_t pv[FP_SIZE + 1u];
        fm6_unpack(a + end, pv);
        fm6_set_patch(k, pv);
        fm6_slot[k] = (uint8_t)t->p[P_E7];
    } else if (e == ENGI_FM6) {
        fm6_slot[k] = 0xFFu;                            /* fm6_poll loads PTCH's patch */
    }
    if (k == song.sel)
        sync_reload = 1;                                /* the editor re-reads it (RELOAD) */
    ui.force = 1;
    return 0;
}

static int ed_sound(uint32_t cmd, const uint8_t *a, uint32_t na)   /* 1: handled, the reply is in ed_out */
{
    if (cmd != ED_SOUND || na < 1u || a[0] >= NTRK)
        return 0;
    if (na > 1u && ed_sound_in(a[0], a + 1, na - 1u))
        return 0;
    ed_sound_out(a[0]);
    return 1;
}
