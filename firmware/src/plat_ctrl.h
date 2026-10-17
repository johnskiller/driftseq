/* fm1-ctrl's trimmed plat.h: only what panel.c needs (button/encoder enums,
 * OBJ_PANEL, store stubs). v1 note: calibration is not persisted (store stub
 * returns -1) — panel runs on PANEL_DEFAULT, which matches every stock FM-1.
 * App layer: Copyright (C) 2026 johnskiller, GPL-3.0-only. */
#ifndef PLAT_CTRL_H
#define PLAT_CTRL_H
#include <stdint.h>

enum { B_FX, B_SEL, B_ENV, B_LFO, B_EDIT, B_GLO, B_HOME, B_SAVE, B_ARP, B_SEQ, B_PLAY, B_REC,
       B_OCTDN, B_OCTUP, NB };
enum { EN_SELECT, EN_ALGO, EN_PRESET, EN_K1, EN_K2, EN_K3, EN_K4, NE };
enum { OBJ_SET, OBJ_SOUND, OBJ_PAT0, OBJ_PAT1, OBJ_PANEL, OBJ_SONG, OBJ_MOTION, OBJ_NOBJ };

static int plat_store_load(uint32_t obj, void *dst, uint32_t max) { (void)obj; (void)dst; (void)max; return -1; }
static int plat_store_save(uint32_t obj, const void *src, uint32_t len) { (void)obj; (void)src; (void)len; return -1; }

/* plat.h's MIDI contract (plat_fm1.c provides these on x0x; our glue calls them) */
int plat_midi_in(uint32_t *pkt);
void plat_midi_out(uint32_t pkt);
#endif
