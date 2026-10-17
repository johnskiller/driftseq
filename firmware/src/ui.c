/* ui.c — DriftSeq state + screen dispatch + key entry (spec 03, emu r16j
 * parity). Included by app.c AFTER the platform (lcd/gfx statics visible) and
 * the engine (host/driftseq.c: ds_state g). Ported from web/emu.js r16j:
 *   HOME hero / MELODY GRID (line-Y = pitch, bare-gate bar) /
 *   DRUM GRID (11 lanes x 9x9 gap cells, hollow/solid, readout strip,
 *   bare-gate tick = sel lane + lane-in-use) / ARP (K/N/ROT/LINK).
 * v1 scope (b42a5eb): step rec/play/edit + USB out. EAS GM = batch 2. */

/* ---- UI state (emu globals parity) ---- */
enum { M_HOME = 0, M_GRID, M_ARP };
static uint8_t ui_mode = M_HOME;
static uint8_t focus_layer;                  /* 0 base 1 EDIT (GRID only) */
static uint8_t cursor;                       /* 0..63, dead-stop */
static uint8_t edit_voice;                   /* melodic: active voice (pitch order) */
static uint8_t drum_sel;                     /* selected drum lane */
static int8_t key_shift;                     /* OCT± keybed shift (±12) */
static uint8_t glo_held;
static uint8_t sel_track;

static ds_track *TRK(void) { return &g.pat.track[sel_track]; }
static uint8_t is_drum_trk(void) { return TRK()->channel == 9; }
static uint8_t step_nv(uint8_t t, uint8_t s) { return g.pat.track[t].steps[s].nvoices; }
static uint8_t step_note(uint8_t t, uint8_t s, uint8_t vi) { return g.pat.track[t].steps[s].v[vi].note; }

/* engine state init: ds_init(42) + demo (emu boot parity) */
static void ds_state_init(void)
{
    ds_init(&g, 42);
    ds_load_demo(&g);
}

/* Full-canvas convention (batch 2): one cv_begin(240,224) pass per frame,
 * each painter blits its band to the screen y it owns. cv_y0/y1 stay full so
 * painters share the whole canvas (cross-band items like the drum playhead). */
static void cv_frame_begin(void)
{
    cv_y0 = 0;
    cv_y1 = (int32_t)cv_h;
}

static void ui_draw(void)
{
    cv_begin(240, 224, C_BLACK);              /* painters blit their own bands */
    if (ui_mode == M_HOME) draw_home();
    else if (ui_mode == M_GRID) {
        if (is_drum_trk()) draw_grid_drum(); else draw_grid_melodic();
    } else draw_arp();
}

/* ---- panel matrix mapping (fm1-ctrl panel.c proven values) ----
 * The web build has no panel_t (no calibration layer): its ui_web.c shim
 * defines a compatible two-array stand-in (panel.btn/panel.enc) so this
 * code compiles unchanged in both roots. */
#ifndef DS_WEB_PANEL_SHIM
static panel_t panel;
#endif
static uint8_t btn_role(uint8_t matrix_id)   /* matrix id -> logical button, 0xFF=none */
{
    uint8_t i;
    for (i = 0; i < 14u; i++)
        if (panel.btn[i] == matrix_id) return i;
    return 0xFF;
}
static uint8_t enc_role(uint8_t matrix_enc)  /* matrix enc -> logical encoder, 0xFF=none */
{
    uint8_t i;
    for (i = 0; i < 7u; i++)
        if (panel.enc[i] == matrix_enc) return i;
    return 0xFF;
}

/* ---- input routing (emu panelEnc/panelButton/panelKey parity) ---- */
static void cursor_move(int8_t n)            /* GLO+K1 = x16 (rev.9 dead-stop) */
{
    int step = (glo_held ? 16 : 1) * (n > 0 ? 1 : -1);
    int next = cursor + step;
    if (next < 0) next = 0;
    if (next > 63) next = 63;
    cursor = (uint8_t)next;
}

/* black-key ordinal -> step-in-page. Key ids 0..26 = notes 53..79 (F3..G5);
 * black keys are the ids whose note % 12 is in {1,3,6,8,10} — ordinal 0..10
 * matches the emu's keybed (drum lane list = 11 black keys, r16f). */
static const uint8_t KEY_IS_BLACK[27] = {
    0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0,   /* F3 F#3 G3 G#3 A3 A#3 B3 C4 */
    0, 1, 0, 1, 0, 1, 0, 0, 1, 0, 1, 0,   /* F4..C5 */
    0, 1, 0                                /* F5, F#5, G5 */
};
static uint8_t key_black_ordinal(uint8_t key)      /* 0xFF = white/none */
{
    uint8_t ord = 0, i;
    if (key >= 27u || !KEY_IS_BLACK[key]) return 0xFF;
    for (i = 0; i < key; i++)
        if (KEY_IS_BLACK[i]) ord++;
    return ord;                                     /* (void)n; silences pedants */
}
static uint8_t key_white_index(uint8_t key)        /* 0..15 = step-in-page */
{
    uint8_t ord = 0, i;
    for (i = 0; i < key; i++)
        if (!KEY_IS_BLACK[i]) ord++;
    return ord;
}

/* black-key ordinal -> drum lane; white key -> toggle step (drum track).
 * Non-grid keys = live preview through the engine (emu liveNote parity). */
static void panel_key_handler(uint8_t key, uint8_t down)
{
    if (!down)
        return;
    if (ui_mode == M_GRID && is_drum_trk()) {
        uint8_t bi = key_black_ordinal(key);
        if (bi != 0xFF) { drum_sel = bi; return; }
        {   /* white key = toggle step: add/remove sel drum on that step */
            uint8_t wi = key_white_index(key), page = cursor >> 4;
            if (wi < 16u) {
                uint8_t s = page * 16 + wi;
                ds_track *tr = TRK();
                uint8_t vi, found = 0;
                for (vi = 0; vi < tr->steps[s].nvoices; vi++)
                    if (tr->steps[s].v[vi].note == GM_DRUMS[drum_sel]) found = 1;
                if (found)
                    ds_voice_remove(tr, s, GM_DRUMS[drum_sel]);
                else
                    ds_voice_add(tr, s, GM_DRUMS[drum_sel], 100, 1);
                cursor = s;
            }
        }
        return;
    }
    {   /* melodic live audition: note = 53 + key + key_shift (F3 base, rev.7) */
        int note = 53 + (int)key + key_shift;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        if (down) ds_live_on(&g, (uint8_t)sel_track, (uint8_t)note, 100);
    }
    /* melodic key entry (override semantics, rev.11): batch-2 polish lands
     * the chord/hold-step-picker paths; v1 batch 1 = single note write. */
}

/* key release -> note-off for the live preview (same pitch math as down) */
static void panel_key_up(uint8_t key)
{
    int note = 53 + (int)key + key_shift;
    if (note < 0) note = 0;
    if (note > 127) note = 127;
    ds_live_off(&g, (uint8_t)sel_track, (uint8_t)note);
}

static void ui_poll(void)
{
    ui_input_poll();
    ui_draw();
}
