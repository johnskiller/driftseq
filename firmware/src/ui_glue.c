/* ui_glue.c — forward declarations + shared helpers (part of ui.c unity
 * section). Included FIRST among the ui files so every cross-reference is
 * declared; definitions follow in ui_tables.c / ui_text.c / ui.c /
 * ui_input.c / ui_screens.c / ui_edit.c. libc's fmt_int comes from libc.c.
 * btn_role/enc_role live in ui.c (panel.c statics visible there). */

/* screens (ui_screens.c) */
static void draw_home(void);
static void draw_grid_melodic(void);
static void draw_grid_drum(void);
static void draw_arp(void);
static void ui_draw_footer(void);

/* edit/readout/arp internals (ui_edit.c) */
static void ui_draw_edit_panel(void);
static void ui_draw_readout(void);
static void ui_draw_ring_onto_main(void);
static void ui_arp_encoder(uint8_t enc, int8_t det);
static void ui_edit_encoder(uint8_t enc, int8_t det);
static void ui_grid_encoder(uint8_t enc, int8_t det);
static void ui_home_encoder(uint8_t enc, int8_t det);

/* routing + lifecycle (ui.c / ui_edit.c / ui_input.c) */
static void ui_encoder(uint8_t enc, int8_t det);
static void ui_button(uint8_t btn, uint8_t down, uint8_t released);
static void panel_key_handler(uint8_t key, uint8_t down);
static void ui_input_poll(void);
static void ui_draw(void);
static void ui_init(void);
static void ds_state_init(void);
static void cursor_move(int8_t n);
static uint8_t key_black_ordinal(uint8_t key);
static uint8_t key_white_index(uint8_t key);

/* text builders (ui_text.c) */
static char *txt_next(void);
static char *txt_trk(const char *prefix, uint8_t one_based);
static char *txt_st(uint8_t one_based);
static char *txt_join(const char *a, const char *b);
static char *txt_vgp(uint8_t vel, uint8_t gate_raw, uint8_t prob);
static char *txt_knr(uint8_t k, uint8_t n, uint8_t rot);
static void strcpy_(char *dst, const char *src);

/* libc (Felucca libc.c) */
static void fmt_int(char *b, int32_t v);

/* note name table (emu NAMES) */
static const char *const NOTE_NAMES[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
static const char *note_name(uint8_t n) { return NOTE_NAMES[n % 12]; }
