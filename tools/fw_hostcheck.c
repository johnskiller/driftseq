/* tools/fw_hostcheck.c — host-side compile + behavior check of the FM-1 app
 * layer (firmware/src/ui_*.c) against stubs of the Felucca platform surface.
 * Catches C errors and interaction-logic bugs without the JieLi toolchain.
 * Build: gcc -Wall -O1 -I host -I firmware -I firmware/src tools/fw_hostcheck.c -o /tmp/fw_hostcheck
 * The engine (host/driftseq.c + demo_pattern.c) is compiled UNCHANGED — the
 * same files the firmware uses. 21 behavioral checks, see main(). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "driftseq.h"

/* ---- platform surface stubs (gfx.c / libc.c / fm1_input.h) ---- */
#define RGB(r,g,b) ((uint16_t)((((r)>>3)<<11)|(((g)>>2)<<5)|((b)>>3)))
#define C_BLACK 0
#define C_WHITE 0xFFFF
#define C_DIM RGB(74,74,82)
#define C_GRAY RGB(150,150,158)
#define C_AMB RGB(200,200,90)
#define C_HI RGB(80,220,120)
typedef struct { uint8_t h,pad,first,last; const uint8_t *adv,*bw; const uint16_t *off; const uint8_t *data; } felucca_font_t;
static const felucca_font_t FONT_T = {12,0,32,95,0,0,0,0}, FONT_XS = {15,0,32,126,0,0,0,0},
                           FONT_S = {16,0,32,126,0,0,0,0}, FONT_M = {24,0,32,95,0,0,0,0};
static uint16_t cv_px[240*240];
static uint32_t cv_w = 240, cv_h = 240; static int32_t cv_oy, cv_y0, cv_y1 = 240;
static void cv_begin(uint32_t w, uint32_t h, uint16_t bg){ uint32_t i; cv_w=w; cv_h=h; cv_oy=cv_y0=0; cv_y1=(int32_t)h; for(i=0;i<w*h;i++) cv_px[i]=bg; }
static void cv_blit(uint32_t x, uint32_t y){ (void)x;(void)y; }
static void cv_rect(int32_t x,int32_t y,int32_t w,int32_t h,uint16_t c){ (void)x;(void)y;(void)w;(void)h;(void)c; }
static void cv_pset(int32_t x,int32_t y,uint16_t c){ (void)x;(void)y;(void)c; }
static void cv_line(int32_t x0,int32_t y0,int32_t x1,int32_t y1,uint16_t c){ (void)x0;(void)y0;(void)x1;(void)y1;(void)c; }
static void cv_disc(int32_t cx,int32_t cy,int32_t r,uint16_t c){ (void)cx;(void)cy;(void)r;(void)c; }
static int32_t cv_text(int32_t x, int32_t y, const felucca_font_t *f, const char *s, uint16_t c){ (void)f;(void)c;(void)y; while(*s++)x+=6; return x; }
static void lcd_fill(uint32_t x,uint32_t y,uint32_t w,uint32_t h,uint16_t c){(void)x;(void)y;(void)w;(void)h;(void)c;}
static void draw_text_box(uint32_t x,uint32_t y,uint32_t w,const felucca_font_t *f,const char *s,uint16_t c,int a){(void)x;(void)y;(void)w;(void)f;(void)s;(void)c;(void)a;}
static void fmt_int(char *b, int32_t v){ sprintf(b, "%d", (int)v); }

#define FM1_NKEY 41u
#define FM1_NENC 7u
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
static volatile struct {
    uint32_t notes, buttons, pressed, released, notes_pressed;
    uint8_t raw[18], cnt[FM1_NKEY];
    uint8_t enc_prev[FM1_NENC], enc_last[FM1_NENC], enc_rest[FM1_NENC], enc_home[FM1_NENC];
    uint8_t enc_sth[FM1_NENC], enc_stc[FM1_NENC], enc_still[FM1_NENC];
    int8_t enc_sub[FM1_NENC], enc_dir[FM1_NENC];
    int16_t enc_steps[FM1_NENC];
    uint32_t frames;
} fm1_in;
static uint32_t fm1_input_edges(uint32_t *released){ uint32_t p=fm1_in.pressed; if(released)*released=fm1_in.released; fm1_in.pressed=fm1_in.released=0; return p; }
static uint32_t fm1_input_note_edges(void){ uint32_t p=fm1_in.notes_pressed; fm1_in.notes_pressed=0; return p; }
static int32_t fm1_enc_take(uint32_t e){ int32_t v=fm1_in.enc_steps[e]; fm1_in.enc_steps[e]=0; return v; }
typedef struct { uint32_t magic; uint8_t btn[14]; uint8_t enc[7]; int8_t dir[7]; } panel_t;
static panel_t panel = {0x50414E35u,{2,3,4,5,6,7,8,9,10,11,12,13,0,1},{0,1,6,2,3,4,5},{1,1,1,1,1,1,1}};

/* midi transport stub */
typedef void (*midi_transport_fn)(uint8_t st, uint8_t d1, uint8_t d2);
static midi_transport_fn midi_transport_send;
static void midi_transport_usb3(uint8_t st, uint8_t d1, uint8_t d2){ (void)st;(void)d1;(void)d2; }
static int plat_midi_in(uint32_t *pkt){ (void)pkt; return 0; }

/* chord_name sanity (exercises the r16j rev.13 port): single = "C3", C-E-G
 * over C = "Cmaj", spell-out for unknown sets. The body runs AFTER the unity
 * includes (g/chord_name live below) — this is just the prototype. */
static void chord_name_test(void);
static int chord_name_result;

/* engine state: the app expects the symbol g (host/driftseq.c defines
 * ds_state as struct; wasm shim uses a pointer — host code takes &g) */
#include "driftseq.c"
#include "demo_pattern.c"
ds_state g;

/* ---- app layer, unity order ---- */
#include "../firmware/src/ui_glue.c"
#include "../firmware/src/ui_tables.c"
#include "../firmware/src/ui_text.c"
#include "../firmware/src/ui.c"
#include "../firmware/src/ui_input.c"
#include "../firmware/src/ui_screens.c"
#include "../firmware/src/ui_edit.c"

/* chord_name test body — g and chord_name are both visible here */
static void chord_name_test(void)
{
    ds_track *tr = &g.pat.track[3];
    char *cn;
    while (tr->steps[20].nvoices) ds_voice_remove(tr, 20, tr->steps[20].v[0].note);
    ds_voice_add(tr, 20, 48, 100, 4);            /* C3 */
    cn = chord_name(3, 20);
    if (strcmp(cn, "C3")) { chord_name_result = 0; return; }
    ds_voice_add(tr, 20, 52, 100, 4);            /* E3 -> 0,4 */
    ds_voice_add(tr, 20, 55, 100, 4);            /* G3 -> 0,4,7 */
    cn = chord_name(3, 20);
    if (strcmp(cn, "Cmaj")) { chord_name_result = 0; return; }
    ds_voice_add(tr, 20, 58, 100, 4);            /* A#3 -> unknown set */
    cn = chord_name(3, 20);
    if (strcmp(cn, "C E G A#")) { chord_name_result = 0; return; }
    ds_voice_remove(tr, 20, 52);
    ds_voice_remove(tr, 20, 58);
    cn = chord_name(3, 20);
    chord_name_result = !strcmp(cn, "C G");      /* power dyad spells out */
}


/* ---- extended asserts ---- */
static int fails;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); fails++; } else printf("ok: %s\n", msg); } while (0)

int main(void)
{
    uint8_t vi; int found;
    ds_state_init();
    ui_init();

    /* 1. boot: HOME mode, demo loaded (trk0 = kick E(16,4) with 36s) */
    CHECK(ui_mode == M_HOME, "boot mode HOME");
    CHECK(g.pat.track[0].channel == 9, "trk0 is drum");
    { int c = 0, s; for (s = 0; s < 64; s++) if (g.pat.track[0].steps[s].nvoices) c++; }
    CHECK(g.pat.track[0].steps[0].nvoices == 1 && g.pat.track[0].steps[0].v[0].note == 36, "trk0 st0 KCK");

    /* 2. SEQ -> GRID on drum track, black key selects drum lane */
    ui_button(B_SEQ, 1, 1);
    CHECK(ui_mode == M_GRID, "SEQ enters GRID");
    CHECK(is_drum_trk(), "trk0 drum grid");
    panel_key_handler(5, 1);               /* idx5 = note58 A#3 = black ordinal 2 */
    CHECK(drum_sel == 2, "black key idx5 (A#3) -> lane2 (CHH)");
    /* 2b. rev.22: only NDRUM=9 lanes exist. Black ordinals 9/10 are the two
     * reserved keys — they must NOT move the selection (silently landing on a
     * different drum than the printed key would be worse than doing nothing). */
    { uint8_t keep = drum_sel;
      panel_key_handler(20, 1);            /* idx20 = C#5 -> black ordinal 8, last lane */
      CHECK(drum_sel == NDRUM - 1, "black key ordinal 8 = last real lane (SHK)");
      panel_key_handler(22, 1);            /* idx22 = D#5 -> black ordinal 9 (reserved) */
      CHECK(drum_sel == NDRUM - 1, "reserved black key 9 keeps the selection");
      panel_key_handler(25, 1);            /* idx25 = F#5 -> black ordinal 10 (reserved) */
      CHECK(drum_sel == NDRUM - 1, "reserved black key 10 keeps the selection");
      drum_sel = keep; }

    /* 3. white key toggles the sel drum on its mapped step (key idx14 =
     *    note 67 G4 = white index 8 → page step 8; toggle there) */
    drum_sel = 0;                           /* KCK */
    { uint8_t s8 = TRK()->steps[8].nvoices; panel_key_handler(14, 1);
      CHECK(TRK()->steps[8].nvoices == s8 - 1, "white key (idx14->step8) removes KCK");
      panel_key_handler(14, 1);
      found = 0;
      for (vi = 0; vi < TRK()->steps[0].nvoices; vi++) if (TRK()->steps[0].v[vi].note == 36) found = 1;
      CHECK(found, "white key re-adds KCK"); }

    /* 4. cursor dead-stop (rev.9) — K1 = EN_K1 (3) after batch-2 role fix.
     * GLO+K1 used to be an x16 jump; rev.11 gave GLO+K1 to LOOP START (03
     * §2.2), so the same detent now moves the window instead of the cursor. */
    cursor = 0; ui_encoder(EN_K1, -1);
    CHECK(cursor == 0, "K1 left at st0 dead-stops");
    cursor = 63; ui_encoder(EN_K1, 1);
    CHECK(cursor == 63, "K1 right at st63 dead-stops");
    cursor = 0; { uint8_t s0 = TRK()->loop_start;
      glo_held = 1; ui_encoder(EN_K1, 1); glo_held = 0;
      CHECK(cursor == 0, "GLO+K1 no longer moves the cursor (rev.11)");
      CHECK(TRK()->loop_start == (uint8_t)(s0 + 1), "GLO+K1 = LOOP START (rev.11)");
      TRK()->loop_start = s0; }
    /* rev.22: GLO+K3 = DIVISION on the GRID, dead-stop at both ends.
     * demo tune track 0 is 1/16, so one detent down lands on 1/8T, not 1/8. */
    { uint8_t d0 = TRK()->division;
      glo_held = 1;
      ui_encoder(EN_K3, -1);
      CHECK(TRK()->division == DS_DIV_8T, "GLO+K3 down = 1/8T");
      ui_encoder(EN_K3, -1);
      CHECK(TRK()->division == DS_DIV_8, "GLO+K3 down = 1/8");
      ui_encoder(EN_K3, -1);
      CHECK(TRK()->division == DS_DIV_8, "GLO+K3 dead-stops at 1/8");
      ui_encoder(EN_K3, 1); ui_encoder(EN_K3, 1); ui_encoder(EN_K3, 1); ui_encoder(EN_K3, 1);
      CHECK(TRK()->division == DS_DIV_32, "GLO+K3 reaches 1/32");
      ui_encoder(EN_K3, 1);
      CHECK(TRK()->division == DS_DIV_32, "GLO+K3 dead-stops at 1/32");
      glo_held = 0;
      ui_encoder(EN_K3, 1);
      CHECK(TRK()->division == DS_DIV_32, "bare K3 is still MUTE, not DIV");
      TRK()->division = d0; }
    { uint8_t t0 = sel_track; ui_encoder(EN_SELECT, 1);
      CHECK(sel_track == t0 + 1, "SELECT encoder moves track");
      sel_track = 0; }                     /* tests below assume trk0 (drum) */

    /* 5. EDIT layer: drum VEL edit hits the right voice (r16f mapping) */
    cursor = 0; drum_sel = 0; ui_button(B_EDIT, 1, 1);
    { uint8_t v0 = TRK()->steps[0].v[0].vel;
      ui_encoder(EN_K1, -1);               /* K1 VEL -2 (127 is cap; test -1 detent) */
      CHECK(TRK()->steps[0].v[0].vel == v0 - 2, "EDIT K1 VEL -2 on drum");
      CHECK(focus_layer == 1, "EDIT toggles focus layer"); }
    ui_button(B_EDIT, 1, 1);               /* exit EDIT */
    CHECK(focus_layer == 0, "EDIT exits");

    /* 6. ARP FREE freeze (lesson 60): K1 on freed track = meta only */
    ui_button(B_ARP, 1, 1);
    CHECK(ui_mode == M_ARP, "ARP enters");
    CHECK(g.pat.track[0].eu_on == 1, "demo trk0 linked at boot");
    ui_arp_encoder(EN_K4, -1);             /* FREE */
    CHECK(g.pat.track[0].eu_on == 0, "K4 left = FREE");
    { uint8_t on3 = g.pat.track[0].steps[3].on, k0 = g.pat.track[0].eu_k;
      ui_arp_encoder(EN_K1, 1);
      CHECK(g.pat.track[0].eu_on == 0, "K1 on FREE track does NOT re-link");
      CHECK(g.pat.track[0].eu_k == k0 + 1, "K1 still updates meta");
      CHECK(g.pat.track[0].steps[3].on == on3, "pattern bits untouched while FREE"); }
    ui_arp_encoder(EN_K4, 1);              /* re-link */
    CHECK(g.pat.track[0].eu_on == 1, "K4 right re-links");

    /* 7. engine ticks deterministically (golden hash contract lives in wasmtest;
     * here: play produces output bytes on ch10 drum track) */
    ds_play(&g);
    { int i; unsigned first_st = 0; g.out_len = 0;
      /* accumulate everything, then scan: must contain note-on 0x99 (kick) */
      static uint8_t stream[16384]; unsigned slen = 0;
      for (i = 0; i < 2000; i++) {
        if (slen + g.out_len < sizeof stream) { memcpy(stream + slen, g.out, g.out_len); slen += g.out_len; }
        g.out_len = 0; ds_tick(&g, 0.001); }
      CHECK(slen > 0, "ds_tick produces MIDI bytes over 2000 ticks");
      first_st = 0;
      for (i = 0; i + 2 < (int)slen; i += 3)
        if ((stream[i] & 0xF0) == 0x90 && (stream[i] & 0x0F) == 9) { first_st = 1; break; }
      CHECK(first_st, "note-on 0x99 (ch10 kick) in stream");
    }

    /* 8. HOME button toggles HOME <-> GRID (emu parity, batch-2) */
    ui_button(B_HOME, 1, 1);               /* currently GRID -> HOME */
    CHECK(ui_mode == M_HOME, "HOME button returns to HOME");
    ui_button(B_HOME, 1, 1);               /* HOME -> GRID */
    CHECK(ui_mode == M_GRID, "HOME button re-enters GRID");

    /* 9. REC toggles the cursor step's gate bit (emu parity) */
    cursor = 5;
    { uint8_t on5 = TRK()->steps[5].on;
      ui_button(B_REC, 1, 1);
      CHECK(TRK()->steps[5].on == !on5, "REC toggles cursor step");
      ui_button(B_REC, 1, 1);
      CHECK(TRK()->steps[5].on == on5, "REC toggles back"); }

    /* 10. melodic key = live audition through the engine (ds_live_on) */
    ui_button(B_HOME, 1, 1);
    CHECK(ui_mode == M_HOME, "back to HOME for live keys");
    sel_track = 3;                          /* melodic track: keys = 53+key (r16j liveNote) */
    key_shift = 0;
    { int n0 = (int)g.live_n;
      panel_key_handler(0, 1);             /* key 0 = F3 = note 53 */
      CHECK((int)g.live_n == n0 + 1, "melodic key starts live note");
      CHECK(g.live[g.live_n - 1].note == 53 + key_shift, "live note = 53+key+shift");
      panel_key_up(0);
      CHECK((int)g.live_n == n0, "key up removes live note"); }
    { int n0 = (int)g.live_n;              /* drum track HOME keys = GM map (r16j) */
      sel_track = 0;
      panel_key_handler(0, 1);
      CHECK(g.live[g.live_n - 1].note == 36, "drum track live key = GM kick");
      panel_key_up(0);
      CHECK((int)g.live_n == n0, "drum live key up removes note"); }

    /* 10b. melodic GRID step entry: white key writes the cursor step,
     * revives it, advances cursor (dead-stop), and auditions */
    ui_button(B_SEQ, 1, 1);
    sel_track = 3;                          /* melodic demo track (bass) */
    cursor = 20;                            /* an empty step: override = pure write */
    panel_key_handler(2, 1);                /* key 2 = note 55 (G3) */
    CHECK(TRK()->steps[20].nvoices == 1 && TRK()->steps[20].v[0].note == 55,
          "melodic white key writes cursor step (override)");
    CHECK(TRK()->steps[20].on == 1, "step entry revives gate");
    CHECK(cursor == 21, "step entry advances cursor");
    panel_key_up(2);                        /* release auditions off, writes nothing */
    CHECK(TRK()->steps[20].v[0].note == 55, "key release keeps the written note");

    /* 10c. EDIT layer keys: add voice, same-note deletes, cursor never moves */
    focus_layer = 1;                        /* EDIT on */
    cursor = 20;
    panel_key_handler(4, 1);                /* key 4 = note 57 (A3) */
    CHECK(TRK()->steps[20].nvoices == 2, "EDIT key adds a voice");
    /* rev.28: a newly written voice holds ONE step, not four. Spec 01 §2 and
     * 03 §3 both say "vel 100 / gate 1". gate=4 meant a note on step 1 at
     * DIV 1/16 sustained for a whole beat (4 x 1/16), and the hero faithfully
     * drew a beat-long line — John: "在hero上，第一个beat都有横线". The hero was
     * right; the default was wrong. */
    CHECK(TRK()->steps[20].v[1].gate == 1,
          "EDIT-added voice defaults to gate 1 (one step)");
    CHECK(cursor == 20, "EDIT keys never advance cursor");
    panel_key_handler(4, 1);                /* same note again = delete */
    CHECK(TRK()->steps[20].nvoices == 1, "EDIT same-note deletes the voice");
    chord_name_test();
    CHECK(chord_name_result, "chord name single note + quality");
    focus_layer = 0;
    sel_track = 0;

    /* 11. draw calls smoke (no crash, all four screens) */
    ui_mode = M_HOME; ui_draw();
    ui_button(B_SEQ, 1, 1); ui_draw();
    { uint8_t t; for (t = 1; t < 5; t++) { sel_track = t; ui_draw(); } }
    ui_button(B_ARP, 1, 1); ui_draw();
    printf(fails ? "== %d FAILURES ==\n" : "ALL FW HOST CHECKS PASSED\n", fails);
    return fails ? 1 : 0;
}
