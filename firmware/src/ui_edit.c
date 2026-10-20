/* ui_edit.c — EDIT panel (rev.12 embedded, melodic) + drum readout strip +
 * ARP page + encoder/button routing (part of ui.c unity section). Ported
 * from web/emu.js r16j: drawEditPanel / drum readout / drawArp. */

/* ---- melodic EDIT panel: 4 voice cards (r16j drawEditPanel verbatim) ----
 * card bg luminance = velocity, hue bar length = gate, 5-segment text
 * v<sym> NOTE g<n> v<n> p<n> at fixed columns, 1px dark shadow under text. */
static void ui_draw_edit_panel(void)
{
    ds_step *st = &TRK()->steps[cursor];
    int r;
    voices_sorted(sel_track, cursor);
    cv_begin(240, 100, RGB565(16, 16, 21));          /* screen y112..212 (r16j) */
    cv_rect(0, 0, 240, 1, RGB565(58, 58, 68));       /* separator at y112 */
    for (r = 0; r < 4; r++) {
        int y = r * 25;                              /* 4 x 25 = 100 exact */
        uint8_t active = focus_layer && r == edit_voice;
        if (r >= sort_n) {                           /* empty slot: plain dark */
            cv_rect(2, y + 1, 236, 23, RGB565(22, 22, 27));
            cv_text(8, y + 6, &FONT_XS, "--", RGB565(85, 85, 95));
        } else {
            uint8_t note = sort_vs[r][0], vel = sort_vs[r][1], gate = sort_vs[r][2];
            uint8_t prob = st->prob;
            uint16_t hue = hue12(note);
            uint16_t desat = blend(hue, RGB565(128, 128, 128), (uint8_t)(255 - prob * 255 / 100));
            int gspan = (gate == 0xFF) ? 234 : (int)(((gate / 4.0) > 8.0 ? 8.0 : gate / 4.0) * 29);
            /* whole-row bg = velocity luminance (18%..73% gray) */
            cv_rect(3, y + 1, 234, 23, blend(RGB565(186, 186, 186), RGB565(46, 46, 46), (uint8_t)(255 - vel * 255 / 127)));
            cv_rect(3, y + 1, gspan < 3 ? 3 : gspan, 23, desat);   /* bar = gate */
            if (gate == 0xFF && g.playing)
                cv_rect(234, y + 1, 3, 23, C_WHITE);
            {   /* text: "v1" NOTE "g" GATE "v" VEL "p" PROB at fixed columns */
                int xs[5] = {7, 34, 84, 140, 188};
                uint16_t tcol = active ? WARMWHITE : BRIGHTTX;
                char *seg = txt_next();
                seg[0] = 'v';
                seg[1] = (char)('1' + r);
                seg[2] = ' ';
                seg[3] = st->on ? (char)((g.playing && g.pos16[sel_track] == cursor) ? '>' : 'o') : '-';
                seg[4] = 0;
                cv_text(xs[0], y + 5, &FONT_XS, seg, tcol);
                cv_text(xs[1], y + 5, &FONT_XS, note_name2(note), tcol);
                cv_text(xs[2], y + 5, &FONT_XS, txt_join("g", gate_str(gate)), tcol);
                cv_text(xs[3], y + 5, &FONT_XS, txt_join("v", txt_num(0, vel)), tcol);
                cv_text(xs[4], y + 5, &FONT_XS, txt_join("p", txt_num(0, prob)), tcol);
            }
        }
        if (active) {                                /* r16j: yellow card outline */
            cv_rect(2, y, 236, 1, YELLOW);
            cv_rect(2, y + 24, 236, 1, YELLOW);
            cv_rect(2, y, 1, 25, YELLOW);
            cv_rect(237, y, 1, 25, YELLOW);
        }
    }
    cv_blit(0, 112);
}

/* ---- drum single-drum card (03 §3.1 rev.13c): the panel is ONE enlarged card
 * for the selected drum, not a list of every drum at this step. John's call:
 * "同时编辑一个 step 的不同鼓" is a rare need — the lanes panorama above already
 * answers "what is on this step"; fine editing is always one object. Swapping
 * instruments = one black key, so two cards side by side would only cost space.
 *
 * Layout: lanes end at y=16+9*11=115, so the card owns y115..215 (100px) and
 * the footer 216..239. Big numbers + three full-width parameter bars (the
 * same bg-luminance/hue-length/gray-out grammar as the melodic EDIT cards),
 * so the value can be read as a number OR as a shape. */
static void ui_draw_drum_card(void)
{
    ds_step *st = &TRK()->steps[cursor];
    int hit = -1, vi;
    for (vi = 0; vi < st->nvoices; vi++)
        if (st->v[vi].note == GM_DRUMS[drum_sel]) hit = vi;
    cv_begin(240, 100, RGB565(16, 16, 21));
    cv_rect(0, 0, 240, 1, RGB565(58, 58, 68));       /* separator at y115 */
    cv_rect(2, 3, 236, 94, focus_layer ? RGB565(46, 46, 58) : RGB565(22, 22, 27));
    if (focus_layer) {                               /* yellow = editing this card */
        cv_rect(2, 3, 236, 1, YELLOW);
        cv_rect(2, 96, 236, 1, YELLOW);
        cv_rect(2, 3, 1, 94, YELLOW);
        cv_rect(237, 3, 1, 94, YELLOW);
    }
    /* identity: 3-char drum name + step number, in the instrument's own colour */
    cv_text(10, 7, &FONT_M, GM_SHORT[drum_sel], D_PAL[drum_sel]);
    cv_text(78, 12, &FONT_T, txt_st(cursor + 1), RGB565(122, 122, 136));
    if (hit < 0) {                                   /* not voiced on this step */
        cv_text(10, 44, &FONT_T, "off", RGB565(85, 85, 95));
        cv_text(10, 66, &FONT_T, "white key = toggle", RGB565(85, 85, 95));
        cv_text(10, 82, &FONT_T, "K1 VEL  K2 GAT  K3 PROB", RGB565(106, 106, 118));
        cv_blit(0, 115);
        return;
    }
    {
        uint8_t vel = st->v[hit].vel, gate = st->v[hit].gate, prob = st->prob;
        uint16_t hue = D_PAL[drum_sel];
        char *b;
        /* VEL row: bg luminance = velocity (same grammar as the melodic cards) */
        cv_rect(3, 34, 234, 20, blend(RGB565(186, 186, 186), RGB565(46, 46, 46),
                                      (uint8_t)(255 - vel * 255 / 127)));
        b = txt_next(); strcpy_(b, "VEL ");
        fmt_int(txt_end(b), vel);
        cv_text(8, 38, &FONT_T, b, RGB565(20, 20, 24));
        /* GATE row: hue bar length = gate (8 steps = full), like EDIT PIT/GAT */
        cv_rect(3, 56, 234, 20, RGB565(30, 30, 36));
        cv_rect(3, 56, gate == 0xFF ? 234 : (gate > 8 ? 234 : (int)gate * 29 + 3), 20,
                blend(hue, RGB565(30, 30, 36), (uint8_t)(255 - prob * 255 / 100)));
        b = txt_next(); strcpy_(b, "GAT ");
        if (gate == 0xFF) strcpy_(txt_end(b), "hold");
        else fmt_int(txt_end(b), gate);
        cv_text(8, 60, &FONT_T, b, C_WHITE);
        /* PROB row: gray-out = probability (never a colour change: the hue is
         * the instrument's identity, not a parameter) */
        cv_rect(3, 78, 234, 20, RGB565(30, 30, 36));
        cv_rect(3, 78, prob * 234 / 100, 20, blend(hue, RGB565(30, 30, 36),
                                                    (uint8_t)(prob * 255 / 100)));
        b = txt_next(); strcpy_(b, "PROB ");
        fmt_int(txt_end(b), prob);
        cv_text(8, 82, &FONT_T, b, C_WHITE);
    }
    cv_blit(0, 115);
}

/* ---- ARP (r16j drawArp verbatim): strip y16..28, ring center (120,112)
 * r76, ticks/pulses on the circle, north pointer, numbers row y196..216. ---- */
static const int16_t RING_SIN[64] = {0,175,349,519,684,843,995,1138,1271,1393,1503,1601,1686,1757,1815,1858,1886,1899,1897,1880,1847,1800,1739,1664,1576,1476,1364,1242,1111,972,826,673,516,355,192,27,-138,-305,-470,-632,-789,-941,-1086,-1223,-1352,-1471,-1580,-1677,-1762,-1833,-1889,-1929,-1954,-1962,-1954,-1930,-1889,-1833,-1762,-1677,-1580,-1471,-1352,-1223};
static void ring_pt(int i, int n, int rad, int cx, int cy, int *px, int *py)
{
    int a = i * 64 / n;                              /* north = slot 0, clockwise */
    *px = cx + RING_SIN[a & 63] * rad / 1962;
    *py = cy - RING_SIN[(a + 16) & 63] * rad / 1962;
}

static void draw_arp(void)
{
    ds_track *tr = TRK();
    uint8_t eu = tr->eu_on, k0 = tr->eu_k, n0 = tr->eu_n;
    int i, cx = 120, cy = 112, r = 76;
    uint8_t pat[64];
    uint8_t n, k, rot;
    uint8_t start = tr->loop_start, len = tr->loop_len;
    int pos = (int)g.pos16[sel_track];
    uint8_t is_drum = (tr->channel == 9);
    uint16_t tcol = TRACK_HUE(sel_track % 11);
    int sx = 47, sw = 192, sy = 16, sh = 12;         /* rev.18: hero grammar = 64 x 3px (2+1) = 192 */

    cv_begin(240, 224, C_BLACK);
    cv_rect(0, 0, 240, 16, TB_BG);
    /* rev.25: topbar text sits at y=4 like HOME's. It was y=2 here — FONT_T is
     * 8px tall, so inside a 16px bar y=4 is centred and y=2 is visibly high.
     * Caught by the probe that compares the ARP readout band with HOME's
     * pixel for pixel: same string, different baseline, so it failed. */
    cv_text(6, 4, &FONT_T, txt_join("ARP TRK", txt_num(0, sel_track + 1)), TB_TX);
    /* zoom, same readout grammar as HOME: the strip below is the same time
     * axis, so its scale must be readable here too (rev.21). rev.25: same
     * "N BAR" wording as HOME — two views of one axis must not describe the
     * same span in two different notations. */
    {   char *b = txt_next();
        fmt_int(b, (int32_t)hero_bars);
        strcpy_(txt_end(b), " BAR");
        cv_text(112, 4, &FONT_T, b, RGB565(122, 122, 136));
    }
    cv_text(192, 4, &FONT_T, eu ? "*LINK" : "FREE", eu ? GREEN_OK : RGB565(106, 106, 118));
    if (!eu && k0 == 0 && n0 == 0) {                 /* fresh track: hint page */
        cv_text(70, 112, &FONT_XS, "K4 - generate", RGB565(85, 85, 95));
        cv_text(64, 202, &FONT_XS, "K -  N -  ROT -", RGB565(138, 138, 153));
        cv_text(168, 202, &FONT_XS, "ARP v1.1", YELLOW);
        cv_blit(0, 0);
        ui_draw_footer();
        return;
    }
    n = n0 ? n0 : 16;
    k = k0 < n ? k0 : n;
    rot = tr->eu_rot % n;
    {   /* the pre-window pattern: engine's own math, recomputed for the ring */
        ds_bjorklund(n, k, pat);
    }
    /* ---- 64-step result strip: same ABSOLUTE TIME axis as the hero (rev.19),
     * same zoom (rev.21) and same one-loop-only grammar (rev.23). 64 steps of a
     * 1/16 track = 384 ticks = the whole 192px span at x4, so the strip lines
     * up 1:1 with the HOME hero; at other zooms it must follow tx() or the two
     * views silently disagree about where the bar lines are.
     *
     * rev.23: the strip used to tile the loop across the window. The hero no
     * longer does, and the strip claims to be the SAME axis — repeating here
     * while the hero shows a single band would make the two views disagree
     * about how much time is on screen. One loop, then dark. */
    cv_rect(0, sy, 240, sh, TB_BG);
    cv_rect(sx, sy, sw, sh, RGB565(34, 34, 42));
    {   int ct = cell_ticks(tr->division);
        int bw = step_w(ct);
        int loop_ticks = (int)len * ct;
        int lpx = tx(loop_ticks);
        if (lpx > sw) lpx = sw;
        if (lpx > 0)
            cv_rect(sx, sy, lpx, sh, RGB565(42, 42, 51));   /* in-loop bed */
        {   int k, bx = tx(24);                      /* meter, zoom-aware */
            int beats_per_bar = TPB / 24;
            if (bx >= 8) {
                for (k = 0; k <= hxspan() / 24; k++) {
                    int x = sx + tx(k * 24);
                    if (x >= sx + sw) break;
                    cv_rect(x, sy, 1, sh,
                            (k % beats_per_bar) ? RGB565(48, 48, 60) : RGB565(68, 68, 82));
                }
            } else {
                for (k = 0; k < (int)hero_bars; k++) {
                    int x = sx + tx(k * TPB);
                    if (x >= sx + sw) break;
                    cv_rect(x, sy, 1, sh, RGB565(68, 68, 82));
                }
            }
        }
        {   for (i = 0; i < (int)len; i++) {
                uint8_t idx, on;
                int px;
                idx = (uint8_t)(((i % n) + n - rot % n) % n);
                if (!pat[idx]) continue;
                on = tr->steps[(start + i) & 63].on;
                if (!on) continue;
                px = sx + tx(i * ct);
                if (px >= sx + lpx) break;
                if (is_drum) {                       /* lane mini-lines */
                    uint16_t mask = 0;               /* 9 lanes need 9 bits */
                    uint8_t vi, li, row = 0;
                    cv_rect(px, sy, bw, sh, blend(tcol, RGB565(24, 24, 32), 205));
                    for (vi = 0; vi < tr->steps[(start + i) & 63].nvoices; vi++) {
                        uint8_t d;
                        for (d = 0; d < NDRUM; d++)
                            if (tr->steps[(start + i) & 63].v[vi].note == GM_DRUMS[d])
                                mask |= (uint16_t)(1u << d);
                    }
                    for (li = 0; li < NDRUM && row < 4; li++) {
                        if (!(mask & (1u << li))) continue;
                        cv_rect(px, sy + 1 + row * 3, bw, 1, D_PAL[li]);
                        row++;
                    }
                } else {
                    cv_rect(px, sy, bw, sh, tcol);
                }
            }
        }
    }
    if (g.playing) {                                  /* this track's own head */
        int rel = (int)((g.pos16[sel_track] + 64u - start) & 63u);
        if (rel < (int)len) {
            int ph = sx + tx((int)((double)rel * cell_ticks(tr->division) +
                                   g.frac16[sel_track] * cell_ticks(tr->division)));
            cv_rect(ph, sy, 1, sh, C_WHITE);
        }
    }
    cv_text(30, sy + 2, &FONT_T, "LOOP", RGB565(106, 106, 118));
    /* ---- ring: outer guide, ticks, pulses (disc + white core), north pin ---- */
    {
        int a;
        for (a = 0; a < 64; a++) {                   /* guide circle r+4 */
            int gx = cx + RING_SIN[a & 63] * (r + 4) / 1962;
            int gy = cy - RING_SIN[(a + 16) & 63] * (r + 4) / 1962;
            cv_pset(gx, gy, RGB565(58, 58, 68));
        }
        for (i = 0; i < n; i++) {
            int px2, py2;
            uint8_t pulse = pat[(uint8_t)(((i % n) + n - (rot % n)) % n)];
            ring_pt(i, n, r, cx, cy, &px2, &py2);
            if (pulse) {                             /* drawn after ticks */
                cv_disc(px2, py2, 3, tcol);
                cv_disc(px2, py2, 1, blend(C_WHITE, C_BLACK, 38));
            } else {
                cv_rect(px2 - 1, py2 - 1, n > 32 ? 1 : 2, n > 32 ? 1 : 2,
                        n > 32 ? RGB565(74, 74, 84) : RGB565(106, 106, 118));
            }
            if (i == 0) {                            /* north pointer */
                int nx, ny;
                ring_pt(0, n, r - 10, cx, cy, &nx, &ny);
                cv_line(px2, py2, nx, ny, RGB565(138, 138, 153));
            }
        }
    }
    if (g.playing) {                                 /* playhead dot outside ring */
        int rel = (pos + 64 - start) & 63;
        if (rel < (int)len) {
            int ph = rel % n, px, py;
            int a = ph * 64 / n;
            px = cx + RING_SIN[a & 63] * (r + 8) / 1962;
            py = cy - RING_SIN[(a + 16) & 63] * (r + 8) / 1962;
            cv_disc(px, py, 2, C_WHITE);
        }
    }
    /* ---- numbers row ---- */
    cv_rect(0, 196, 240, 20, TB_BG);
    cv_text(10, 204, &FONT_T, txt_join("K", txt_num(0, k)), BRIGHTTX);
    cv_text(64, 204, &FONT_T, txt_join("N", txt_num(0, n)), BRIGHTTX);
    cv_text(118, 204, &FONT_T, txt_join("ROT", txt_num(0, rot)), BRIGHTTX);
    cv_text(168, 204, &FONT_T, "ARP v1.1", YELLOW);
    cv_blit(0, 0);
    ui_draw_footer();
}

/* ---- GLO+K2 = hero zoom (rev.20; rev.21 makes it page-independent).
 * Zoom is a DISPLAY state, not a per-page parameter: the hero and the ARP
 * strip are two views of the same time axis, so the wheel must work wherever
 * one of them is on screen. Before rev.21 this lived only in ui_grid_encoder
 * — the one page that does not draw the hero — so the HOME screen, where the
 * `x4` readout sits, could not move it. */
static void zoom_encoder(int8_t det)
{
    int b = (int)hero_bars;
    if (det > 0) b = (b == 1) ? 2 : (b == 2) ? 4 : 8;
    else         b = (b == 8) ? 4 : (b == 4) ? 2 : 1;
    hero_bars = (uint8_t)b;
}

static void loop_start_encoder(int8_t det)    /* dead-stop 0..63 (spec 02 §4) */
{
    int v = TRK()->loop_start + det;
    if (v < 0) v = 0;
    if (v > 63) v = 63;
    TRK()->loop_start = (uint8_t)v;
}

/* DIVISION (rev.22, John 2026-10-19: "DIV现在没有设定的方法，在grid screen
 * 设置比较好"). A per-track step length (01 §1), so it belongs on the page
 * that draws the steps — the HOME top bar only DISPLAYS it. Five values, dead
 * stop at both ends like every other detent in this UI: cycling would let a
 * blind twist wrap past 1/32 back to 1/8 unnoticed. */
static void track_div_encoder(int8_t det)
{
    int v = (int)TRK()->division + det;
    if (v < DS_DIV_8) v = DS_DIV_8;
    if (v > DS_DIV_32) v = DS_DIV_32;
    TRK()->division = (uint8_t)v;
}

/* ---- per-page encoder behavior ---- */
static void ui_home_encoder(uint8_t enc, int8_t det)
{
    ds_track *tr = TRK();
    if (enc == EN_K2 && glo_held) {              /* GLO+K2 = hero zoom (rev.20/21) */
        zoom_encoder(det);
        return;
    }
    if (enc == EN_K1) {                          /* K1 LOOP START (dead-stop) */
        loop_start_encoder(det);
    } else if (enc == EN_K2) {                   /* K2 LEN 1..64 */
        int v = tr->loop_len + det;
        if (v < 1) v = 1;
        if (v > 64) v = 64;
        tr->loop_len = (uint8_t)v;
    } else if (enc == EN_K3 && det > 0) {        /* K3 MUTE: right = on */
        tr->mute = tr->mute ? 0 : 1;
    } else if (enc == EN_K4) {                   /* K4 PROB 0..100 */
        int v = tr->steps[cursor].prob + det * 5;
        if (v < 0) v = 0;
        if (v > 100) v = 100;
        tr->steps[cursor].prob = (uint8_t)v;
    }
}

static void ui_grid_encoder(uint8_t enc, int8_t det)
{
    if (enc == EN_K2 && glo_held) {              /* GLO+K2 = hero zoom (rev.20/21) */
        zoom_encoder(det);
        return;
    }
    if (enc == EN_K1 && glo_held) {              /* GLO+K1 = LOOP START (rev.11) */
        loop_start_encoder(det);
        return;
    }
    if (enc == EN_K3 && glo_held) {              /* GLO+K3 = DIVISION (rev.22) */
        track_div_encoder(det);
        return;
    }
    if (enc == EN_K1) cursor_move(det);          /* K1 cursor (bare: the fast path) */
    else if (enc == EN_K2) ui_home_encoder(EN_K2, det);   /* K2 LEN */
    else if (enc == EN_K3 && det > 0) { TRK()->mute ^= 1; }
    else if (enc == EN_K4) ui_home_encoder(EN_K4, det);   /* K4 PROB */
}

static void ui_edit_encoder(uint8_t enc, int8_t det)
{
    ds_step *st = &TRK()->steps[cursor];
    if (is_drum_trk()) {                         /* r16f: drum VEL GAT PROB */
        int hit = -1, vi;
        for (vi = 0; vi < st->nvoices; vi++)
            if (st->v[vi].note == GM_DRUMS[drum_sel]) hit = vi;
        if (hit < 0) return;
        if (enc == EN_K1) {
            int v = st->v[hit].vel + det * 2;
            if (v < 1) v = 1;
            if (v > 127) v = 127;
            st->v[hit].vel = (uint8_t)v;
        } else if (enc == EN_K2) {
            int v = st->v[hit].gate + det;
            if (v < 1) v = 1;
            if (v > 32) v = 32;
            st->v[hit].gate = (uint8_t)v;
        } else if (enc == EN_K3) {
            int v = st->prob + det * 5;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            st->prob = (uint8_t)v;
        }
        return;
    }
    /* melodic: PIT GAT VEL PROB SEL */
    if (enc == EN_SELECT) {
        int v = edit_voice + det;
        if (v < 0) v = 0;
        if (v > 3) v = 3;
        edit_voice = (uint8_t)v;
    } else if (edit_voice < st->nvoices && st->v[edit_voice].note != 0xFF) {
        if (enc == EN_K1) {
            int v = st->v[edit_voice].note + det * 12;
            if (v < 0) v = 0;
            if (v > 127) v = 127;
            st->v[edit_voice].note = (uint8_t)v;
        } else if (enc == EN_K2) {
            int v = st->v[edit_voice].gate + det;
            if (v < 1) v = 1;
            if (v > 32) v = 32;
            st->v[edit_voice].gate = (uint8_t)v;
        } else if (enc == EN_K3) {
            int v = st->v[edit_voice].vel + det * 2;
            if (v < 1) v = 1;
            if (v > 127) v = 127;
            st->v[edit_voice].vel = (uint8_t)v;
        } else if (enc == EN_K4) {
            int v = st->prob + det * 5;
            if (v < 0) v = 0;
            if (v > 100) v = 100;
            st->prob = (uint8_t)v;
        }
    }
}

static void ui_arp_encoder(uint8_t enc, int8_t det)
{
    ds_track *tr = TRK();
    if (enc == EN_K2 && glo_held) {              /* GLO+K2 = zoom (strip is the hero axis) */
        zoom_encoder(det);
        return;
    }
    if (enc == EN_K4) {                          /* K4 LINK */
        if (det > 0) {
            uint8_t k = tr->eu_k ? tr->eu_k : 4, n = tr->eu_n ? tr->eu_n : 16;
            tr->eu_on = 1;
            tr->eu_k = k; tr->eu_n = n;
            ds_euclid_fill(tr);
            if (tr->channel == 9) {              /* seed = selected drum (03 §4.2) */
                int stp;
                for (stp = 0; stp < 64; stp++)
                    if (tr->steps[stp].on && tr->steps[stp].nvoices == 0)
                        ds_voice_add(tr, stp, GM_DRUMS[drum_sel], 100, 1);
            }
        } else if (tr->eu_on) {
            tr->eu_on = 0;                       /* FREE: freeze, meta kept */
        }
        return;
    }
    if (!tr->eu_n) tr->eu_n = 16;
    if (enc == EN_K1) {
        int v = tr->eu_k + det;
        if (v < 1) v = 1;
        if (v > tr->eu_n) v = tr->eu_n;
        tr->eu_k = (uint8_t)v;
    } else if (enc == EN_K2) {
        int v = tr->eu_n + det;
        if (v < 2) v = 2;
        if (v > 64) v = 64;
        tr->eu_n = (uint8_t)v;
        if (tr->eu_k > tr->eu_n) tr->eu_k = tr->eu_n;
    } else if (enc == EN_K3) {
        int v = tr->eu_rot + det;
        if (v < 0) v = 0;
        if (v > tr->eu_n - 1) v = tr->eu_n - 1;
        tr->eu_rot = (uint8_t)v;
    }
    if (tr->eu_on)
        ds_euclid_fill(tr);                      /* link re-fills; FREE = meta only (lesson 60) */
}

/* ---- routing: encoders + buttons ----
 * Encoder ids here are the LOGICAL roles (plat_ctrl.h EN_*): EN_SELECT=0,
 * EN_ALGO=1, EN_PRESET=2, EN_K1..K4=3..6. */
static void ui_encoder(uint8_t enc, int8_t det)
{
    /* r16j panelEnc: SELECT is context-aware, not global —
     * ARP page + non-EDIT = track; EDIT layer (melodic) = active voice.
     * (drum EDIT resolves its voice by drum_sel, never via SELECT) */
    if (enc == EN_SELECT && !(ui_mode == M_GRID && focus_layer && !is_drum_trk())) {
        int next = sel_track + (det > 0 ? 1 : -1);
        if (next < 0) next = 0;
        if (next > 15) next = 15;
        sel_track = (uint8_t)next;
        return;
    }
    if (enc == EN_ALGO || enc == EN_PRESET)
        return;                                  /* BPM=ALGO: batch 3 (BPM→GLO 页待拍板) */
    if (ui_mode == M_ARP) { ui_arp_encoder(enc, det); return; }
    if (ui_mode == M_GRID) {
        if (focus_layer) ui_edit_encoder(enc, det);
        else ui_grid_encoder(enc, det);
        return;
    }
    ui_home_encoder(enc, det);
}

static void ui_button(uint8_t btn, uint8_t down, uint8_t released)
{
    (void)down;
    if (!released)
        return;                                   /* everything fires on the release edge */
    switch (btn) {
    case B_HOME:                                  /* emu: HOME toggles HOME <-> GRID */
        ui_mode = (ui_mode == M_HOME) ? M_GRID : M_HOME;
        focus_layer = 0;
        break;
    case B_SEQ:  ui_mode = M_GRID; break;
    case B_ARP:  ui_mode = M_ARP;  break;
    case B_EDIT: if (ui_mode == M_GRID) focus_layer ^= 1; break;
    case B_PLAY:
        if (g.playing) ds_stop(&g);
        else ds_play(&g);
        break;
    case B_REC:                                    /* v1: REC = cursor step toggle (emu) */
        {   uint8_t on = TRK()->steps[cursor].on;
            TRK()->steps[cursor].on = on ? 0 : 1;
        }
        break;
    case B_OCTDN:
        if (ui_mode == M_GRID && !glo_held && !focus_layer) {
            key_shift -= 12; if (key_shift < -24) key_shift = -24;
        } else if (ui_mode == M_GRID && (glo_held || focus_layer)) {
            if (cursor > 0) cursor--;              /* GLO+OCT- / EDIT = cursor back (emu) */
        } else {
            key_shift -= 12; if (key_shift < -24) key_shift = -24;
        }
        break;
    case B_OCTUP:
        if (ui_mode == M_GRID && glo_held) {
            /* GLO+OCT+ = rest/tie cycle — v1.1; no-op now */
        } else if (ui_mode == M_GRID && focus_layer) {
            if (cursor < 63) cursor++;             /* EDIT layer: cursor fwd (emu) */
        } else {
            key_shift += 12; if (key_shift > 24) key_shift = 24;
        }
        break;
    default: break;                                /* SAVE/FX/ENV/LFO: v1 unsignaled */
    }
}

static void ui_init(void)
{
    ui_mode = M_HOME;
    focus_layer = 0;
    cursor = 0;
    edit_voice = 0;
    drum_sel = 0;
    key_shift = 0;
    glo_held = 0;
    sel_track = 0;
}
