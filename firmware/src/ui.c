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

/* forward decls (ui_screens.c defines the voices_sorted helper) */
static void voices_sorted(uint8_t t, uint8_t s);
static uint8_t sort_vs[4][3];
static void edit_key_handler(uint8_t key, uint8_t down);

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
static void cursor_move(int8_t n)            /* bare K1 = 1 step, dead-stop at 0/63 */
{
    /* rev.11 killed the GLO+K1 x16 jump: GLO+K1 is LOOP START on the GRID now,
     * so the step here is never multiplied. 64 steps at 1 per detent is a
     * deliberate trade — John picked the window shape over the fast scrub. */
    int step = (n > 0 ? 1 : -1);
    int next = cursor + step;
    if (next < 0) next = 0;
    if (next > 63) next = 63;
    cursor = (uint8_t)next;
}

/* black-key ordinal -> step-in-page. Key ids 0..26 = notes 53..79 (F3..G5);
 * black keys are the ids whose note % 12 is in {1,3,6,8,10} — ordinal 0..10.
 * Only 0..NDRUM-1 select a drum (currently 9 of 11); the rest are reserved for
 * the v1.1 percussion expansion (03 §3, John 2026-10-19). */
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
/* The INVERSE maps, needed by the LED layer: masks are addressed by key ID
 * (that is what panel.js and the FM-1 HAL shift by), but the model talks in
 * ordinals (white 0..15 = step-in-page, black 0..10 = drum). Going the other
 * way round is what lit the wrong keys. */
static uint8_t key_white_id(uint8_t ord)           /* white ordinal -> key id */
{
    uint8_t i, n = 0;
    for (i = 0; i < 27u; i++)
        if (!KEY_IS_BLACK[i] && n++ == ord) return i;
    return 0xFF;                                    /* beyond the bed */
}
static uint8_t key_black_id(uint8_t ord)           /* black ordinal -> key id */
{
    uint8_t i, n = 0;
    for (i = 0; i < 27u; i++)
        if (KEY_IS_BLACK[i] && n++ == ord) return i;
    return 0xFF;
}

/* black-key ordinal -> drum lane; white key -> step-in-page.
 * r16j key dispatch (mode/focus aware):
 *   grid+drum  : black = pick drum + audition, white = toggle that drum
 *   grid+melodic: OVERRIDE step entry — write key as the step content,
 *                revive rest, cursor+1 (dead-stop), live audition
 *   non-grid   : keys = live audition only
 * EDIT layer: keys edit voices (batch 3) — no grid writes now. */
static void panel_key_handler(uint8_t key, uint8_t down)
{
    if (ui_mode != M_GRID) {                          /* non-grid: live audition only */
        int note;
        if (is_drum_trk()) {
            uint8_t bi = key_black_ordinal(key);
            /* black key = that drum; the 2 reserved keys and white keys fall
             * back to the selected drum / kick (audition only, never a write) */
            note = (bi != 0xFF && bi < NDRUM) ? GM_DRUMS[bi]
                 : (bi != 0xFF) ? GM_DRUMS[drum_sel] : 36;
        } else {
            note = 53 + (int)key + key_shift;
            if (note < 0) note = 0;
            if (note > 127) note = 127;
        }
        if (down) ds_live_on(&g, sel_track, (uint8_t)note, 100);
        else ds_live_off(&g, sel_track, (uint8_t)note);
        return;
    }
    if (focus_layer) {                            /* EDIT layer: voice add/del (r16j rev.14a) */
        edit_key_handler(key, down);
        return;
    }
    if (is_drum_trk()) {                          /* r16j drumKeyHandler: press edge only */
        if (!down)
            return;                               /* key-up = no toggle (r16j parity) */
        {
            uint8_t bi = key_black_ordinal(key);
            if (bi != 0xFF) {
                /* 11 black keys, 9 lanes (03 §3): the last two are reserved for
                 * the v1.1 percussion expansion and select nothing — "选不满
                 * 不报错", and silently selecting a different drum than the
                 * printed key would be worse than doing nothing. */
                if (bi < NDRUM) {
                    drum_sel = bi;                /* pick instrument */
                    ds_live_on(&g, sel_track, GM_DRUMS[bi], 100);   /* hear it */
                }
                return;
            }
            {   /* white key = toggle sel drum on that step (page follows cursor) */
                uint8_t wi = key_white_index(key), page = cursor >> 4;
                if (wi < 16u) {
                    uint8_t s = (uint8_t)(page * 16 + wi);
                    ds_track *tr = TRK();
                    uint8_t vi, found = 0;
                    for (vi = 0; vi < tr->steps[s].nvoices; vi++)
                        if (tr->steps[s].v[vi].note == GM_DRUMS[drum_sel]) found = 1;
                    if (found)
                        ds_voice_remove(tr, s, GM_DRUMS[drum_sel]);
                    else {
                        ds_voice_add(tr, s, GM_DRUMS[drum_sel], 100, 1);
                        tr->steps[s].on = 1;      /* bare write revives the gate */
                    }
                    ds_live_on(&g, sel_track, GM_DRUMS[drum_sel], 100);  /* audition (r16j) */
                }
            }
        }
        return;
    }
    {   /* r16j stepEntry (rev.11 override, edge-model v1): one key = one
         * write — clear step, add voice, revive, cursor+1 dead-stop */
        ds_track *tr = TRK();
        uint8_t s = cursor;
        int note = 53 + (int)key + key_shift;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
        if (down) {
            while (tr->steps[s].nvoices)          /* override, not append */
                ds_voice_remove(tr, s, tr->steps[s].v[0].note);
            ds_voice_add(tr, s, (uint8_t)note, 100, 1);
            tr->steps[s].on = 1;                  /* revive rest steps on write */
            edit_voice = 0;
            if (cursor < 63u) cursor++;           /* rev.9: advance, dead-stop */
        }
        /* audition follows the key down/up either way */
        if (down) ds_live_on(&g, sel_track, (uint8_t)note, 100);
        else ds_live_off(&g, sel_track, (uint8_t)note);
    }
}

/* EDIT layer keys (r16j editKeyHandler, rev.14a): cursor NEVER advances.
 * Any key = that note on the cursor step: same note = delete the voice,
 * else add (vel 100, gate 1); full step -> replace the highest note.
 * Drums map through GM_DRUMS (black = lane note; white = KEY_WHITE ordinal). */
static void edit_key_handler(uint8_t key, uint8_t down)
{
    ds_track *tr = TRK();
    ds_step *st = &tr->steps[cursor];
    int note;
    if (is_drum_trk()) {
        uint8_t bi = key_black_ordinal(key);
        if (bi != 0xFF)
            note = GM_DRUMS[bi];
        else {
            uint8_t wi = key_white_index(key);
            if (wi >= NDRUM) wi = (uint8_t)(wi % NDRUM);  /* %GM_DRUMS.length */
            note = GM_DRUMS[wi];
        }
    } else {
        note = 53 + (int)key + key_shift;
        if (note < 0) note = 0;
        if (note > 127) note = 127;
    }
    if (down) {                                            /* hear + write */
        uint8_t vi, hit = 0xFF;
        ds_live_on(&g, sel_track, (uint8_t)note, 100);
        for (vi = 0; vi < st->nvoices; vi++)
            if (st->v[vi].note == note) hit = vi;
        if (hit != 0xFF) {                                 /* same note = delete */
            ds_voice_remove(tr, cursor, (uint8_t)note);
            if (edit_voice >= st->nvoices && st->nvoices)
                edit_voice = (uint8_t)(st->nvoices - 1);
            return;
        }
        if (st->nvoices < 4) {
            /* gate 1 step, not 4 (rev.28). 01 §2 + 03 §3 both specify the
             * default voice as "vel 100 / gate 1". Hardcoding 4 meant a note
             * written on step 1 at DIV 1/16 held for 4 steps = a whole beat,
             * so the hero drew a line across the entire 1/4 note for what the
             * user heard (and intended) as a single 1/16 hit. The hero was
             * telling the truth about the data; the data was wrong. */
            ds_voice_add(tr, cursor, (uint8_t)note, 100, 1);
            edit_voice = (uint8_t)(st->nvoices - 1);       /* new voice active */
        } else {                                           /* full: replace highest */
            voices_sorted(sel_track, cursor);
            ds_voice_remove(tr, cursor, sort_vs[3][0]);
            ds_voice_add(tr, cursor, (uint8_t)note, 100, 1);
            edit_voice = 3;
        }
        st->on = 1;
    } else {
        int off = 53 + (int)key + key_shift;
        if (off < 0) off = 0;
        if (off > 127) off = 127;
        ds_live_off(&g, sel_track, (uint8_t)off);          /* drum offs dedup C-side */
    }
}

/* key release -> note-off for the live preview (same pitch math as down) */
static void panel_key_up(uint8_t key)
{
    panel_key_handler(key, 0);
}

static void ui_poll(void)
{
    ui_input_poll();
    ui_draw();
}

/* ---- LED state as a pure function of the UI (03 §2.3 / §3) ----
 * John 2026-10-20: "drum grid中，选中的鼓，对应的鼓点没有点亮白键。理想情况下
 * 可以用dim表示1，5，9，13，亮起白键表示这个step有鼓点".
 *
 * Nothing in the firmware ever fed UI state to the LEDs: leds.c holds a mirror
 * of incoming MIDI notes only (midi_io.c leds_on_note), which is for a host
 * driving the panel. The 16 white keys therefore showed whatever the last MIDI
 * message said — normally nothing.
 *
 * Grammar (one bit per white key 0..15 = step-in-page, page follows cursor):
 *   dim  = beat onset (1, 5, 9, 13 at 1/16) — a ruler for blind entry
 *   lit  = the selected drum has a hit on that step
 *   both = the key is a beat AND has a hit; lit wins (a hit is the louder fact)
 *
 * Beats are computed in TICKS, not steps: a beat is TPB/4 = 24 ticks, so at
 * 1/8 a beat is 2 steps, at 1/8T it is 3, at 1/32 it is 8. Hardcoding "every
 * 4th step" would put the ruler in the wrong place on three of the five
 * divisions, which is worse than no ruler at all.
 *
 * Melodic tracks use the identical bit layout (03 §2.3: white key = that step
 * is on), so the same function serves both — only "which voice counts" differs.
 */
static void ui_leds(uint32_t *litK, uint32_t *dimK, uint32_t *litB, uint32_t *dimB)
{
    ds_track *tr = TRK();
    uint8_t is_drum = (tr->channel == 9);
    uint8_t page = cursor >> 4;
    int ct = cell_ticks(tr->division);
    int c;

    *litK = *dimK = *litB = *dimB = 0;

    for (c = 0; c < 16; c++) {
        uint8_t s = (uint8_t)(page * 16 + c);
        ds_step *st = &tr->steps[s];
        int on = 0;
        uint8_t key;

        if (is_drum) {                       /* only the SELECTED drum lights up */
            uint8_t vi;
            for (vi = 0; vi < st->nvoices; vi++)
                if (st->v[vi].note == GM_DRUMS[drum_sel]) on = 1;
        } else {
            on = st->on && st->nvoices > 0;  /* melodic: the step is voiced */
        }

        /* A white key's ID is NOT its ordinal: the bed is a real piano, so the
         * 16 step keys are ids 0,2,4,6,7,9,11,12,... with black keys between
         * them. panel.js and the HAL both address LEDs by key ID, so shifting
         * by the ordinal lit the wrong physical keys (and on a real FM-1 the
         * leftmost keys stayed dark). John: "亮的不对（估计是没考虑到黑键，
         * 没有跳过黑键）". */
        key = key_white_id((uint8_t)c);
        if (key == 0xFF)
            continue;
        if (on) *litK |= 1u << key;
        /* beat onset: 24 ticks = one beat in 4/4 (TPB = 96). At 1/16 (6 ticks
         * per step) that is steps 0,4,8,12 — the 1,5,9,13 John asked for. */
        if (((s * ct) % (TPB / 4)) == 0)
            *dimK |= 1u << key;
    }
    /* One white key = one step of the CURRENT page (03 §2.3 "cursor 所在 16 格页
     * 内映射"). Beats outside the page belong to keys that show other steps, so
     * nothing extra is drawn. */

    /* The selected drum's own black key lights up: John 2026-10-20 — "选择某个
     * 黑键，那么这个黑键亮起，对应鼓点的白键亮起". Without this the panel gives no
     * feedback about which instrument the white keys are currently describing,
     * which is the one thing you cannot infer from the keys themselves. */
    if (is_drum && drum_sel < NDRUM) {
        uint8_t bk = key_black_id(drum_sel);   /* black ordinal -> key id */
        if (bk != 0xFF)
            *litK |= 1u << bk;
    }

    if (tr->mute)                    /* muted track: no hit LEDs, ruler stays */
        *litK = 0;

    /* button LEDs (only states that actually exist in v1 — REC is a momentary
     * cursor-step toggle, not an armed mode, so it has no idle light) */
    if (g.playing)   *litB |= 1u << B_PLAY;
    if (focus_layer) *litB |= 1u << B_EDIT;
}

#ifndef DS_WEB_UI
/* Push the masks above onto the physical panel. Key ids in the HAL are
 * "button 0..13, note key 14..40": white key i (0..15) is note index i, which
 * is HAL id 14+i. Writing every key every pass is cheap (11 columns x 4 rows of
 * bit tests) and keeps this stateless. */
static void ui_leds_sync(void)
{
    uint32_t litK, dimK, litB, dimB, i;
    ui_leds(&litK, &dimK, &litB, &dimB);
    fm1_led_dim_div = 4;                 /* steady, ~1/4 brightness (03 grammar) */
    for (i = 0; i < 27u; i++) {          /* all 27 key ids: white step keys AND
                                          * the black key of the selected drum */
        fm1_led_key(14u + i, (litK >> i) & 1u);
        fm1_led_key_dim(14u + i, (dimK >> i) & 1u);
    }
    for (i = 0; i < 14u; i++)
        fm1_led_key(i, (litB >> i) & 1u);
}
#endif /* DS_WEB_UI */
