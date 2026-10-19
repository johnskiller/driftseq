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

/* ---- HOME hero: ABSOLUTE TIME axis (rev.19, P = John's pick) ----
 * top bar 20px: TRK nn | PTN 01 | BPM | DIV, right: S nn
 * 16 rows x 12px (y24..216): gutter TRK nn / LEN nn / route U / M.
 *
 * WHY A TIME AXIS: divisions are per-track (01 §1), so a step-index axis
 * silently assumes "all tracks have the same cell length" — a 12-step 1/8
 * loop is twice as long as a 12-step 1/16 loop but drew the same width, so
 * the phase drift on screen was a lie. The internal clock is 24 PPQ and all
 * five divisions divide a quarter exactly (12 / 8 / 6 / 4 / 3 ticks), so
 * there is an exact common grid: x = tick, 0.5px per tick.
 *
 *   4/4 bar = 16 sixteenths = 96 ticks = 48px;  beat = 24 ticks = 12px
 *   span    = 4 bars = 384 ticks = 192px = x47..238
 *   cell px = 6 / 4 / 3 / 2 / 1.5  for 1/8 1/8T 1/16 1/16T 1/32
 *   block w = cell px - 1 gap, min 1px (John: 1/32 is always 1px)
 *
 * 1/16 keeps exactly the rev.18 look (3px pitch, 2px block, 1px gap).
 *
 * BACKGROUND carries both references John asked for:
 *   loop  = 15% hue band per pass + a 40% hue seam at each pass restart
 *           (seam suppressed when a pass is < 8px wide, else it is noise)
 *   meter = bar lines (brighter) and beat lines (dimmer) over the band
 *
 * NOTES: melody rows draw the four voices as separate 1px lines (same slot
 * grammar as the EDIT voice cards, so voice identity is stable) with the
 * line length = that note's gate — a sustained note is literally a long
 * line. Drum rows keep the 9-lane instrument mini-lines.
 *
 * ONE global playhead: every track shares the axis now, so a single white
 * line sweeps the whole grid; drift reads as notes sliding against the bar
 * lines instead of sixteen playheads drifting apart. */
#define TPB      96                        /* ticks per 4/4 bar @24 PPQ */
#define HX0      47
#define HXW      192                       /* bar area is always 192px wide */
#define HX1      (HX0 + HXW)               /* 239, independent of zoom */
/* ZOOM (rev.20): the window spans 1/2/4/8 bars but keeps 192px, so zooming
 * changes TICKS PER PIXEL, not the drawing width:
 *   1 bar  96t / 192px = 2.0 px/tick   (1/16 cell = 12px)
 *   2 bar 192t / 192px = 1.0 px/tick   (1/16 cell =  6px)
 *   4 bar 384t / 192px = 0.5 px/tick   (1/16 cell =  3px)  <- default
 *   8 bar 768t / 192px = 0.25px/tick   (1/16 cell = 1.5px)
 * John's original point: 64 steps is the ceiling but most tracks use 16, so
 * at 4 bars a 16-step 1/16 loop only filled a quarter of the screen. */
static uint8_t hero_bars = 4;              /* 1, 2, 4 or 8 */
static int hxspan(void) { return TPB * (int)hero_bars; }
/* tick -> x offset within the bar area (exact integer math, 192/span) */
static int tx(int tick) { return (tick * HXW) / hxspan(); }

/* cell length in 24-PPQ ticks — every division is an exact integer */
static int cell_ticks(uint8_t div)
{
    switch (div) {
    case DS_DIV_8:   return 12;
    case DS_DIV_8T:  return 8;
    case DS_DIV_16:  return 6;
    case DS_DIV_16T: return 4;
    case DS_DIV_32:  return 3;
    default:         return 6;
    }
}
static const char *div_str(uint8_t div)
{
    switch (div) {
    case DS_DIV_8:   return "1/8";
    case DS_DIV_8T:  return "1/8T";
    case DS_DIV_16:  return "1/16";
    case DS_DIV_16T: return "1/16T";
    case DS_DIV_32:  return "1/32";
    default:         return "1/16";
    }
}
/* block width for a cell of ct ticks: cell px minus the 1px gap, min 1.
 * MUST go through tx() — a zoom-independent width would make blocks wider
 * than the cell pitch once zoomed out (at 8 bars a 1/16 cell is 1.5px). */
static int step_w(int ct) { int w = tx(ct) - 1; return w < 1 ? 1 : w; }

/* distance (in steps) from s to the next step holding a voice, within the
 * window; 0xFF gate means "hold until that event or the loop end" */
static int next_event_run(uint8_t t, uint8_t s, uint8_t len, uint8_t start)
{
    ds_track *tr = &g.pat.track[t];
    int rel = (int)((s + 64 - start) & 63), d;
    for (d = 1; d < (int)len - rel; d++) {
        ds_step *nx = &tr->steps[(start + rel + d) & 63];
        if (nx->on && nx->nvoices)
            return d;
    }
    return (int)len - rel;
}
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
        strcpy_(txt_end(b), "-");
        strcpy_(txt_end(b), div_str(TRK()->division));
        cv_text(6, 4, &FONT_T, b, TB_TX);
    }
    cv_text(204, 4, &FONT_T, txt_join("S", txt_num(0, TRK()->loop_start)),
            TRK()->mute ? MUTE_GRAY : GREEN_OK);
    {   /* zoom readout: the window is 1/2/4/8 bars; "x4" tells the user how
         * much time the 192px bar area covers, so a loop's on-screen width is
         * never mistaken for its real length. */
        char *b = txt_next();
        strcpy_(b, "x");
        fmt_int(txt_end(b), (int32_t)hero_bars);
        cv_text(168, 4, &FONT_T, b, RGB565(122, 122, 136));
    }
    for (t = 0; t < 16; t++) {
        ds_track *tr = &g.pat.track[t];
        int y = 24 + (int)t * 12;
        uint16_t col = TRACK_HUE(t);
        uint8_t muted = tr->mute;
        uint8_t is_drum = (tr->channel == 9);
        int ct = cell_ticks(tr->division);       /* ticks per step (12/8/6/4/3) */
        int loop_ticks = (int)tr->loop_len * ct; /* one full window pass */
        int bw = step_w(ct);                     /* block width, 1px floor */
        int npass = loop_ticks > 0 ? (hxspan() + loop_ticks - 1) / loop_ticks : 1;
        int pass;
        uint8_t s;
        /* gutter (46px): TRK nn | LEN nn | route | M */
        cv_text(2, y, &FONT_T, txt_num(0, t + 1), muted ? MUTE_GRAY : RGB565(154, 154, 168));
        cv_text(15, y, &FONT_T, txt_num(0, tr->loop_len), muted ? RGB565(74, 74, 82) : RGB565(106, 106, 120));
        cv_text(28, y, &FONT_T, "U", muted ? RGB565(74, 74, 82) : RGB565(122, 122, 136));
        if (muted)
            cv_text(36, y, &FONT_T, "M", RGB565(224, 160, 64));
        cv_rect(HX0, y, HX1 - HX0, 11, muted ? RGB565(20, 20, 24) : RGB565(29, 29, 38));
        if (muted) {
            if (t == sel_track)
                goto outline;
            continue;
        }
        /* ---- background 1: loop passes, ALTERNATING brightness ----
         * Each pass is one full loop of this track, so the stripe pattern
         * directly shows both loop LENGTH (how wide one stripe is) and
         * POSITION (where the stripes break) on the shared time axis.
         * Alternating 15% / 8% hue beats a 1px seam line: a restart always
         * coincides with step 0, and step 0 usually holds a note, so a seam
         * would be painted over by that note's block. Opacity alternation
         * never collides and stays readable at any pass width. */
        for (pass = 0; pass < npass; pass++) {
            int x0 = HX0 + tx(pass * loop_ticks);
            int xw = tx(loop_ticks);
            if (x0 >= HX1)
                break;
            if (x0 + xw > HX1)
                xw = HX1 - x0;
            cv_rect(x0, y, xw, 11,
                    blend(col, RGB565(29, 29, 38), (pass & 1) ? 255 : 200));
        }
        /* ---- background 2: meter (beat 24 ticks, bar 96 ticks) ----
         * Global, identical on every row: this is the reference that makes
         * "12-step 1/8 restarts mid-bar" readable at a glance. Beat spacing
         * follows the zoom, so at 1 bar a beat is 48px and at 8 bars 6px —
         * below 8px the beat lines stop being drawn (they become a comb and
         * the bar lines alone carry the metric). */
        {   int k, bx = tx(24);
            int beats_per_bar = TPB / 24;            /* 4 in 4/4 */
            if (bx >= 8) {
                for (k = 0; k <= hxspan() / 24; k++) {
                    int x = HX0 + tx(k * 24);
                    if (x >= HX1)
                        break;
                    cv_rect(x, y, 1, 11,
                            (k % beats_per_bar) ? RGB565(44, 44, 54) : RGB565(64, 64, 78));
                }
            } else {                                 /* 8-bar: bar lines only */
                for (k = 0; k <= (int)hero_bars; k++) {
                    int x = HX0 + tx(k * TPB);
                    if (x >= HX1)
                        break;
                    cv_rect(x, y, 1, 11, RGB565(64, 64, 78));
                }
            }
        }
        /* ---- background 3 removed: the alternating pass stripes already
         * show where a loop restarts (see background 1). ---- */
        /* ---- notes: one drawing pass per loop repetition ---- */
        for (pass = 0; pass < npass; pass++) {
            int tbase = pass * loop_ticks;
            if (tbase >= hxspan())
                break;
            for (s = 0; s < tr->loop_len; s++) {
                uint8_t si = (uint8_t)((tr->loop_start + s) & 63);
                ds_step *st = &tr->steps[si];
                int x = HX0 + tx(tbase + (int)s * ct);
                int rel_end = (int)tr->loop_len - (int)s;   /* steps left in pass */
                if (x >= HX1)
                    break;
                if (!st->on || !st->nvoices)
                    continue;
                if (is_drum) {
                    /* rev.14e: 9-lane instrument mini-lines, top->bottom */
                    uint8_t mask = 0, li, row = 0, vi;
                    cv_rect(x, y, bw, 11, blend(col, RGB565(29, 29, 38), 205));
                    for (vi = 0; vi < st->nvoices; vi++) {
                        uint8_t d;
                        for (d = 0; d < 11; d++)
                            if (st->v[vi].note == GM_DRUMS[d]) mask |= (uint8_t)(1u << d);
                    }
                    for (li = 0; li < 9 && row < 4; li++) {
                        if (!(mask & (1u << li)))
                            continue;
                        cv_rect(x, y + 1 + row * 3, bw, 1, D_PAL[li % 11]);
                        row++;
                    }
                } else {
                    /* melodic: four voices as four 1px lines, pitch-ascending
                     * bottom->top, line color = hue12(note) (same pitch color
                     * as the GRID). Line LENGTH = gate, so a sustained note is
                     * literally a long line and staccato is a dot. */
                    uint8_t vi;
                    voices_sorted(t, si);
                    for (vi = 0; vi < sort_n; vi++) {
                        int gsteps, gpx, ly;
                        uint8_t g = sort_vs[vi][2];
                        uint16_t c = hue12(sort_vs[vi][0]);
                        if (g == 0xFF)
                            gsteps = next_event_run(t, si, tr->loop_len, tr->loop_start);
                        else
                            gsteps = g;
                        if (gsteps > rel_end)          /* clamp at loop end */
                            gsteps = rel_end;
                        gpx = tx(gsteps * ct);
                        if (gpx < 1)
                            gpx = 1;
                        if (x + gpx > HX1)
                            gpx = HX1 - x;
                        ly = y + 1 + (sort_n - 1 - vi) * 3;   /* high pitch on top */
                        cv_rect(x, ly, gpx, 1,
                                st->prob < 100 ? blend(c, RGB565(29, 29, 38), 115) : c);
                    }
                }
            }
        }
outline:
        if (t == sel_track) {                        /* selected: green outline at 46 / 239 */
            cv_rect(HX0 - 1, y - 1, (HX1 - HX0) + 2, 1, RGB565(127, 217, 74));
            cv_rect(HX0 - 1, y + 11, (HX1 - HX0) + 2, 1, RGB565(127, 217, 74));
            cv_rect(HX0 - 1, y, 1, 11, RGB565(127, 217, 74));
            cv_rect(HX1, y, 1, 11, RGB565(127, 217, 74));
        }
    }
    /* ONE global playhead: every track shares this axis, so a single white
     * line sweeps the grid and wraps once per zoom window. */
    if (g.playing) {
        int ph = (int)g.tick_acc % hxspan();
        cv_rect(HX0 + tx(ph), 24, 1, 192, C_WHITE);
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
