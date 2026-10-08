/* SPDX-License-Identifier: GPL-3.0-only */
/* Jangada: the layers (firmware/src/ui_layers.c) on the host, with the UI code and a framebuffer in
 * place of the LCD: the SEQ layer's step keys, pattern tools and undo / redo, the ENGINE keys, and
 * every layer's screen draws; and the page columns (ui_draw.c draw_column, Inter Tight on cards): no
 * label, value or unit of any page, engine or value is cut to fit its card.
 * Build: cc -Ibuild/gen -Ifirmware/src tests/layers_test.c -lm */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
static int ncut;
#define UI_HOOK_CUT(src, maxw) (ncut++ < 20 ? printf("cut to %d px: '%s'\n", (int)(maxw), (src)) : 0)
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
static struct { volatile uint32_t notes, buttons; } fm1_in;
static volatile uint32_t fm1_ms;
static uint16_t FB[240*240];
static void lcd_sync(void){}
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c){for(uint32_t j=y;j<y+h&&j<240;j++)for(uint32_t i=x;i<x+w&&i<240;i++)FB[j*240+i]=(uint16_t)((c>>8)|(c<<8));}
static void lcd_blit(uint32_t x,uint32_t y,uint32_t w,uint32_t h,const uint16_t*p){for(uint32_t j=0;j<h;j++)for(uint32_t i=0;i<w;i++)if(y+j<240&&x+i<240)FB[(y+j)*240+x+i]=p[j*w+i];}
static void fm1_delay_ms(uint32_t ms){(void)ms;}
static uint32_t fm1_ticks(void){return fm1_ms*1000;}
static void fm1_wdt_feed(void){}
static void fm1_irq_off(void){} static void fm1_irq_on(void){}
static uint8_t ledstate[64];
static void fm1_led_key(uint32_t k,int on){if(k<64)ledstate[k]=on;}
static int32_t enc_q[32];                          /* detents injected per encoder (enc_turn) */
static int32_t fm1_enc_take(uint32_t i){int32_t v=enc_q[i&31];enc_q[i&31]=0;return v;}
static uint32_t fm1_input_edges(int x){(void)x;return 0;}
static uint32_t fm1_input_note_edges(void){return 0;}
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
#include "upreset.c"
#include "project.c"

/* power-on: three parts with their default sounds (TRK_DEF), the drum track, empty patterns */
static void felucca_init(void)
{
    uint32_t i;
    fm6_init();                                 /* Jangada: every track the FM6 init voice */
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        if (i < NPART) {
            set_engine_of(t, TRK_DEF[i][0]);
            apply_preset_to(t, TRK_DEF[i][1]);   /* with its sends */
            t->engine = t->eng_req;
        }
        track_defaults_steps(t);              /* the sequencers start empty */
    }
    song.sel = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}
#include <assert.h>

static int notes(track_t*t,int i){return t->step[i].n?t->step[i].note[0]:0;}
int main(void){
 felucca_init(); track_t*t=&trk[0]; t->p[P_SLEN]=4;
 for(int i=0;i<4;i++){t->step[i].note[0]=60+i;t->step[i].n=1;t->step[i].time=ST_NOTE;}
 ly.snap=0; pattern_tool(1);  /* shift > */
 assert(notes(t,0)==63&&notes(t,1)==60);
 pattern_tool(3); assert(t->p[P_SLEN]==8&&notes(t,4)==63); /* same hold: same snapshot */
 undo_swap(0); assert(t->p[P_SLEN]==4&&notes(t,0)==60&&notes(t,3)==63);
 undo_swap(1); assert(t->p[P_SLEN]==8&&notes(t,0)==63);
 undo_swap(1); /* nothing to redo */
 ly.snap=0; pattern_tool(5); assert(notes(t,0)==64);
 pattern_tool(2); assert(t->p[P_SLEN]==4);
 undo_swap(0); assert(notes(t,0)==63&&t->p[P_SLEN]==8);
 /* step keys: down on empty sets, down+up on set clears */
 ly.snap=0; ly.page=0; last_note=50; t->step[6].n=0; t->step[6].time=ST_REST;
 layer_key(LY_STEP,key_of_white(6),1,0); assert(notes(t,6)==50);
 layer_key(LY_STEP,key_of_white(6),0,0); assert(notes(t,6)==50);  /* just set: stays */
 layer_key(LY_STEP,key_of_white(6),1,0); layer_key(LY_STEP,key_of_white(6),0,0); assert(!t->step[6].n);
 /* held + knob: edited, kept */
 layer_key(LY_STEP,key_of_white(1),1,0); steps_held_edit(1,1); layer_key(LY_STEP,key_of_white(1),0,0);
 assert(t->step[1].n && ((t->step[1].flags&SF_RATCH)>>SF_RATCH_SH)==1);
 { /* after SLOOP 2.4, steps held: SELECT nudges, ALGORITHM picks, PRESETS locks, OCT+ the condition, OCT- clears;
    * SHIFT moves the locks with their steps, undo brings them back; GLO keys 9 / 10: the fill */
   #define enc_turn(role, v) (enc_q[panel.enc[role] & 31] = (v) * panel.dir[role])
   uint32_t k2; int kk, base;
   panel = PANEL_DEFAULT; ly.snap=0; ly.page=0; ly.held=0; t->p[P_SLEN]=8;
   layer_key(LY_STEP,key_of_white(2),1,0); assert(ly.held==1u<<2 && step_on(&t->step[2]));
   ly.lkp=P_LEVEL; base=p_unlocked(t,P_LEVEL);
   enc_turn(EN_PRESET,3); steps_held_encs(); kk=lock_find(t,2,P_LEVEL,0);
   assert(kk>=0 && t->x.lock[kk].val>base && t->x.lock[kk].val<=base+30 && t->p[P_LEVEL]==base);   /* a lock, not the base */
   enc_turn(EN_SELECT,-5); steps_held_encs(); assert(step_micro(t,2)==-5);
   enc_turn(EN_ALGO,1); steps_held_encs(); assert(ly.lkp!=P_LEVEL && p_lockable(t,ly.lkp));
   enc_turn(EN_ALGO,-1); steps_held_encs(); assert(ly.lkp==P_LEVEL);
   ly.lkp=P_SDIV; assert(lock_cur(t)!=P_SDIV);                                     /* not lockable: another one */
   ly.lkp=P_LEVEL;
   steps_held_oct(1); assert(step_cond(t,2)==FC_FILL); steps_held_oct(1); assert(step_cond(t,2)==FC_NOFILL);
   ly.btn=LY_STEP; ly.t0=0; fm1_ms=1000; ly.shown=0; ui.force=1; ui_draw(); assert(ly.shown); ly.btn=0;   /* the title, the marks */
   layer_key(LY_STEP,key_of_white(2),0,0); assert(step_on(&t->step[2]) && step_marked(t,2));   /* edited: kept */
   layer_key(LY_STEP,key_of_white(2),1,0); steps_held_oct(0);
   assert(lock_find(t,2,P_LEVEL,0)<0 && !step_micro(t,2) && step_cond(t,2)==FC_NOFILL);
   layer_key(LY_STEP,key_of_white(2),0,0);
   ly.snap=0; lock_set(t,1,P_REV,77); step_micro_set(t,1,9); pattern_tool(1);    /* SHIFT > */
   assert(lock_find(t,2,P_REV,0)>=0 && lock_find(t,1,P_REV,0)<0 && step_micro(t,2)==9);
   undo_swap(0); assert(lock_find(t,1,P_REV,0)>=0 && step_micro(t,1)==9);
   ly.snap=0; pattern_tool(3); assert(t->p[P_SLEN]==16 && lock_find(t,9,P_REV,0)>=0 && step_micro(t,9)==9);   /* LEN x2 */
   ly.snap=0; t->step[1].n=1; t->step[1].time=ST_NOTE;
   layer_key(LY_STEP,key_of_white(1),1,0); layer_key(LY_STEP,key_of_white(1),0,0);   /* a set step cleared: */
   assert(!t->step[1].n && lock_find(t,1,P_REV,0)<0 && !step_micro(t,1));           /* its lock and nudge too */
   layer_key(LY_MIX,key_of_white(8),1,0); assert(fill_held);
   layer_key(LY_MIX,key_of_white(8),0,0); assert(!fill_held);
   song.playing=1; layer_key(LY_MIX,key_of_white(9),1,0); assert(fill_arm);
   layer_key(LY_MIX,key_of_white(9),0,0); layer_key(LY_MIX,key_of_white(9),1,0); assert(!fill_arm);
   song.playing=0; fill_arm=0;
   for (k2 = 0; k2 < 4u; k2++) { ly.btn=(uint8_t)LY_MIX; ly.shown=0; ui.force=1; ui_draw(); } ly.btn=0; ui_draw();
   ly.snap=0; fm1_irq_off(); track_defaults_steps(t); fm1_irq_on(); assert(lock_find(t,9,P_REV,0)<0);
   t->p[P_SLEN]=8; ly.held=0;
   printf("%-46s ok\n", "layers: SEQ nudge, locks, conditions; GLO fill");
 }
 { /* 0.7: the visualiser: HOME on the TRACKS screen opens it, SELECT its style, every style draws in every
    * palette, HOME then goes HOME; another page closes it */
   uint32_t st, p, f;
   ly.btn = ly.lock = 0; ui.menu = 0; ui.confirm = 0;
   open_family(FAM_TRK); ui.home = 0; assert(cur_page()->scope == SC_TRK && !vis_shown());
   vis_open(); assert(vis_shown());
   for (p = 0; p < NPALETTES; p++) for (st = 0; st < VIS_N; st++) {
     palette_set(p); vis_style = (uint8_t)st;
     for (f = 0; f < 4u; f++) { ui.force = (f == 0); ui_draw(); }
   }
   st = vis_style; vis_select(1); assert(vis_style == (st + 1u) % VIS_N);
   vis_on = 0; go_home(); ui_draw(); assert(!vis_shown());
   palette_set(5);
   printf("%-46s ok\n", "visualiser: opens, styles, palettes, closes");
 }
 /* engine layer */
 layer_key(LY_ENGINE,key_of_white(6),1,0); assert(t->eng_req==6);
 printf("%-46s ok\n", "layers: SEQ tools, undo / redo, step keys");
 printf("%-46s ok\n", "layers: ENGINE keys pick the engine");
 { /* Jangada DRONES: a preset sets EVOL / TENS / RAMP, the next one puts them back (they sit after P_E15) */
   track_t *u = &trk[1]; uint32_t k, f = 0;
   set_engine_of(u, 0);
   for (k = 0; k < ENGINES[0]->npresets; k++) if (!strcmp(ENGINES[0]->presets[k].name, "FERRUGEM")) f = k;
   apply_preset_to(u, f); assert(f && u->p[P_EVOL] == 90 && u->p[P_TENS] == 110 && u->p[P_TRAMP] == 5);
   apply_preset_to(u, 0); assert(!u->p[P_EVOL] && !u->p[P_TENS] && !u->p[P_TRAMP]);
   u->p[P_EVOL] = 50; track_defaults(u); assert(!u->p[P_EVOL]);
   u->p[P_TFLT] = -30; apply_preset_to(u, 1); assert(u->p[P_TFLT] == -30); u->p[P_TFLT] = 0;   /* FILT: the mix, kept */
   set_engine_of(u, TRK_DEF[1][0]); apply_preset_to(u, TRK_DEF[1][1]); u->engine = u->eng_req;
   printf("%-46s ok\n", "presets: EVOL TENS RAMP set and reset");
 }
 { /* every layer's screen draws, in every palette, with something on it; leaving it restores the page */
   uint32_t l, p, lit;
   for (p = 0; p < NPALETTES; p++) for (l = LY_FX; l < LY_COUNT; l++) {
     palette_set(p); ly.btn = (uint8_t)l; ly.lock = 0; ly.t0 = 0; fm1_ms = 1000; ly.shown = 0;
     memset(FB, 0, sizeof FB); ui.force = 1; ui_draw();
     for (lit = 0; lit < 240u * 240u && !FB[lit]; lit++) ;
     assert(lit < 240u * 240u && ly.shown);
   }
   ly.btn = 0; ui_draw(); assert(!ly.shown);
 }
 printf("%-46s ok\n", "layers: every screen draws, in every palette");
 { /* every page of every engine, every value (sampled) of every column: nothing cut */
   uint32_t e, pi, c;
   palette_set(5); ui.home = 0; song.sel = 0;
   for (e = 0; e < NENGINES; e++) {
     set_engine_of(&trk[0], e); trk[0].engine = trk[0].eng_req;
     for (pi = 0; pi < NPAGES; pi++) {
       const page_t *pg = &PAGES[pi];
       if (e && pg->scope != SC_ENGINE && pg->graph != GR_BROWSE) continue;
       ui.page = (uint8_t)pi;
       for (c = 0; c < 4u; c++) {
         int16_t *vp = 0, keep;
         const param_desc_t *d = pg->scope == SC_STEP || pg->scope == SC_TRK ? 0 : page_desc(pg, c, &vp);
         int32_t v, step;
         if (!d || !vp) { ui.force = 1; draw_columns(); continue; }
         keep = *vp; step = (d->max - d->min) / 300 + 1;
         for (v = d->min; v <= d->max; v += step) { *vp = (int16_t)v; ui.force = 1; draw_columns(); }
         *vp = keep;
       }
     }
   }
   ui.home = 1; ui.force = 1; draw_columns();
   assert(!ncut);
 }
 printf("%-46s ok\n", "columns: no label / value / unit cut (Inter Tight)");
 { /* menu LIGHTS / KEYS / NOTES (after SLOOP 2.3): which keys glow, which light; the settings word */
   uint32_t m, w; int n = 0;
   lights_lvl = 0; lights_keys = KEYS_C; assert(!lights_keys_mask());           /* KEYS needs LIGHTS */
   lights_lvl = LIGHTS_LOW; m = lights_keys_mask(); assert(m == (1u << 7 | 1u << 19));   /* C4, C5 */
   lights_keys = KEYS_WHITE; m = lights_keys_mask(); for (; m; m &= m - 1) n++; assert(n == 16);
   lights_notes = 1; usb_full = 1; lights_lvl = LIGHTS_HIGH; w = lights_word();
   lights_lvl = lights_keys = lights_notes = usb_full = 0; lights_from_word(w);
   assert(lights_lvl == LIGHTS_HIGH && lights_keys == KEYS_WHITE && lights_notes && usb_full);
   lights_from_word(0xFFFFFFFFu); assert(!lights_lvl && !lights_keys);       /* a damaged word: off */
   song.sel = 0; trk[0].p[P_VOICE] = V_POLY; song.octave = 0;
   trk_note_on(&trk[0], kb_map(&trk[0], 7), 100);                            /* a note sounding: its key */
   assert(keys_sounding(&trk[0]) == 1u << 7);
   trk_all_off(&trk[0]); assert(!keys_sounding(&trk[0]));
   lights_lvl = lights_keys = lights_notes = usb_full = 0;
 }
 printf("%-46s ok\n", "menu LIGHTS / KEYS / NOTES: keys, settings word");
 { /* MENU > NEW PROJECT while the song plays (Jangada): the transport stops and every note is let go at the
    * next block, no step index is left past the new pattern length, the track that changes engine fades its
    * old voices and switches (none of the old engine stays), every part back on its power-on sound */
   static int32_t out[2 * CTL]; uint32_t i, k, old1;
   felucca_init(); panel = PANEL_DEFAULT; song.sel = 0;
   set_engine_of(&trk[1], 3); trk[1].engine = trk[1].eng_req; old1 = trk[1].engine;   /* track 2 on LOFI */
   trk[0].p[P_SLEN] = 32;
   for (i = 0; i < 32u; i++) { trk[0].step[i].n = 1; trk[0].step[i].note[0] = 60; trk[0].step[i].time = ST_NOTE; }
   transport_req = 1;
   for (i = 0; i < 2800u; i++) mix_block(out, CTL);                   /* START, 2 s in: past step 16 */
   trk_note_on(&trk[1], 64, 100);                                     /* a key held on track 2 */
   assert(song.playing && trk[0].seq_idx >= 16u && trk[1].v[0].active);
   ui.menu = 1; ui.menu_sel = MI_NEW; menu_new_armed = 0;
   menu_input(1u << panel.btn[B_OCTUP]); assert(menu_new_armed && ui.menu == 1);
   menu_input(1u << panel.btn[B_OCTUP]);
   assert(!ui.menu && transport_req == 2 && panic_req == (1u << NTRK) - 1u && sync_reload);
   assert(trk[0].p[P_SLEN] == TP[P_SLEN].def && !trk[0].step[0].n && trk[1].eng_req == TRK_DEF[1][0]);
   assert(trk[1].engine == old1 && trk[1].v[0].active);               /* the old engine sounds until it faded */
   for (i = 0; i < NTRK; i++) assert(trk[i].seq_idx < (uint32_t)trk[i].p[P_SLEN]);
   for (i = 0; i < 40u; i++) mix_block(out, CTL);                     /* the ISR: stop, panic, the fade */
   assert(!song.playing && !transport_req && !panic_req);
   for (i = 0; i < NTRK; i++) {
     assert(trk[i].engine == trk[i].eng_req && trk[i].seq_idx < (uint32_t)trk[i].p[P_SLEN] && !trk[i].seq_n);
     for (k = 0; k < NVOICE; k++) assert(!trk[i].v[k].gate);          /* nothing held: released or gone */
   }
   for (k = 0; k < NVOICE; k++) assert(!trk[1].v[k].active);          /* LOFI's voices: faded and gone */
   trk_all_off(&trk[0]); ui.force = 1;
 }
 printf("%-46s ok\n", "menu NEW PROJECT while playing: stop, fade, reset");
 { /* GLO > KIT: the kits in the browser's order (Jangada's own first; G_KIT keeps its old indices), a kit's
    * beat into an empty or untouched drum track (not into the user's), KNOB 4 BEAT (twice over the user's) */
   uint32_t pi; const track_t *td = TDRUM;
   felucca_init(); ui.home = 0; song.sel = TRK_DRUM;
   for (pi = 0; pi < NPAGES && strcmp(PAGES[pi].title, "KIT"); pi++) ;
   assert(pi < NPAGES && PAGES[pi].id[3] == PG_BEAT); ui.page = (uint8_t)pi;
   song.g[G_KIT] = 0; track_defaults_steps(TDRUM);
   edit_param(0, 1); assert(!strcmp(DRUM_KIT_NAMES[drum_kit()], "RUST") && drum_kit() == DRUM_KITS - 5u);
   assert(drums.beat && !strcmp(DS_BEATS[drums.beat - 1].name, "GRIND") && td->step[0].n == 2 && td->step[0].note[0] == 36);
   edit_param(0, 1); assert(!strcmp(DRUM_KIT_NAMES[drum_kit()], "FORGE") && !strcmp(DS_BEATS[drums.beat - 1].name, "ANVIL"));
   TDRUM->step[1].n = 1; TDRUM->step[1].note[0] = 49; TDRUM->step[1].time = ST_NOTE;   /* the user's now */
   edit_param(0, 1); assert(!strcmp(DRUM_KIT_NAMES[drum_kit()], "PISTON") && td->step[1].note[0] == 49);
   ui.arm = 0; edit_param(3, 1); assert(ui.arm == PG_BEAT && td->step[1].note[0] == 49);   /* armed, kept */
   edit_param(3, 1); assert(!strcmp(DS_BEATS[drums.beat - 1].name, "ENGINE") && td->step[1].note[0] == 42);
   edit_param(0, 1); edit_param(0, 1); assert(!strcmp(DRUM_KIT_NAMES[drum_kit()], "MANGUE"));
   assert(!strcmp(DS_BEATS[drums.beat - 1].name, "MARACATU"));
   edit_param(0, 1); assert(drum_kit() == 1u && !strcmp(DS_BEATS[drums.beat - 1].name, "MARACATU"));   /* 808: no beat of its own */
   song.g[G_KIT] = 1; edit_param(0, -1); assert(!strcmp(DRUM_KIT_NAMES[drum_kit()], "MANGUE"));
   song.g[G_KIT] = 0; edit_param(0, -1); assert(drum_kit() == 0u);                     /* the knob stops at GM */
   song.t4 = 1; edit_param(3, 1); assert(!strcmp(DS_BEATS[drums.beat - 1].name, "MARACATU")); song.t4 = 0;
   song.sel = 0;
 }
 printf("%-46s ok\n", "GLO > KIT: browser order, kit beats, BEAT (Jangada)");
 { /* Jangada 0.8.1 (issue #2): the preset categories. Every factory preset has one; KNOB 2 on the PRESETS page (0.8.2; KNOB 4 in 0.8.1)
    * picks it and KNOB 1 / KNOB 3 walk only its presets, across the engines; ALL is the list as before */
   uint32_t e, k, pi, total, cur, n, nb = 0, c, eng0;
   for (e = 0; e < NENGINES; e++)
     for (k = 0; k < ENGINES[e]->npresets; k++)
       if (!ENGINES[e]->presets[k].cat || ENGINES[e]->presets[k].cat >= PC_COUNT) {
         printf("no category: %s %s\n", ENGINES[e]->name, ENGINES[e]->presets[k].name); return 1; }
   for (pi = 0; pi < NPAGES && PAGES[pi].graph != GR_BROWSE; pi++) ;
   assert(pi < NPAGES); ui.page = (uint8_t)pi; song.sel = 0; ui.pcat = 0;
   assert(!is_drum(TSEL));
   cur = preset_pos(&total);
   edit_param(0, 1); assert(preset_pos(&total) == (cur + 1u) % total);           /* ALL: one by one, as before */
   for (n = 0; n < total; n++) nb += preset_cat(n) == PC_BASS;
   edit_param(1, 1); assert(ui.pcat == PC_BASS && preset_cat(preset_pos(&total)) == PC_BASS);   /* moved into BASS */
   { uint32_t start = preset_pos(&total), steps = 0;                             /* a full turn: every BASS once */
     do { edit_param(0, 1); steps++; assert(preset_cat(preset_pos(&total)) == PC_BASS); } while (preset_pos(&total) != start && steps < 500u);
     assert(steps == nb); }
   edit_param(0, -1); assert(preset_cat(preset_pos(&total)) == PC_BASS);           /* backwards too */
   eng0 = TSEL->eng_req; c = 0;
   for (n = 0; n < 12u; n++) { edit_param(2, 1); assert(preset_cat(preset_pos(&total)) == PC_BASS); c += TSEL->eng_req != eng0; }
   assert(c > 0);                                                                  /* KNOB 3: engine to engine, BASS only */
   edit_param(2, -1); assert(preset_cat(preset_pos(&total)) == PC_BASS);
   for (n = 0; n < PC_COUNT - 1u; n++) edit_param(1, 1);                           /* round to ALL */
   assert(ui.pcat == 0);
   cur = preset_pos(&total); edit_param(0, 1); assert(preset_pos(&total) == (cur + 1u) % total);
   for (n = 0; n < PC_COUNT - 1u; n++) edit_param(1, -1);                          /* back past FX .. to BASS */
   assert(ui.pcat == PC_BASS && preset_cat(preset_pos(&total)) == PC_BASS);
   ui.pcat = 0;
 }
 printf("%-46s ok\n", "PRESETS: categories, the filter (Jangada 0.8.1)");
 return 0;}
