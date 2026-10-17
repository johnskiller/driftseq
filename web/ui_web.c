/* ui_web.c — DriftSeq UI as a wasm module (batch 2): the SAME app layer that
 * runs on the FM-1 (firmware/src/ui_*.c painters + routing + engine), with a
 * browser platform shim. JS = shell only: it injects panel events, calls
 * ds_ui_frame(dt), and blits the 240x240 RGB565 framebuffer to the canvas.
 *
 * Compile (make uiwasm): emcc web/ui_web.c host/*.c firmware/src/gfx.c
 * (NOT the app.c unity root — the browser replaces lcd/usb/panel/HAL.)
 * App layer: Copyright (C) 2026 johnskiller, GPL-3.0-only.
 */
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <emscripten/emscripten.h>
#include "driftseq.h"
#include "demo_pattern.h"

/* ---- felucca gfx body (renderer + fonts), shared with the firmware root ----
 * lcd prototypes first (gfx_body.h calls them; the firmware root gets them
 * from lcd.c — here from the shim below). */
static void lcd_init(void);
static void lcd_sync(void);
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c);
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px);
#include "gfx_body.h"

/* ---- lcd shim for the browser: painters blit into one persistent fb ---- */
static uint16_t fb[240 * 240];
static int fb_dirty;
static void lcd_init(void) { }
static void lcd_sync(void) { }
static void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t r, i;
    if (x >= 240u || y >= 240u) return;
    if (x + w > 240u) w = 240u - x;
    if (y + h > 240u) h = 240u - y;
    for (r = 0; r < h; r++)
        for (i = 0; i < w; i++)
            fb[(y + r) * 240u + x + i] = c;
    fb_dirty = 1;
}
static void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    uint32_t r, i;
    if (x >= 240u || y >= 240u || !w || !h) return;
    if (x + w > 240u) w = 240u - x;
    if (y + h > 240u) h = 240u - y;
    for (r = 0; r < h; r++)
        for (i = 0; i < w; i++)
            /* cv_px is pre-swapped for the big-endian panel (real lcd.c sends
             * it raw); the browser fb is host-order, so un-swap on the way in. */
            fb[(y + r) * 240u + x + i] = swap16(px[r * w + i]);
    fb_dirty = 1;
}

/* ---- panel matrix map (fm1-ctrl PANEL_DEFAULT, proven): label -> matrix id.
 * The web shell dispatches by LABEL (panel.js B_* / EN names), so it never
 * needs the matrix layer; the firmware keeps panel.c for calibration.
 * DS_WEB_PANEL_SHIM gives ui.c a compatible panel stand-in. ---- */
enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC, B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
#define DS_WEB_PANEL_SHIM 1
static const struct { uint8_t btn[NB]; uint8_t enc[NE]; } panel = {
    {2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 0, 1},
    {0, 1, 6, 2, 3, 4, 5},
};

/* ---- input: JS injects logical events directly (ui_input.c firmware front-
 * end is excluded via DS_WEB_UI). Buttons = B_* enums, encoders = EN_* enums,
 * keys = 0..26 (F3..G5). Edges are coalesced by the JS shell (panel.js). ---- */
static void ui_input_poll(void) { }               /* no-op: events are push, not polled */

/* ---- the app layer (identical files to the firmware) ---- */
/* ---- the app layer (identical files to the firmware) ---- */
ds_state g;                                       /* engine global (host/driftseq.c defines it in the other TU) */
#if 0                                 /* libc.c memset/memcpy clash with real libc in wasm */
#include "libc.c"                     /* fmt_int / fmt_fix (unity: once, first) */
#else
#include "libc_fmt.h"
#endif
#include "ui_glue.c"
#include "ui_tables.c"
#include "ui_text.c"
#include "ui.c"
#include "ui_screens.c"
#include "ui_edit.c"

/* ---- exported: lifecycle ---- */
EMSCRIPTEN_KEEPALIVE
void ds_ui_boot(void)
{
    pal[0] = RGB(34, 34, 40); pal[1] = RGB(74, 74, 82); pal[2] = RGB(150, 150, 158);
    pal[3] = RGB(200, 200, 90); pal[4] = RGB(80, 220, 120);   /* DriftSeq theme */
    ui_init();
    ds_state_init();
    ui_draw();
}

EMSCRIPTEN_KEEPALIVE
void ds_ui_frame(double dt)
{
    ds_tick(&g, dt);
    ui_draw();
}

/* ---- exported: panel injection (logical ids; JS maps hardware/panel) ----
 * GLO is the modifier: track it here so glo_held matches the emu semantics
 * (down on press, up on release) before the page handlers see events. */
EMSCRIPTEN_KEEPALIVE
void ds_web_btn(uint8_t btn, int down)             /* down=1 press, 0 release */
{
    if (btn == B_GLO) {
        glo_held = down ? 1 : 0;
        ui_button(btn, (uint8_t)(down != 0), (uint8_t)(down == 0));
        return;
    }
    ui_button(btn, (uint8_t)(down != 0), (uint8_t)(down == 0));
}
EMSCRIPTEN_KEEPALIVE
void ds_web_enc(uint8_t enc, int8_t det)
{
    int n = det;                                     /* split into per-detent events:
        the handlers are written for ±1 (fw front-end splits too); SELECT moves
        one track per detent, K1 cursor one step per detent, etc. */
    while (n > 0) { ui_encoder(enc, 1); n--; }
    while (n < 0) { ui_encoder(enc, -1); n++; }
}
EMSCRIPTEN_KEEPALIVE
void ds_web_key(uint8_t key, int down)
{
    if (down) panel_key_handler(key, 1);
    else panel_key_up(key);
}

/* ---- exported: framebuffer access (240x240 RGB565, byte-swapped) ---- */
EMSCRIPTEN_KEEPALIVE
uint8_t *ds_web_fb(void) { return (uint8_t *)fb; }
EMSCRIPTEN_KEEPALIVE
uint8_t *ds_web_out(void) { return g.out; }        /* MIDI byte stream */
EMSCRIPTEN_KEEPALIVE
uint32_t ds_web_out_len(void) { return g.out_len; }
EMSCRIPTEN_KEEPALIVE
void ds_web_out_clear(void) { g.out_len = 0; }
