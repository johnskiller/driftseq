/* ui_edit.c — EDIT panel (rev.12 embedded, melodic) + drum readout strip +
 * ARP page + encoder/button routing (part of ui.c unity section). */

/* ---- melodic EDIT panel: 4 voice cards, whole-row param bars (rev.14c) ---- */
static void ui_draw_edit_panel(void)
{
    ds_track *tr = TRK();
    ds_step *st = &tr->steps[cursor];
    int r;
    /* 4 rows x 26px at y 112..216 (rev.12 budget: 16+96+104+24) */
    cv_begin(240, 104, RGB565(16, 16, 21));
    for (r = 0; r < 4; r++) {
        int y = r * 26;
        uint8_t active = (focus_layer && r == edit_voice);
        cv_rect(0, y, 240, 25, active ? RGB565(42, 42, 51) : RGB565(22, 22, 27));
        if (r < st->nvoices && st->v[r].note != 0xFF) {
            char *line;
            char nb[8];
            uint16_t col = TRACK_HUE(st->v[r].note & 15);
            int gspan = (st->v[r].gate == 0xFF) ? 240 : (st->v[r].gate * 240 / 32);
            cv_rect(0, y, gspan, 25, col);
            /* "v1 C3 g4 v100" */
            line = txt_next();
            line[0] = 'v'; line[1] = (char)('1' + r); line[2] = ' ';
            strcpy_(nb, note_name(st->v[r].note));
            strcpy_(line + 3, nb);
            {   /* append " g<v> v<v>" via fmt_int */
                char *w = line;
                while (*w) w++;
                *w++ = ' '; *w++ = 'g';
                fmt_int(w, st->v[r].gate); while (*w) w++;
                *w++ = ' '; *w++ = 'v';
                fmt_int(w, st->v[r].vel);
            }
            cv_text(6, y + 5, &FONT_XS, line, C_BLACK);
            if (active)
                cv_rect(0, y, 239, 1, RGB565(230, 200, 74));
        } else {
            cv_text(6, y + 5, &FONT_XS, "--", C_DIM);
        }
    }
    cv_blit(0, 112);
}

/* ---- drum readout strip (r16e: 56px, text is the data) ---- */
static void ui_draw_readout(void)
{
    ds_step *st = &TRK()->steps[cursor];
    int hit = -1, vi;
    for (vi = 0; vi < st->nvoices; vi++)
        if (st->v[vi].note == GM_DRUMS[drum_sel]) hit = vi;
    cv_begin(240, 56, RGB565(16, 16, 21));
    cv_rect(0, 0, 240, 1, RGB565(58, 58, 68));
    cv_text(10, 10, &FONT_XS, txt_join(GM_SHORT[drum_sel], txt_st(cursor + 1)), D_PAL[drum_sel]);
    if (hit >= 0)
        cv_text(74, 10, &FONT_XS,
                txt_vgp(st->v[hit].vel,
                        st->v[hit].gate == 0xFF ? 0 : st->v[hit].gate,
                        st->prob), RGB565(255, 244, 214));
    else
        cv_text(74, 10, &FONT_XS, "-- toggle: white key", RGB565(255, 244, 214));
    cv_text(10, 34, &FONT_XS, "edit: VEL GAT PROB", RGB565(138, 138, 153));
    cv_blit(0, 160);
}

/* ---- ARP ring: n ticks clockwise from 12 o'clock, k pulses in track color ----
 * Integer polar via a 64-entry sine table (1796 ≈ 2π scale; cos = sin+16 quads). */
static const int16_t RING_SIN[64] = {0,175,349,519,684,843,995,1138,1271,1393,1503,1601,1686,1757,1815,1858,1886,1899,1897,1880,1847,1800,1739,1664,1576,1476,1364,1242,1111,972,826,673,516,355,192,27,-138,-305,-470,-632,-789,-941,-1086,-1223,-1352,-1471,-1580,-1677,-1762,-1833,-1889,-1929,-1954,-1962,-1954,-1930,-1889,-1833,-1762,-1677,-1580,-1471,-1352,-1223};
static void ui_ring_polar(int a, int r, int cx, int cy, int *x, int *y)
{
    int sn = RING_SIN[a & 63], cs = RING_SIN[(a + 16) & 63];
    *x = cx + sn * r / 1962;
    *y = cy - cs * r / 1962;
}
static void draw_arp(void)
{
    ds_track *tr = TRK();
    int i;
    cv_begin(240, 224, C_BLACK);
    cv_text(6, 2, &FONT_T, tr->eu_on ? "*LINK" : "FREE",
            tr->eu_on ? RGB565(95, 224, 138) : RGB565(90, 90, 98));
    /* result strip y16..28: what the loop plays (hero grammar) */
    for (i = 0; i < 64; i++)
        if (tr->steps[i].on)
            cv_rect(30 + i * 3, 18, 2, 8, TRACK_HUE(sel_track % 11));
    cv_rect(28 + (int)g.pos16[sel_track] * 3, 16, 1, 12, C_WHITE);
    /* ring zone y28..196: one canvas pass, ring + numbers drawn into it */
    ui_draw_ring_onto_main();
    cv_text(4, 202, &FONT_XS, txt_knr(tr->eu_k, tr->eu_n, tr->eu_rot), C_AMB);
    cv_blit(0, 0);
    ui_draw_footer();
}

/* ring drawn directly at final coordinates (single-canvas pass) */
static void ui_draw_ring_onto_main(void)
{
    ds_track *tr = TRK();
    int i;
    int cx = 119, cy = 112, r0 = 60, r1 = 68;
    for (i = 0; i < tr->eu_n; i++) {
        int x0, y0, x1, y1;
        ui_ring_polar(i * 64 / tr->eu_n, r0, cx, cy, &x0, &y0);
        ui_ring_polar(i * 64 / tr->eu_n, r1, cx, cy, &x1, &y1);
        cv_line(x0, y0, x1, y1, i < tr->eu_k ? TRACK_HUE(sel_track % 11) : C_DIM);
    }
    {   /* playhead tick, white, just outside the ring */
        int x0, y0, x1, y1;
        ui_ring_polar((int)g.pos16[sel_track] * 64 / tr->eu_n, r0, cx, cy, &x0, &y0);
        ui_ring_polar((int)g.pos16[sel_track] * 64 / tr->eu_n, r1 + 3, cx, cy, &x1, &y1);
        cv_line(x0, y0, x1, y1, C_WHITE);
    }
}

/* ---- per-page encoder behavior ---- */
static void ui_home_encoder(uint8_t enc, int8_t det)
{
    ds_track *tr = TRK();
    if (enc == EN_K1) {                          /* K1 LOOP START (dead-stop) */
        int v = tr->loop_start + det;
        if (v < 0) v = 0;
        if (v > 63) v = 63;
        tr->loop_start = (uint8_t)v;
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
    if (enc == EN_K1) cursor_move(det);          /* K1 cursor (GLO+K1 x16) */
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
 * EN_ALGO=1, EN_PRESET=2, EN_K1..K4=3..6. The per-page handlers use
 * K1..K4 = 3..6 (emu panelEnc role numbers), so select-track is matched
 * explicitly and everything else passes through. */
static void ui_encoder(uint8_t enc, int8_t det)
{
    if (enc == EN_SELECT) {                      /* SELECT: track, no wrap */
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
            /* GLO+OCT+ = rest/tie cycle — v1.1 (needs cond/tie semantics); no-op now */
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
