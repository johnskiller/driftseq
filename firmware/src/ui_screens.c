/* ui_screens.c — screen painters (part of ui.c unity section).
 * Faithful port of web/emu.js r16j (f33931a) draw branches: same geometry,
 * same colors, same layering. 240x240 budget: top bar 16 + main + footer 24.
 * Text via ui_text.c builders (no printf in the platform).
 * Font mapping (real Barlow roles): FONT_T 12px caps for titles/gutters/
 * footer; FONT_XS 15px for readout/cards. r16j's 11px monospace -> XS. */

/* r16j top-bar color constants (all screens share) */
#define TB_BG      RGB565(24, 24, 32)
#define TB_TX      RGB565(217, 217, 232)
#define GREEN_OK   RGB565(95, 224, 138)
#define MUTE_GRAY  RGB565(90, 90, 98)
#define FOOT_GRAY  RGB565(138, 138, 153)
#define YELLOW     RGB565(230, 200, 74)
#define WARMWHITE  RGB565(255, 244, 214)
#define BRIGHTTX   RGB565(240, 240, 246)

/* hsl(note%12*30, 72%, 55%) — the 12-hue pitch wheel (melodic line color) */
static uint16_t hue12(uint8_t note)
{
    static const uint16_t H12[12] = {
        RGB565(217, 74, 61),  RGB565(217, 128, 51), RGB565(217, 189, 46),
        RGB565(151, 214, 57), RGB565(87, 212, 66),  RGB565(56, 209, 125),
        RGB565(51, 205, 189), RGB565(52, 177, 214), RGB565(58, 122, 216),
        RGB565(97, 77, 215),  RGB565(163, 62, 209), RGB565(212, 60, 180),
    };
    return H12[note % 12u];
}

/* sorted voices of step s, pitch-ascending -> out[0..3] (note, vel, gate).
 * sort_vs is DECLARED in ui.c (edit_key_handler consumes it before this
 * definition appears in the unity include order). */
static uint8_t sort_n;
static void voices_sorted(uint8_t t, uint8_t s)
{
    uint8_t nv = g.pat.track[t].steps[s].nvoices, i, j;
    sort_n = 0;
    for (i = 0; i < nv; i++) {
        ds_note *v = &g.pat.track[t].steps[s].v[i];
        if (v->note == 0xFF)
            continue;
        sort_vs[sort_n][0] = v->note;
        sort_vs[sort_n][1] = v->vel;
        sort_vs[sort_n][2] = v->gate;
        sort_n++;
    }
    for (i = 0; i < sort_n; i++)                    /* insertion sort (n<=4) */
        for (j = (uint8_t)(i + 1); j < sort_n; j++)
            if (sort_vs[j][0] < sort_vs[i][0]) {
                uint8_t tmp0 = sort_vs[i][0], tmp1 = sort_vs[i][1], tmp2 = sort_vs[i][2];
                sort_vs[i][0] = sort_vs[j][0]; sort_vs[i][1] = sort_vs[j][1]; sort_vs[i][2] = sort_vs[j][2];
                sort_vs[j][0] = tmp0; sort_vs[j][1] = tmp1; sort_vs[j][2] = tmp2;
            }
}

/* note name "C#3" (FONT_T-safe; NOTE_NAMES from ui_glue.c) */
static char *note_name2(uint8_t note)
{
    char *b = txt_next();
    strcpy_(b, note_name(note));
    fmt_int(txt_end(b), (int32_t)(note / 12u) - 1);
    return b;
}

/* gate display: 0xFF = INF (r16j shows the loop symbol) */
static char *gate_str(uint8_t gate)
{
    char *b = txt_next();
    if (gate == 0xFF) { b[0] = 'L'; b[1] = 'O'; b[2] = 0; return b; }  /* FONT_T-safe "∞" */
    fmt_int(b, (int32_t)gate);                       /* raw steps, r16j parity (g4 = 1.00) */
    return b;
}

/* chord name (r16j rev.13 verbatim): single note = "C3"; interval-set match
 * against common triads/7ths = "Cmaj7" style; else pitch classes spaced out.
 * Returns a txt_next() buffer ("" = empty step). */
static char *chord_name(uint8_t t, uint8_t s)
{
    char *b = txt_next();
    uint8_t i, j, root, ivs[4], niv = 0;
    uint16_t set_mask;                              /* 12 bits — uint8_t would eat bit8+ (Db..) */
    voices_sorted(t, s);
    b[0] = 0;
    if (sort_n == 0)
        return b;
    if (sort_n == 1) {
        strcpy_(b, note_name2(sort_vs[0][0]));
        return b;
    }
    root = (uint8_t)(sort_vs[0][0] % 12u);
    set_mask = 0;
    for (i = 0; i < sort_n; i++)
        set_mask = (uint16_t)(set_mask | (1u << (sort_vs[i][0] % 12u)));
    for (i = 0; i < 12; i++) {                      /* sorted interval list (dedup via mask) */
        if (set_mask & (1u << i)) {
            uint8_t iv = (uint8_t)((i - root + 12u) % 12u);
            for (j = 0; j < niv && ivs[j] < iv; j++)
                ;
            if (j == niv || ivs[j] != iv) {
                int k;
                for (k = niv; k > j; k--)
                    ivs[k] = ivs[k - 1];
                ivs[j] = iv;
                niv++;
            }
        }
    }
    {   /* match "0,4,7"=maj "0,3,7"=m "0,4,7,11"=maj7 "0,3,7,10"=m7
         * "0,3,6"=dim "0,4,8"=aug "0,5,7"=sus4 "0,2,7"=sus2; else spell out */
        const char *q = NULL;
        if (niv == 3 && ivs[0] == 0 && ivs[1] == 4 && ivs[2] == 7) q = "maj";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 3 && ivs[2] == 7) q = "m";
        else if (niv == 4 && ivs[0] == 0 && ivs[1] == 4 && ivs[2] == 7 && ivs[3] == 11) q = "maj7";
        else if (niv == 4 && ivs[0] == 0 && ivs[1] == 3 && ivs[2] == 7 && ivs[3] == 10) q = "m7";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 3 && ivs[2] == 6) q = "dim";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 4 && ivs[2] == 8) q = "aug";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 5 && ivs[2] == 7) q = "sus4";
        else if (niv == 3 && ivs[0] == 0 && ivs[1] == 2 && ivs[2] == 7) q = "sus2";
        if (q) {
            strcpy_(b, note_name(root));             /* r16j: NAMES[root], NO octave */
            strcpy_(txt_end(b), q);
        } else {
            for (i = 0; i < sort_n; i++) {
                strcpy_(txt_end(b), note_name(sort_vs[i][0]));
                if (i + 1 < sort_n)
                    strcpy_(txt_end(b), " ");
            }
        }
    }
    return b;
}

/* ---- footer (prefix-free, fixed columns, 10px) ---- */
static void ui_draw_footer(void)
{
    static const char *const FOOT_HOME[] = {"START", "LEN", "MUTE", "PROB"};
    static const char *const FOOT_GRID[] = {"STEP", "LEN", "MUTE", "PROB"};
    static const char *const FOOT_EDIT[] = {"PIT", "GAT", "VEL", "PROB", "SEL"};
    static const char *const FOOT_DRUM[] = {"VEL", "GAT", "PROB"};
    static const char *const FOOT_ARP[]  = {"K", "N", "ROT", "LINK"};
    static const uint8_t FX[5] = {8, 64, 120, 176, 204};
    uint8_t k;
    cv_begin(240, 24, TB_BG);
    if (ui_mode == M_GRID && is_drum_trk() && focus_layer) {
        for (k = 0; k < 3; k++)
            cv_text(FX[k], 6, &FONT_T, FOOT_DRUM[k], C_AMB);
    } else if (ui_mode == M_GRID && focus_layer) {
        for (k = 0; k < 5; k++)
            cv_text(FX[k], 6, &FONT_T, FOOT_EDIT[k], YELLOW);
    } else if (ui_mode == M_ARP) {
        for (k = 0; k < 4; k++)
            cv_text(FX[k], 6, &FONT_T, FOOT_ARP[k], FOOT_GRAY);
    } else {
        const char *const *f = (ui_mode == M_HOME) ? FOOT_HOME : FOOT_GRID;
        for (k = 0; k < 4; k++)
            cv_text(FX[k], 6, &FONT_T, f[k], FOOT_GRAY);
    }
    cv_blit(0, 216);
}

/* ---- HOME hero (r16j drawHome geometry; rev.17 step width) ----
 * top bar 20px: TRK nn | PTN 01 | BPM ... right: S nn
 * 16 rows x 12px (y24..216): gutter TRK nn / LEN nn / route U / M,
 * window band (15% hue), drum = lane mini-lines, melodic = block,
 * prob<100 = 55%, selected track green outline, white playhead.
 * rev.17 (John): each of the 64 steps is exactly 2px with NO gap —
 * 64*2 = 128px bar (56..184). The old 176px bar gave 2.75px/step which
 * integer-truncated to 1px blocks and read as noise on the real screen. */
#define HX0 56
#define HSTEP 2
#define HX1 (HX0 + 64 * HSTEP)                   /* 184 */
static void draw_home(void)
{
    uint8_t t;
    cv_begin(240, 224, C_BLACK);
    cv_rect(0, 0, 240, 20, TB_BG);
    {   char *b = txt_next();
        strcpy_(b, "TRK ");
        fmt_int(txt_end(b), sel_track + 1);
        strcpy_(txt_end(b), "-PTN 01-");
        fmt_int(txt_end(b), (int32_t)(g.bpm + 0.5));
        cv_text(6, 4, &FONT_T, b, TB_TX);
    }
    cv_text(204, 4, &FONT_T, txt_join("S", txt_num(0, TRK()->loop_start)),
            TRK()->mute ? MUTE_GRAY : GREEN_OK);
    for (t = 0; t < 16; t++) {
        ds_track *tr = &g.pat.track[t];
        int y = 24 + (int)t * 12;
        uint16_t col = TRACK_HUE(t);
        uint8_t muted = tr->mute;
        uint8_t is_drum = (tr->channel == 9);
        uint8_t s;
        /* gutter: TRK nn | LEN nn | U | M */
        cv_text(4, y, &FONT_T, txt_num(0, t + 1), muted ? MUTE_GRAY : RGB565(154, 154, 168));
        cv_text(30, y, &FONT_T, txt_num(0, tr->loop_len), muted ? RGB565(74, 74, 82) : RGB565(106, 106, 120));
        cv_text(46, y, &FONT_T, "U", muted ? RGB565(74, 74, 82) : RGB565(122, 122, 136));
        if (muted)
            cv_text(46, y, &FONT_T, "M", RGB565(224, 160, 64));
        /* window band: 15% track hue over the bar (spec img/home.svg opacity 0.15).
         * blend()'s 3rd arg is the BACKGOUND weight: a=255 keeps bg, so 0.15 hue
         * = a 217. On-blocks below carry the real track color at full alpha. */
        cv_rect(HX0, y, HX1 - HX0, 11, muted ? RGB565(20, 20, 24) : RGB565(29, 29, 38));
        if (!muted) {
            uint8_t k;
            for (k = 0; k < tr->loop_len; k++) {
                int px = HX0 + (((tr->loop_start + k) & 63) * HSTEP);
                cv_rect(px, y, HSTEP, 11, blend(col, RGB565(29, 29, 38), 217));
            }
            for (s = 0; s < 64; s++) {
                ds_step *st = &tr->steps[s];
                uint8_t vi;
                int px = HX0 + s * HSTEP;
                if (!st->on || !st->nvoices)
                    continue;
                if (((s + 64 - tr->loop_start) & 63) >= tr->loop_len)
                    continue;
                if (is_drum) {
                    /* rev.14e: 1px lane mini-lines, top->bottom, lane order */
                    cv_rect(px, y, HSTEP, 11, blend(col, RGB565(29, 29, 38), 205));
                    {
                        uint8_t mask = 0, li, row = 0;
                        for (vi = 0; vi < st->nvoices; vi++) {
                            uint8_t d;
                            for (d = 0; d < 11; d++)
                                if (st->v[vi].note == GM_DRUMS[d]) mask |= (uint8_t)(1u << d);
                        }
                        for (li = 0; li < 9 && row < 4; li++) {
                            if (!(mask & (1u << li)))
                                continue;
                            cv_rect(px, y + 1 + row * 3, HSTEP, 1, D_PAL[li % 11]);
                            row++;
                        }
                    }
                } else {
                    /* melodic on-block: full track color; prob<100 = 55% (r16j globalAlpha 0.55)
                     * over the bar bg, i.e. bg weight 115 */
                    cv_rect(px, y, HSTEP, 11, st->prob < 100 ? blend(col, RGB565(29, 29, 38), 115) : col);
                }
            }
            {   /* playhead (absolute, white, 2px = exactly one step, playing only) */
                int pos = (int)g.pos16[t];
                int rel = (pos + 64 - tr->loop_start) & 63;
                if (rel < (int)tr->loop_len && g.playing)
                    cv_rect(HX0 + pos * HSTEP, y, HSTEP, 11, C_WHITE);
            }
        }
        if (t == sel_track) {                        /* selected: green outline */
            cv_rect(HX0 - 2, y - 1, HX1 - HX0 + 4, 1, RGB565(127, 217, 74));
            cv_rect(HX0 - 2, y + 11, HX1 - HX0 + 4, 1, RGB565(127, 217, 74));
            cv_rect(HX0 - 2, y, 1, 11, RGB565(127, 217, 74));
            cv_rect(HX1 + 1, y, 1, 11, RGB565(127, 217, 74));
        }
    }
    cv_blit(0, 0);
    ui_draw_footer();
}

/* "EDIT" badge at the top-bar right corner — the loud layer flag (r16j has
 * footer color only; John asked for a醒目标记). Yellow box, black text. */
static void ui_draw_edit_badge(void)
{
    cv_rect(196, 2, 40, 12, YELLOW);
    cv_text(201, 4, &FONT_T, "EDIT", C_BLACK);
}

/* ---- MELODY GRID (r16j drawGrid verbatim) ----
 * 4x24px rows (y16..112), cell bg = loop in/out (#2a2a33 / #16161c),
 * pitch line = hue12(note), rest = gray dashed, bare gate = bottom bar,
 * yellow cursor, white playhead ring, P1-P4 labels, EDIT panel at 112. */
static void draw_grid_melodic(void)
{
    ds_track *tr = TRK();
    uint8_t pmin = 127, pmax = 0;
    int r, c;
    uint8_t start = tr->loop_start, len = tr->loop_len;
    int pos = (int)g.pos16[sel_track];
    for (r = 0; r < 64; r++) {
        uint8_t vi;
        for (vi = 0; vi < tr->steps[r].nvoices; vi++) {
            int n = tr->steps[r].v[vi].note;
            if (n == 0xFF) continue;
            if (n < pmin) pmin = tr->steps[r].v[vi].note;
            if (n > pmax) pmax = tr->steps[r].v[vi].note;
        }
    }
    if (pmin > pmax) { pmin = 48; pmax = 72; }
    if (pmax == pmin) pmax = (uint8_t)(pmin + 1);

    cv_begin(240, 224, C_BLACK);
    cv_rect(0, 0, 240, 16, TB_BG);
    cv_text(6, 2, &FONT_T, txt_join("SEQ TRK", txt_num(0, sel_track + 1)), TB_TX);
    {   /* r16j: `st${cursor+1}${cn?' '+cn:''}` — chord quality on the right */
        char *cn = chord_name(sel_track, cursor);
        cv_text(150, 2, &FONT_T, txt_join2(txt_st(cursor + 1), *cn ? txt_join("", cn) : (char *)0),
                tr->mute ? MUTE_GRAY : GREEN_OK);
    }
    if (key_shift != 0)
        cv_text(120, 2, &FONT_T, key_shift > 0 ? "+" : "-", YELLOW);
    if (focus_layer)
        ui_draw_edit_badge();
    {
        const int x0 = 18, cw = 13, chh = 24;        /* r16j: x0=18, cw=(240-18-16)/16=13 */
        for (r = 0; r < 4; r++)
            for (c = 0; c < 16; c++) {
                uint8_t s = (uint8_t)(r * 16 + c);
                ds_step *st = &tr->steps[s];
                int x = x0 + c * cw, y = 16 + r * chh;
                uint8_t rel = (uint8_t)((s + 64 - start) & 63);
                uint8_t inWin = rel < len;
                uint8_t vi;
                /* cell bg: loop range (bright inside / dark outside) */
                cv_rect(x + 1, y + 1, cw - 2, chh - 2, inWin ? RGB565(42, 42, 51) : RGB565(22, 22, 28));
                if (st->on && st->nvoices == 0)      /* bare gate (r15b/#16) */
                    cv_rect(x + 2, y + chh - 4, cw - 4, 3, blend(D_PAL[(uint8_t)((tr->channel * 3) % 11)], C_BLACK, 153));
                voices_sorted(sel_track, s);         /* pitch-ascending, high on top */
                for (vi = 0; vi < sort_n; vi++) {
                    int span = pmax - pmin;
                    int frac = span ? ((int)sort_vs[vi][0] - pmin) * 256 / span : 128;
                    int lineY = y + 4 + ((256 - frac) * (chh - 11) + 128) / 256 + vi;
                    if (!st->on)
                        cv_rect(x + 3, lineY, cw - 6, 1, RGB565(102, 102, 114));
                    else
                        cv_rect(x + 2, lineY, cw - 4, 2, hue12(sort_vs[vi][0]));
                }
                if (g.playing && pos == s)           /* playhead ring */
                    cv_rect(x + 1, y + 1, cw - 2, 1, C_WHITE);
                if (cursor == s) {                   /* cursor: yellow outline */
                    cv_rect(x, y, cw, 1, YELLOW);
                    cv_rect(x, y + chh - 1, cw, 1, YELLOW);
                    cv_rect(x, y, 1, chh, YELLOW);
                    cv_rect(x + cw - 1, y, 1, chh, YELLOW);
                }
                if (c == 0)
                    cv_text(3, y + 4, &FONT_T, txt_join("P", txt_num(0, r + 1)), FOOT_GRAY);
            }
    }
    cv_blit(0, 0);
    ui_draw_edit_panel();                            /* rev.12 embedded panel */
    ui_draw_footer();
}

/* ---- DRUM GRID (r16j drawDrumGrid verbatim: 11 lanes x13px, cells 9x9
 * GAP4, hollow/solid, gate dimming, playhead column, readout strip 56px) */
static void draw_grid_drum(void)
{
    uint8_t page = cursor >> 4;
    int li, c, x0 = 30, y0 = 16, lh = 13, gap = 4;
    int cw = (240 - x0) / 16;                        /* 13 */
    uint8_t lane_in_use[11];
    int pos = (int)g.pos16[sel_track];
    for (li = 0; li < 11; li++) {                    /* r16i: which drums are voiced? */
        int s;
        lane_in_use[li] = 0;
        for (s = 0; s < 64 && !lane_in_use[li]; s++) {
            uint8_t nv = step_nv(sel_track, s), vi;
            for (vi = 0; vi < nv; vi++)
                if (step_note(sel_track, s, vi) == GM_DRUMS[li]) lane_in_use[li] = 1;
        }
    }
    cv_begin(240, 160, C_BLACK);
    cv_rect(0, 0, 240, 16, TB_BG);
    cv_text(6, 2, &FONT_T, txt_join(txt_trk("DRUM TRK", sel_track + 1),
             txt_join("P", txt_join(txt_num(0, page + 1), "/4"))), TB_TX);
    cv_text(150, 2, &FONT_T, txt_join(txt_st(cursor + 1), GM_SHORT[drum_sel]),
            TRK()->mute ? MUTE_GRAY : GREEN_OK);
    if (focus_layer)
        ui_draw_edit_badge();
    for (li = 0; li < 11; li++) {
        int y = y0 + li * lh;
        uint8_t sel = (li == drum_sel);
        cv_text(2, y + 2, &FONT_T, GM_SHORT[li], sel ? RGB565(201, 201, 212) : RGB565(106, 106, 118));
        for (c = 0; c < 16; c++) {
            uint8_t s = (uint8_t)(page * 16 + c);
            ds_step *st = &TRK()->steps[s];
            int x = x0 + c * cw + gap / 2, cy = y + gap / 2;
            int cellw = cw - gap, cellh = lh - gap;
            uint8_t on = 0, vi, hit_gate = 4;
            for (vi = 0; vi < st->nvoices; vi++)
                if (st->v[vi].note == GM_DRUMS[li]) { on = 1; hit_gate = st->v[vi].gate; }
            if (on) {                                /* solid hit (35% when not sel lane) */
                uint16_t col = blend(D_PAL[li % 11], C_BLACK, sel ? 0 : 166);
                int gf = (hit_gate == 0xFF) ? cellw : cellw * ((hit_gate * 64) / 32) / 256;
                cv_rect(x, cy, cellw, cellh, col);
                if (s == cursor && sel && gf < cellw) /* gate: dim the un-gated span */
                    cv_rect(x + gf, cy, cellw - gf, cellh, blend(col, C_BLACK, 179));
                if (s == cursor && sel) {            /* cursor hit: white outline */
                    cv_rect(x - 1, cy - 1, cellw + 2, 1, BRIGHTTX);
                    cv_rect(x - 1, cy + cellh, cellw + 2, 1, BRIGHTTX);
                    cv_rect(x - 1, cy - 1, 1, cellh + 2, BRIGHTTX);
                    cv_rect(x + cellw, cy - 1, 1, cellh + 2, BRIGHTTX);
                }
            } else {                                 /* hollow square */
                uint16_t stroke = sel ? RGB565(86, 86, 100) : RGB565(46, 46, 58);
                cv_rect(x, cy, cellw, 1, stroke);
                cv_rect(x, cy + cellh - 1, cellw, 1, stroke);
                cv_rect(x, cy, 1, cellh, stroke);
                cv_rect(x + cellw - 1, cy, 1, cellh, stroke);
                if (st->on && sel && lane_in_use[li])/* bare-gate tick (r16i) */
                    cv_rect(x + 1, cy + cellh - 3, cellw - 2, 2, blend(D_PAL[li % 11], C_BLACK, 115));
            }
        }
    }
    if (g.playing) {                                 /* playhead: column + 16% veil */
        int px = x0 + (pos - page * 16) * cw;
        if (pos >= page * 16 && pos < page * 16 + 16) {
            cv_rect(px, y0, cw, 11 * lh, blend(C_WHITE, C_BLACK, 219));
            cv_rect(px, y0, 1, 11 * lh, C_WHITE);
        }
    }
    cv_blit(0, 0);
    ui_draw_readout();                               /* 56px text strip */
    ui_draw_footer();
}
