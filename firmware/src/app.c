/* app.c — DriftSeq unity root for FM-1 (v1 scope batch 1).
 * App layer: Copyright (C) 2026 johnskiller, GPL-3.0-only.
 *
 * ONE compilation unit, Felucca/x0x layout (ctrl.c is the template): platform
 * sources #included in strict order; statics stay file-local. Include order:
 *   HAL headers → libc/lcd/gfx → usb/ota (+ glue shims) → panel →
 *   input (keyscan/encoders/leds/midi_io/ctrl_glue), the engine in host/,
 *   unchanged and golden-pinned, then ui.c (screens), app_main.c (boot + loop).
 */
#include <stdint.h>
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_guard.h"
#include "fm1_input.h"
#include "fm1_timer.h"
#include "fm1_lcd_hw.h"
#include "fm1_flash.h"
#include "fm1_perf.h"
#include "fm1_cpu1.h"            /* fm1_cpu1_halt() — dual-core bring-up (crt0 step) */

#include "libc.c"
#include "lcd.c"
#include "gfx.c"

static volatile uint32_t fm1_ms;
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")

/* FELUCCA_OTA must be set BEFORE usb.c: that file guards the SysEx frame
 * plumbing (ota_wire_send / ota_frame_get / ota_frame_done, used by ota.c and
 * by the map SysEx path) with #if FELUCCA_OTA. Setting it after the include
 * left those three as declarations only — the link failed with
 * "internal linkage but is not defined". */
#define FELUCCA_OTA 1
#include "usb.c"
#ifndef FELUCCA_ID
#define FELUCCA_ID "FM-1_91001"          /* DriftSeq identity (plan b42a5eb: avoids
                                          * Polly FM-1_3\d{6} / sloop _900 / bubba _003) */
#endif
#include "ota.c"
static uint32_t ota_now_ms(void) { return fm1_ms; }
static void ota_idle(void) { fm1_wdt_feed(); }
static int ota_in_area(uint32_t off, uint32_t n) { return FL_IN(off, n, OTA_AREA, OTA_AREA + OTA_AREA_LEN); }
static int ota_erase(uint32_t off)
{
    uint32_t took;
    if (!ota_in_area(off, 0x1000u) || (off & 0xFFFu))
        return -8;
    return fl_erase4k(off, &took);
}
static int ota_prog(uint32_t off, const void *p, uint32_t n)
{
    if (!ota_in_area(off, n))
        return -8;
    return fl_write(off, p, n);
}
static int ota_fread(uint32_t off, void *p, uint32_t n) { return fl_read_ram(off, p, n); }
static void ota_show(uint32_t step, int32_t code)
{
    static const char *const STEP[] = {"", "PACKAGE", "CHECK HEAD", "LOADER", "CONFIRM", "RESTART"};
    lcd_fill(0, 0, 240, 240, C_BLACK);
    draw_text_box(0, 92, 240, &FONT_S, "UPDATE", C_WHITE, 1);
    if (step < 9u) {
        draw_text_box(0, 124, 240, &FONT_S, STEP[step < 6u ? step : 0], C_HI, 1);
        return;
    }
    draw_text_box(0, 124, 240, &FONT_S, code == 1 ? "DRY RUN OK" : "FAILED", C_HI, 1);
    fm1_delay_ms(1500);
}
static void ota_commit(const uint8_t *parm)
{
    usb_detach();
    fm1_delay_ms(30);
    fm1_enter_update(parm);
}
static void fm1_fault(const fm1_crash_t *c)
{
    char b[9];
    uint32_t i;
    lcd_fill(0, 0, 240, 240, RGB(160, 0, 0));
    draw_text_box(0, 8, 240, &FONT_S, "DRIFTSEQ CRASH", C_WHITE, 1);
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(c->pc >> (28u - 4u * i)) & 15u];
    b[8] = 0;
    draw_text_box(10, 60, 220, &FONT_S, b, C_WHITE, 0);
    { uint32_t t0 = fm1_ticks();
      while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US) ; }
    fm1_reboot();
}

#include "plat_ctrl.h"
#include "panel.c"
#include "plat_ctrl.c"               /* midi ring accessors over usb.c statics */

#include "../keyscan.c"
#include "../encoders.c"
#include "../leds.c"
#include "../map.c"                  /* DriftSeq map: values ARE the map (map.h) */
#include "../midi_io.c"
#include "ctrl_glue.c"               /* ctrl_midi_in3 + transport hook */

/* ---- engine: pure C, compiled unchanged (golden 420C4824C85B79CB) ----
 * g is the single engine instance: on web it lives in ui_web.c, on hardware
 * here (the unity root owns it, like fm1-ctrl's ctrl.c owns its globals). */
#include "../host/driftseq.h"
ds_state g;
#include "../host/driftseq.c"
#include "../host/demo_pattern.c"

/* ---- app: screens + boot + main loop ---- */
#define DS_WEB_PANEL_SHIM 1          /* panel.c above already defines `panel` */
#include "ui_glue.c"                 /* forward decls first (order independence) */
#include "ui_tables.c"               /* GM_DRUMS / GM_SHORT (web root includes it too) */
#include "ui_text.c"
#include "ui.c"
#include "ui_input.c"
#include "ui_screens.c"
#include "ui_edit.c"
#include "app_main.c"
