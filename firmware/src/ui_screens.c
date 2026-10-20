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

/* ---- footer (prefix-free, fixed columns, 10px) ----
 * The footer is the contract "手不用看旋钮就知道在改什么" (03 §0), so the GLO
 * layer gets its own row of names: while GLO is held the four big boxes are
 * no longer STEP/LEN/MUTE/PROB, and showing the bare names would actively lie. */
static void ui_draw_footer(void)
{
    static const char *const FOOT_HOME[] = {"START", "LEN", "MUTE", "PROB"};
    static const char *const FOOT_GRID[] = {"STEP", "LEN", "MUTE", "PROB"};
    /* GLO layer on GRID: K1 START (rev.11), K3 DIV (rev.22); K2 zoom lives on
     * HOME too, so it is named here as well — the modifier is page-independent. */
    static const char *const FOOT_GLO_GRID[] = {"START", "ZOOM", "DIV", "PROB"};
    static const char *const FOOT_GLO_HOME[] = {"START", "ZOOM", "MUTE", "PROB"};
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
    } else if (glo_held && ui_mode != M_ARP) {
        const char *const *f = (ui_mode == M_HOME) ? FOOT_GLO_HOME : FOOT_GLO_GRID;
        for (k = 0; k < 4; k++)
            cv_text(FX[k], 6, &FONT_T, f[k], YELLOW);
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
 *   loop  = ONE 15% hue band anchored at t=0; past its right edge the row is
 *           "outside the loop" and drawn as a dark hatched bed (rev.23 —
 *           the row is NOT tiled with repetitions any more)
 *   meter = bar lines (brighter) and beat lines (dimmer) over the band
 *
 * NOTES: melody rows draw the four voices as separate 1px lines (same slot
 * grammar as the EDIT voice cards, so voice identity is stable) with the
 * line length = that note's gate — a sustained note is literally a long
 * line. Drum rows keep the 9-lane instrument mini-lines.
 *
 * ONE PLAYHEAD PER TRACK (rev.23): drift is a per-track fact — each loop
 * wraps at its own length — so a single shared line could only ever show the
 * global clock. Each row now draws its own head at its own position, and
 * because the meter behind it stays global, drift reads as heads crossing the
 * bar lines at different moments. */
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
    {   /* zoom readout: say how many BARS the window shows, in bars (rev.25).
         * John: "顶部直接写1 bar 4bar这样，明确表示目前hero显示出几个bar".
         * The old "x4b" was a detent label wearing a unit: nobody reads
         * "x4b" as "four bars". The ladder is still 1/2/4/8, but the
         * number is now the span itself. Lower case folds to upper case in
         * FONT_T, so this prints as "4 BAR". Space before "BAR" keeps the
         * digits and the unit from reading as one token. */
        char *b = txt_next();
        fmt_int(b, (int32_t)hero_bars);
        strcpy_(txt_end(b), " BAR");
        cv_text(168, 4, &FONT_T, b, RGB565(122, 122, 136));
    }
    for (t = 0; t < 16; t++) {
        ds_track *tr = &g.pat.track[t];
        int y = 24 + (int)t * 12;
        uint16_t col = TRACK_HUE(t);
        uint8_t muted = tr->mute;
        uint8_t is_drum = (tr->channel == 9);
        int ct = cell_ticks(tr->division);       /* ticks per step (12/8/6/4/3) */
        int loop_ticks = (int)tr->loop_len * ct; /* window length */
        int bw = step_w(ct);                     /* block width, 1px floor */
        /* pitch range of the whole track (rev.27): the hero row is a pitch
         * axis, so it needs the same pmin/pmax the GRID computes. Scanned once
         * per row per frame — 64 steps x <=4 voices, and only for melodic. */
        uint8_t pmin = 127, pmax = 0;
        if (!is_drum) {
            int r;
            for (r = 0; r < 64; r++) {
                uint8_t vi;
                for (vi = 0; vi < tr->steps[r].nvoices; vi++) {
                    uint8_t n = tr->steps[r].v[vi].note;
                    if (n == 0xFF)
                        continue;
                    if (n < pmin) pmin = n;
                    if (n > pmax) pmax = n;
                }
            }
            if (pmin > pmax) { pmin = 48; pmax = 72; }   /* empty track */
        }
        /* ---- the loop window on the ABSOLUTE step axis (rev.26) ----
         * The window is [loop_start, +loop_len) mod 64 (01 §2), so on screen it
         * starts at loop_start, not at 0. rev.23 drew it at 0 and put a note at
         * loop_start*screen-relative position instead — which made a 12-step
         * track with loop_start=4 show its first 4 slots empty and everything
         * shifted right by 4, disagreeing with the GRID by exactly loop_start
         * cells. John: "都是hero画面有问题，grid画面正常" — the GRID was right:
         * it draws absolute slot indices.
         * Spec 03 §1.1 also says turning K1 moves "亮带整体平移" — the band
         * shifts, the notes stay put. That is only true on absolute axes. */
        int bpx = tx((int)tr->loop_start * ct);  /* window start, px */
        int lpx = tx(loop_ticks);                /* window width, px */
        int wpx = bpx + lpx;                     /* window end, px */
        uint8_t s;
        if (lpx > HXW)
            lpx = HXW;
        if (wpx > HXW)
            wpx = HXW;
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
        /* ---- background 1: the loop, drawn ONCE from t=0 (rev.23) ----
         * John 2026-10-19: "不需要铺满hero —— hero 显示 4bar、trk len 8step@1/16
         * 只占 2bar，hero 就应该只显示 2bar，后面暗色表示 loop 外".
         *
         * So the row is no longer tiled with loop repetitions. One loop = one
         * bright band anchored at t=0; everything past it is "outside the
         * loop" and gets a darker, hatched bed. Tiling made every track look
         * equally busy at every zoom, and — the actual reason — it destroyed
         * the reading of DRIFT: a repeated loop has no single restart point to
         * slide against, so phase errors hid inside the repetition. With one
         * band, the band's width IS the loop length and its right edge IS where
         * the loop gives up on the window. */
        cv_rect(HX0 + bpx, y, lpx, 11,
                blend(col, RGB565(29, 29, 38), 200));   /* in-loop: hue, 15% */
        {   int x;
            /* hatching both BEFORE the window and AFTER it: on an absolute
             * axis the out-of-loop area can be on either side (loop_start>0). */
            for (x = HX0; x < HX0 + bpx; x += 4)
                cv_rect(x, y, 1, 11, RGB565(36, 36, 45));
            for (x = HX0 + wpx + ((wpx ? 1 : 0)); x < HX1; x += 4)
                cv_rect(x, y, 1, 11, RGB565(36, 36, 45));
        }
        /* ---- the loop edge: THIS track's ruler (rev.24) ----
         * John: "如果是12step的trk呢？所以不强调对齐bar / 甚至是13step".
         * At 1/16 a 12-step loop is 0.75 bar and a 13-step loop is 0.8125 bar —
         * they never land on a bar line, and a 13-step loop never lands on the
         * same place twice within a pattern. Drawing the bar grid as the
         * brightest thing on the row told the user the opposite: it implied
         * every loop is measured in bars. It is not, that is the whole point of
         * polymeter.
         *
         * So the row gets its own authoritative tick: a 2px full-saturation
         * line in the track's hue at the exact loop length. The bar grid stays
         * as CONTEXT but drops below everything else in brightness — it answers
         * "where am I in the bar", not "where does this loop end". Those are
         * different questions and only the second one is per-track. */
        if (wpx > 0 && wpx < HXW)
            cv_rect(HX0 + wpx - 1, y, 2, 11, col);
        /* ---- background 2: meter (beat 24 ticks, bar 96 ticks)
         * CONTEXT, not ruler (rev.24). John's point about 12- and 13-step
         * loops: a loop that is 0.75 or 0.8125 bars long never lands on a bar
         * line, so the bar grid cannot be the thing the eye measures loops
         * against — it only answers "where am I in the bar".
         * It therefore sits BELOW the loop band in brightness: a bar line is
         * now dimmer than the bed it crosses, so the band and its edge tick
         * stay the loudest thing on the row. Beat spacing follows the zoom, so
         * at 1 bar a beat is 48px and at 8 bars 6px — below 8px the beat lines
         * stop being drawn (they become a comb and the bar lines alone carry
         * the metric). */
        {   int k, bx = tx(24);
            int beats_per_bar = TPB / 24;            /* 4 in 4/4 */
            if (bx >= 8) {
                for (k = 0; k <= hxspan() / 24; k++) {
                    int x = HX0 + tx(k * 24);
                    if (x >= HX1)
                        break;
                    cv_rect(x, y, 1, 11,
                            (k % beats_per_bar) ? RGB565(34, 34, 42) : RGB565(42, 42, 52));
                }
            } else {                                 /* 8-bar: bar lines only */
                for (k = 0; k <= (int)hero_bars; k++) {
                    int x = HX0 + tx(k * TPB);
                    if (x >= HX1)
                        break;
                    cv_rect(x, y, 1, 11, RGB565(42, 42, 52));
                }
            }
        }
        /* ---- background 3 removed: the alternating pass stripes already
         * show where a loop restarts (see background 1). ---- */
        /* ---- notes: absolute slots, inside the window only (rev.26) ----
         * Iterate the WINDOW in play order but draw each note at its ABSOLUTE
         * slot position, so the hero and the GRID agree cell for cell. The
         * iteration order still matters for gate/next_event math (which is
         * play-order semantics); only the x mapping is absolute now. */
        for (s = 0; s < tr->loop_len; s++) {
            uint8_t si = (uint8_t)((tr->loop_start + s) & 63);
            ds_step *st = &tr->steps[si];
            int x = HX0 + tx(((int)si % 64) * ct);
            int rel_end = (int)tr->loop_len - (int)s;   /* steps left in loop */
            if (x >= HX0 + wpx)
                continue;                    /* outside the window: nothing */
            if (!st->on || !st->nvoices)
                continue;
            if (is_drum) {
                /* mask must hold NDRUM bits: at 9 lanes a uint8_t loses
                 * bit 8 (the 9th drum) silently — the SHK line simply
                 * never appeared in the hero. */
                uint16_t mask = 0;
                uint8_t li, row = 0, vi;
                cv_rect(x, y, bw, 11, blend(col, RGB565(29, 29, 38), 205));
                for (vi = 0; vi < st->nvoices; vi++) {
                    uint8_t d;
                    for (d = 0; d < NDRUM; d++)
                        if (st->v[vi].note == GM_DRUMS[d]) mask |= (uint16_t)(1u << d);
                }
                for (li = 0; li < NDRUM && row < 4; li++) {
                    if (!(mask & (1u << li)))
                        continue;
                    cv_rect(x, y + 1 + row * 3, bw, 1, D_PAL[li]);
                    row++;
                }
            } else {
            /* melodic: pitch maps VERTICALLY, same normalisation as the GRID
             * (rev.27). John 2026-10-20: "按音高画（像 GRID）：在整条轨的音域内
             * 做 pmin/pmax 映射，音高高低在 hero 上一眼可见".
             *
             * The old code stacked voices by their index in the sorted chord
             * ((sort_n-1-vi)*3), which put every single-note step on the SAME
             * row — demo trk5 spans C5/E5/G5/C6, three octaves, and all four
             * drew at dy=1. Index-into-chord is a fact about how many notes a
             * step holds, not about pitch, so the row carried no melodic
             * information at all.
             *
             * Now the row is a pitch axis: pmin..pmax of the whole track maps
             * to the 11px band, high on top. Same pmin/pmax scan and same
             * rounding as draw_grid_melodic(), so a note sits at the same
             * relative height on both screens. Line length is still the gate,
             * colour is still hue12(note). */
            uint8_t vi;
            voices_sorted(t, si);
            for (vi = 0; vi < sort_n; vi++) {
                int gsteps, gpx, ly, frac;
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
                if (x + gpx > HX0 + wpx)       /* gate stops at the window edge */
                    gpx = HX0 + wpx - x;
                /* pitch -> row y: 9 usable scanlines inside the 11px band,
                 * same span/rounding as the GRID's cell mapping */
                frac = pmax > pmin
                     ? ((int)sort_vs[vi][0] - (int)pmin) * 256 / ((int)pmax - (int)pmin)
                     : 128;
                if (frac < 0) frac = 0;
                if (frac > 256) frac = 256;
                ly = y + 1 + ((256 - frac) * 9 + 128) / 256;
                if (ly > y + 10) ly = y + 10;
                cv_rect(x, ly, gpx, 1,
                        st->prob < 100 ? blend(c, RGB565(29, 29, 38), 115) : c);
            }
        }
        }
        /* ---- the track's OWN playhead (rev.23) ----
         * John: "这样每个trk都有自己的playhead，可以表现出drift".
         * One global line can only say "this is where the shared clock is";
         * polymorphic drift is a per-track fact — each loop wraps at its own
         * length, so the rows must show where each one actually is. Because
         * every row's time mapping is its own (cells of its own division,
         * band of its own loop length) while the meter behind them is global,
         * drift reads as playheads crossing the bar lines at different times.
         * Position = steps into the loop + the sub-cell fraction, in ticks:
         * the same clock the engine schedules from, so the line cannot lie
         * about the audio. */
        if (g.playing) {
            /* absolute slot of this track's head (rev.26): pos16 already IS an
             * absolute slot index — the old code subtracted loop_start to get a
             * play-order offset, which only worked while notes were also drawn
             * play-order. Both are absolute now. */
            int ph = HX0 + tx((int)((double)g.pos16[t] * ct + g.frac16[t] * ct));
            if (ph >= HX1)
                ph = HX1 - 1;
            cv_rect(ph, y, 1, 11, C_WHITE);
        }
outline:
        if (t == sel_track) {                        /* selected: green outline at 46 / 239 */
            cv_rect(HX0 - 1, y - 1, (HX1 - HX0) + 2, 1, RGB565(127, 217, 74));
            cv_rect(HX0 - 1, y + 11, (HX1 - HX0) + 2, 1, RGB565(127, 217, 74));
            cv_rect(HX0 - 1, y, 1, 11, RGB565(127, 217, 74));
            cv_rect(HX1, y, 1, 11, RGB565(127, 217, 74));
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
    /* DIV readout (rev.22): GLO+K3 changes the division on this page, so the
     * value has to be visible here — otherwise the twist has no feedback until
     * the user walks back to HOME. Column 96 for BOTH grid pages: same value,
     * same place — the eye should not have to hunt per track type. */
    cv_text(96, 2, &FONT_T, div_str(tr->division), RGB565(122, 122, 136));
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

/* ---- DRUM GRID (03 §3): NDRUM lanes x 11px (7px cell + 4px gap), cells 9x9
 * GAP4, hollow/solid, gate dimming, playhead column, readout strip 56px.
 * Vertical budget (rev.13b): 16 top + 9x11 lanes (99) + 4x25 panel (100) +
 * 24 footer = 239px. Same main-area height as the melodic 4x24 grid. */
static void draw_grid_drum(void)
{
    uint8_t page = cursor >> 4;
    int li, c, x0 = 30, y0 = 16, lh = 11, gap = 4;
    int cw = (240 - x0) / 16;                        /* 13 */
    uint8_t lane_in_use[NDRUM];
    int pos = (int)g.pos16[sel_track];
    for (li = 0; li < NDRUM; li++) {                 /* r16i: which drums are voiced? */
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
    cv_text(96, 2, &FONT_T, div_str(TRK()->division), RGB565(122, 122, 136));
    cv_text(150, 2, &FONT_T, txt_join(txt_st(cursor + 1), GM_SHORT[drum_sel]),
            TRK()->mute ? MUTE_GRAY : GREEN_OK);
    if (focus_layer)
        ui_draw_edit_badge();
    for (li = 0; li < NDRUM; li++) {
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
                uint16_t col = blend(D_PAL[li], C_BLACK, sel ? 0 : 166);
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
                    cv_rect(x + 1, cy + cellh - 3, cellw - 2, 2, blend(D_PAL[li], C_BLACK, 115));
            }
        }
    }
    if (g.playing) {                                 /* playhead: column + 16% veil */
        int px = x0 + (pos - page * 16) * cw;
        if (pos >= page * 16 && pos < page * 16 + 16) {
            cv_rect(px, y0, cw, NDRUM * lh, blend(C_WHITE, C_BLACK, 219));
            cv_rect(px, y0, 1, NDRUM * lh, C_WHITE);
        }
    }
    cv_blit(0, 0);
    ui_draw_drum_card();                             /* 100px single-drum card */
    ui_draw_footer();
}
