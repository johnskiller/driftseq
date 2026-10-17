/* ui_screens.c — screen painters (part of ui.c unity section).
 * Ported from web/emu.js r16j draw branches; geometry in spec 03.
 * 240x240 budget: top bar 16 + main + footer 24. Text via ui_text.c builders
 * (no printf in the platform). */

/* ---- footer (prefix-free, fixed columns, 10px) ---- */
static void ui_draw_footer(void)
{
    static const char *const FOOT_HOME[] = {"START", "LEN", "MUTE", "PROB"};
    static const char *const FOOT_GRID[] = {"STEP", "LEN", "MUTE", "PROB"};
    static const char *const FOOT_EDIT[] = {"PIT", "GAT", "VEL", "PROB", "SEL"};
    static const char *const FOOT_DRUM[] = {"VEL", "GAT", "PROB"};
    static const uint8_t FX[5] = {6, 62, 118, 174, 200};
    uint8_t k;
    cv_begin(240, 24, C_BLACK);
    if (ui_mode == M_GRID && is_drum_trk() && focus_layer) {
        for (k = 0; k < 3; k++)
            cv_text(FX[k], 7, &FONT_S, FOOT_DRUM[k], C_AMB);
    } else if (ui_mode == M_GRID && focus_layer) {
        for (k = 0; k < 5; k++)
            cv_text(FX[k], 7, &FONT_S, FOOT_EDIT[k], C_AMB);
    } else {
        const char *const *f = (ui_mode == M_HOME) ? FOOT_HOME : FOOT_GRID;
        for (k = 0; k < 4; k++)
            cv_text(FX[k], 7, &FONT_S, f[k], C_GRAY);
    }
    cv_blit(0, 216);
}

/* ---- HOME hero: 16 unequal loop bars (03 §1), absolute 64-step space ---- */
static void draw_home(void)
{
    uint8_t t;
    cv_begin(240, 224, RGB565(0, 0, 0));
    cv_text(6, 3, &FONT_S, "HOME", RGB565(217, 217, 232));
    for (t = 0; t < 16; t++) {
        ds_track *tr = &g.pat.track[t];
        int y = t * 13 + 18;                     /* 16 rows x 13px = 208 */
        uint16_t col = TRACK_HUE(t);
        uint8_t s;
        for (s = 0; s < tr->loop_len; s++) {     /* window blocks, absolute space */
            int st = (tr->loop_start + s) & 63;
            int x = 30 + st * 3;                 /* 64 steps * 3px = 192 */
            uint8_t lit = 0, vi;
            for (vi = 0; vi < tr->steps[st].nvoices; vi++)
                if (tr->steps[st].v[vi].note != 0xFF) lit = 1;
            if (tr->steps[st].on)
                cv_rect(x, y + (lit ? 0 : 6), 2, lit ? 8 : 3, col);
        }
        /* playhead */
        cv_rect(28 + (int)g.pos16[t] * 3, y, 1, 10, C_WHITE);
        cv_text(2, y, &FONT_S, "T", tr->mute ? C_DIM : col);
    }
    cv_blit(0, 0);
    ui_draw_footer();
}

/* ---- MELODY GRID (rev.16B: line-Y = pitch, normalized to track span) ---- */
static void draw_grid_melodic(void)
{
    ds_track *tr = TRK();
    int r, c, v;
    int pmin = 127, pmax = 0;
    char *title;
    for (r = 0; r < 64; r++)
        for (v = 0; v < tr->steps[r].nvoices; v++) {
            int n = tr->steps[r].v[v].note;
            if (n < pmin) pmin = n;
            if (n > pmax) pmax = n;
        }
    if (pmin > pmax) { pmin = 48; pmax = 72; }   /* empty fallback C3-C5 */
    if (pmax == pmin) pmax = pmin + 1;

    title = txt_join("SEQ TRK", txt_trk("", sel_track + 1));
    cv_begin(240, 224, C_BLACK);
    cv_text(6, 3, &FONT_S, title, RGB565(217, 217, 232));
    /* 4 rows x 24px (y16..112) + embedded EDIT panel 104px (rev.12 budget) */
    for (r = 0; r < 4; r++) {
        int y = 16 + r * 24;
        cv_rect(16, y, 224, 24, RGB565(16, 16, 21));      /* row bg */
        for (c = 0; c < 16; c++) {                        /* whole 64 in 4 rows */
            int s = r * 16 + c;
            ds_step *st = &tr->steps[s];
            int x = 16 + c * 14;
            uint8_t vi;
            for (vi = 0; vi < st->nvoices; vi++) {
                int n = st->v[vi].note;
                if (n == 0xFF) continue;
                {   /* line y = pitch position in track span (rev.16B) */
                    int ly = y + 21 - (n - pmin) * 18 / (pmax - pmin);
                    uint16_t col = TRACK_HUE(n & 15);
                    cv_line(x + 1, ly, x + 12, ly, col);
                    if (s == cursor)
                        cv_rect(x, y + 1, 14, 22, C_WHITE);
                }
            }
            if (st->on && st->nvoices == 0) {             /* bare gate (r15b) */
                cv_rect(x + 1, y + 20, 12, 2, TRACK_HUE(sel_track % 11));
            }
        }
    }
    cv_blit(0, 0);
    ui_draw_edit_panel();                                  /* rev.12 embedded panel */
    ui_draw_footer();
}

/* ---- DRUM GRID (r16j): 11 lanes x 13px, cells 9x9 with 4px gaps ---- */
static void draw_grid_drum(void)
{
    uint8_t page = cursor >> 4;
    int li, c, x0 = 30, y0 = 16, lh = 13, gap = 4;
    int cw = (240 - x0) / 16;                    /* 13 */
    uint8_t lane_in_use[11];
    char *title;
    for (li = 0; li < 11; li++) {                /* r16i: which drums are voiced? */
        int s;
        lane_in_use[li] = 0;
        for (s = 0; s < 64 && !lane_in_use[li]; s++) {
            uint8_t nv = step_nv(sel_track, s), vi;
            for (vi = 0; vi < nv; vi++)
                if (step_note(sel_track, s, vi) == GM_DRUMS[li]) lane_in_use[li] = 1;
        }
    }
    title = txt_join(txt_trk("DRUM TRK", sel_track + 1),
                     txt_join("· p", txt_trk("/4", page + 1)));
    cv_begin(240, 160, C_BLACK);
    cv_text(6, 3, &FONT_S, title, RGB565(217, 217, 232));
    cv_text(150, 3, &FONT_S, txt_join(txt_st(cursor + 1), GM_SHORT[drum_sel]),
            TRK()->mute ? RGB565(90, 90, 98) : RGB565(95, 224, 138));
    for (li = 0; li < 11; li++) {
        int y = y0 + li * lh;
        uint8_t sel = (li == drum_sel);
        uint16_t stroke = sel ? RGB565(86, 86, 100) : RGB565(46, 46, 58);
        cv_text(2, y + 3, &FONT_S, GM_SHORT[li], sel ? RGB565(201, 201, 212) : RGB565(106, 106, 118));
        for (c = 0; c < 16; c++) {
            int s = page * 16 + c;
            ds_step *st = &TRK()->steps[s];
            int x = x0 + c * cw + gap / 2, cy = y + gap / 2;
            int cellw = cw - gap, cellh = lh - gap;
            uint8_t on = 0, vi;
            for (vi = 0; vi < st->nvoices; vi++)
                if (st->v[vi].note == GM_DRUMS[li]) on = 1;
            if (on) {
                uint8_t hi = 127;
                for (vi = 0; vi < st->nvoices; vi++)
                    if (st->v[vi].note == GM_DRUMS[li]) hi = st->v[vi].vel;
                cv_rect(x, cy, cellw, cellh, D_PAL[li]);
                if (hi < 100)                        /* vel dim (in-cell feedback) */
                    cv_rect(x, cy, cellw, cellh, RGB565(30, 30, 38));
                if (s == cursor && sel)
                    cv_rect(x - 1, cy - 1, cellw + 2, cellh + 2, C_WHITE);
            } else {
                cv_rect(x, cy, cellw, 1, stroke);            /* hollow square */
                cv_rect(x, cy + cellh - 1, cellw, 1, stroke);
                cv_rect(x, cy, 1, cellh, stroke);
                cv_rect(x + cellw - 1, cy, 1, cellh, stroke);
                if (st->on && sel && lane_in_use[li])        /* bare-gate tick (r16i) */
                    cv_rect(x + 1, cy + cellh - 3, cellw - 2, 2, D_PAL[li]);
            }
        }
    }
    /* playhead column (r16f) */
    cv_rect(x0 + (int)(g.pos16[sel_track] & 15) * cw, y0, 1, 11 * lh, C_WHITE);
    cv_blit(0, 0);
    ui_draw_readout();                                    /* 56px text strip */
    ui_draw_footer();
}
