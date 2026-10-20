/* tools/ui_probe.js — pixel-level assertions on the REAL painters.
 *
 * The hostcheck (tools/fw_check.py) compiles the app layer against cv_rect
 * stubs, so it proves routing but cannot see a single pixel. Everything that
 * is actually a drawing contract — bar spacing per zoom, the strip following
 * the hero's axis, which page owns the zoom wheel — has to be measured on the
 * wasm build instead. Those probes used to live in /tmp and were lost every
 * session; this file is the durable version.
 *
 *   make uiprobe        (builds web/ui.js first, then runs this)
 *   node tools/ui_probe.js
 *
 * Reads pixel colours out of the 240x240 RGB565 fb. The colour constants
 * below mirror ui_screens.c; if a painter's colours change, update them here
 * deliberately — a silent mismatch would make an assertion vacuous.
 */
'use strict';
const path = require('path');
const WEB = path.join(__dirname, '..', 'web');
const createMod = require(path.join(WEB, 'ui.js'));

/* panel logical ids (firmware/src/plat_ctrl.h B_* / EN_*) */
const B_FX = 0, B_SEL = 1, B_ENV = 2, B_LFO = 3, B_EDIT = 4, B_GLO = 5,
      B_HOME = 6, B_SAVE = 7, B_ARP = 8, B_SEQ = 9, B_PLAY = 10, B_REC = 11,
      B_OCTDN = 12, B_OCTUP = 13;
const EN_SELECT = 0, EN_ALGO = 1, EN_PRESET = 2, EN_K1 = 3, EN_K2 = 4,
      EN_K3 = 5, EN_K4 = 6;

/* painter colours (ui_screens.c) — RGB565-quantised as px() reports them.
 * rev.24 dimmed the meter below the loop band: the bar grid is now CONTEXT
 * (where am I in the bar), not the ruler loops are measured against — a
 * 12-step loop is 0.75 bar and never lands on a bar line at all. */
const HX0 = 47, HX1 = 239;          /* hero bar area (ui_screens.c) */
const BAR_LINE = '40,40,48';        /* meter: bar line   RGB565(42,42,52) */
const BEAT_LINE = '32,32,40';       /* meter: beat line  RGB565(34,34,42) */

let fails = 0;
function check(cond, msg, detail) {
    if (cond) console.log('ok: ' + msg);
    else { console.log('FAIL: ' + msg + (detail ? '  [' + detail + ']' : '')); fails++; }
}

createMod().then((M) => {
    const fb0 = () => M.HEAPU8.slice(M._ds_web_fb(), M._ds_web_fb() + 240 * 240 * 2);
    const px = (fb, x, y) => {
        const v = fb[(y * 240 + x) * 2] | (fb[(y * 240 + x) * 2 + 1] << 8);
        return ((v >> 11) & 31) * 8 + ',' + (((v >> 5) & 63) * 4) + ',' + ((v & 31) * 8);
    };
    const frame = () => { M._ds_ui_frame(0.016); return fb0(); };
    const hit = (c) => (c === BAR_LINE ? 'bar' : c === BEAT_LINE ? 'beat' : '');
    /* x positions of meter lines at row y inside the bar area */
    const meter = (fb, y) => {
        const bar = [], beat = [];
        for (let x = 47; x < 239; x++) {
            const h = hit(px(fb, x, y));
            if (h === 'bar') bar.push(x); else if (h === 'beat') beat.push(x);
        }
        return { bar, beat, barSp: bar.length > 1 ? bar[1] - bar[0] : 0,
                 beatSp: beat.length > 1 ? beat[1] - beat[0] : 0 };
    };
    const btn = (b) => { M._ds_web_btn(b, 1); M._ds_web_btn(b, 0); };
    /* HOME is a TOGGLE (ui_button: HOME <-> GRID), so "press HOME" is not
     * "go to HOME". Always land via GRID, which is idempotent. */
    const toHome = () => { btn(B_SEQ); btn(B_HOME); };
    const toGrid = () => { btn(B_SEQ); };
    /* GLO+K2 is a dead-stop 4-detent ladder: go to x1 then step up. */
    const setZoom = (z) => {
        M._ds_web_btn(B_GLO, 1);
        for (let i = 0; i < 3; i++) M._ds_web_enc(EN_K2, -1);
        for (let i = 0; i < Math.log2(z); i++) M._ds_web_enc(EN_K2, 1);
        M._ds_web_btn(B_GLO, 0);
        M._ds_ui_frame(0.016);
    };

    M._ds_ui_boot();
    M._ds_ui_frame(0.016);

    /* ---- 0. the boot zoom is x4 (John 2026-10-19) ----
     * Must run before any setZoom(). x4 is the default because it is the only
     * level where one bar fills the 192px area at 1/16 AND the 4-bar phrase
     * John actually plays still fits — x8 hides the phrase, x1 wastes 3/4 of
     * the screen on one bar. Pinning it here so a stray initialiser change
     * cannot silently re-scale every screen. */
    {
        const m = meter(frame(), 138);           /* same row as §1 (see below) */
        check(m.barSp === 48 && m.beatSp === 12,
              'boot zoom is x4 (bar/beat = 48/12px)', 'bar=' + m.barSp + ' beat=' + m.beatSp);
    }

    /* ---- 1. HOME hero meter spacing across the four zoom levels ----
     * bar area is always 192px; zoom changes ticks per pixel, so the bar
     * spacing must be 192/96/48/24 — measured, not assumed. At x1 one bar
     * exactly fills the area, so there is a single bar line at the left. */
    const HOME_ROW = 138;                       /* middle of track row 10 */
    const WANT = { 1: [0, 48], 2: [96, 24], 4: [48, 12], 8: [24, 0] };
    for (const z of [1, 2, 4, 8]) {
        toHome();
        setZoom(z);
        const m = meter(frame(), HOME_ROW);
        check(m.barSp === WANT[z][0] && m.beatSp === WANT[z][1],
              'HOME x' + z + ' meter spacing ' + WANT[z][0] + '/' + WANT[z][1] + 'px',
              'bar=' + m.barSp + ' beat=' + m.beatSp + ' beats=' + m.beat.length);
    }

    /* ---- 2. the zoom wheel works ON THE HOME PAGE (rev.21) ----
     * rev.20 put the handler in ui_grid_encoder only — the one page that does
     * NOT draw the hero — so the screen carrying the `x4` readout could not
     * move it. Assert the wheel is effective where the hero is visible. */
    toHome();
    setZoom(1);
    const before = meter(frame(), HOME_ROW).barSp;
    M._ds_web_btn(B_GLO, 1); M._ds_web_enc(EN_K2, 1); M._ds_web_btn(B_GLO, 0);
    const after = meter(frame(), HOME_ROW).barSp;
    check(before === 0 && after === 96, 'HOME: GLO+K2 steps x1 -> x2 without leaving HOME',
          'before=' + before + ' after=' + after);

    /* ---- 3. plain K2 on HOME is still LOOP LEN, not zoom ----
     * The modifier must not leak: K2 without GLO keeps its documented job. */
    toHome();
    setZoom(4);
    const barsBefore = meter(frame(), HOME_ROW).bar;
    M._ds_web_enc(EN_K2, 1);                    /* bare K2 = LEN +1 */
    const barsAfter = meter(frame(), HOME_ROW).bar;
    check(JSON.stringify(barsBefore) === JSON.stringify(barsAfter),
          'HOME: bare K2 does not change the time axis', 'zoom leaked through K2');

    /* ---- 4. ARP result strip shares the hero axis (rev.21) ----
     * The strip claims to be "the same absolute time axis as the hero"; at x1
     * a 1/16 cell is 12px, at x4 it is 3px. Measure the strip's on-blocks:
     * the same loop must show the same NUMBER of blocks at every zoom (one
     * loop is one loop), only their widths change.
     *
     * rev.23 removed the tiling from both the hero and this strip, so the old
     * "x4 shows more steps than x1" assertion is exactly the bug we deleted —
     * it passed only because the loop was being repeated to fill the window. */
    const STRIP_ROW = 20;
    /* Non-note colours on the strip: outer bg, in-loop bed, out-of-loop bed,
     * beat line, bar line. Anything else on this row is a note block. Listing
     * them explicitly beats "not the strip bg": rev.23 introduced a SECOND bed
     * colour for the out-of-loop region, and a single-value test counted the
     * whole dark tail as one enormous note. */
    const STRIP_BED = new Set(['40,40,48', '42,42,51', '32,32,40', '48,48,56', '64,68,80']);
    const stripRuns = (fb) => {
        const r = []; let s = -1;
        for (let x = 47; x < 239; x++) {
            const on = !STRIP_BED.has(px(fb, x, STRIP_ROW));
            if (on && s < 0) s = x;
            if (!on && s >= 0) { r.push(x - s); s = -1; }
        }
        return r;
    };
    setZoom(1);
    btn(B_ARP);
    const w1 = stripRuns(frame());
    setZoom(4);
    const w4 = stripRuns(frame());
    check(w1.length > 0 && w4.length > 0,
          'ARP strip draws on-blocks at both zooms',
          'x1=' + w1.length + ' x4=' + w4.length);
    check(w1.length === w4.length,
          'ARP strip shows ONE loop at every zoom (rev.23: no tiling)',
          'x1 blocks=' + w1.length + ' x4 blocks=' + w4.length);
    check(w4.length > 0 && w4[0] <= w1[0],
          'ARP strip blocks shrink when zoomed out',
          'x1 first width=' + w1[0] + ' x4 first width=' + w4[0]);

    /* ---- 4b. the hero shows ONE loop, not a tiled wallpaper (rev.23) ----
     * John: "不需要铺满hero —— hero 显示 4bar、trk len 8step@1/16 只占 2bar，
     * hero 就应该只显示 2bar，后面暗色表示 loop 外". Two measurable facts:
     * (a) the bright band ends at the loop's own length, not at the window
     *     edge, and rows with different loop lengths end at DIFFERENT x;
     * (b) past that edge the bed is the out-of-loop colour, and notes stop
     *     there too (a tiled row would still be drawing notes to 239). */
    toHome();
    setZoom(4);
    /* px() reports RGB565-quantised values: RGB565(36,36,45) reads back as
     * 32,36,40 and RGB565(29,29,38) as 24,28,32. Meter lines drawn over the
     * bed must be excluded or every beat would count as "band". */
    const OUT = '32,36,40', BG = '24,28,32';
    const METER = new Set(['40,44,48', '64,64,72']);
    const rowBed = (fb, t) => {
        const y = 24 + t * 12 + 5;
        let band = 0, out = 0;
        for (let x = 47; x < 239; x++) {
            const c = px(fb, x, y);
            if (c === OUT) out++;
            else if (c !== BG && !METER.has(c)) band++;
        }
        return { band, out };
    };
    {
        const f = frame();
        const perRow = [];
        for (let t = 0; t < 16; t++) perRow.push(rowBed(f, t));
        const withOut = perRow.filter((r) => r.out > 0).length;
        check(withOut === 16,
              'HERO: every row marks its out-of-loop tail (rev.23)', 'rows=' + withOut + '/16');
        const lens = new Set(perRow.map((r) => r.band));
        check(lens.size > 1,
              'HERO: band width follows each track own loop length',
              'distinct widths=' + lens.size + ' -> ' + [...lens].join(' '));
        const widest = Math.max(...perRow.map((r) => r.band + r.out));
        check(widest <= 193, 'HERO: no row is tiled past the window',
              'widest row=' + widest + 'px of 192');
    }

    /* ---- 4b2. the loop edge is the row's own ruler (rev.24) ----
     * John: "如果是12step的trk呢？所以不强调对齐bar / 甚至是13step". A 12-step
     * 1/16 loop is 0.75 bar; 13 steps is 0.8125 bar. Neither lands on a bar
     * line, so the bar grid must not be the brightest thing on the row — the
     * loop edge must be. Assert both halves: the edge exists at the exact
     * length, and it is BRIGHTER than any bar line on that row. */
    {
        const setLen = (len) => {
            for (let i = 0; i < 70; i++) M._ds_web_enc(EN_K2, -1);   /* clamp to 1 */
            for (let i = 0; i < len - 1; i++) M._ds_web_enc(EN_K2, 1);
            return frame();
        };
        /* the track hue at full saturation vs the dim meter */
        const lum = (c) => { const p = c.split(',').map(Number); return p[0] + p[1] + p[2]; };
        for (const len of [12, 13]) {
            toHome();
            setZoom(4);
            const f = setLen(len);
            const y = 24 + 5;                       /* trk1, inside a row */
            /* rev.26: the axis is ABSOLUTE slots, so the window edge is at
             * loop_start + len, not at len. trk1 starts at 0 here, but writing
             * the formula with the start keeps it honest if that changes. */
            const wantX = 47 + Math.floor((0 + len) * 6 * 192 / 384);
            /* find the brightest pixel in the row = the loop edge */
            let bestX = -1, bestL = -1;
            for (let x = 47; x < 239; x++) {
                const L = lum(px(f, x, y));
                if (L > bestL) { bestL = L; bestX = x; }
            }
            check(Math.abs(bestX - wantX) <= 2,
                  'HERO: ' + len + '-step loop edge lands at its real length',
                  'edge at ' + bestX + ', expected ~' + wantX);
            /* a bar line must be dimmer than that edge */
            const barAt = lum(px(f, 47 + 48, y));    /* first bar line at x4 */
            check(bestL > barAt,
                  'HERO: ' + len + '-step loop edge outranks the bar line',
                  'edge lum=' + bestL + ' vs bar lum=' + barAt);
        }
        toHome();
        setLen(16);
    }

    /* ---- 4b3. hero draws ABSOLUTE slots, exactly like the GRID (rev.26) ----
     * John: "都是hero画面有问题，grid画面正常" — the hero was drawing play-order
     * positions while the GRID draws absolute step indices, so demo trk2
     * (loop_start=4) disagreed by exactly 4 cells: its SNR hits at absolute
     * 4/7/9/12/14 appeared as a 4-cell gap then notes. This pins hero note
     * positions to the pattern data, cell for cell. */
    {
        const EXPECT = [
            null,
            [0, 4, 8, 12],              /* trk1 kick  E(16,4) start 0 */
            [4, 7, 9, 12, 14],          /* trk2 snare E(12,5) start 4 */
            [0, 3, 6],                  /* trk3 hats  E(8,3)  start 0 */
        ];
        toHome();
        setZoom(4);
        const f = frame();
        const lumAt = (x, y) => {
            const p = px(f, x, y).split(',').map(Number);
            return p[0] + p[1] + p[2];
        };
        for (let t = 1; t <= 3; t++) {
            const yTop = 24 + (t - 1) * 12;
            const lit = [];
            /* HX1 is the selected row's green outline, not a note -> exclude */
            for (let x = HX0; x < HX1; x++) {
                let best = 0;
                for (let dy = 0; dy < 11; dy++) best = Math.max(best, lumAt(x, yTop + dy));
                lit.push(best > 200);
            }
            const runs = []; let s = -1;
            lit.forEach((v, i) => {
                if (v) { if (s < 0) s = i; } else { if (s >= 0) { runs.push([s, i - 1]); s = -1; } }
            });
            if (s >= 0) runs.push([s, lit.length - 1]);
            /* drop 1px meter/edge ticks: real note blocks are >=2px at x4 */
            const steps = runs.filter(([a, b]) => b > a || a === 0)
                              .map(([a]) => Math.round(a * 2 / 6));
            const want = EXPECT[t];
            check(JSON.stringify(steps) === JSON.stringify(want),
                  'HERO: trk' + t + ' notes sit on ABSOLUTE slots (match GRID)',
                  'hero=' + JSON.stringify(steps) + ' abs=' + JSON.stringify(want));
        }
    }

    /* ---- 4b4. melodic rows encode PITCH vertically (rev.27) ----
     * John: "melody trk 还没有修复". Positions and gates were already right;
     * the row was not. The old code stacked voices by their index in the
     * sorted chord, so every single-note step landed on the SAME scanline:
     * demo trk5 spans C5/E5/G5/C6 — three octaves — and all four drew at
     * dy=1. The row now maps pmin..pmax across the 11px band, same
     * normalisation as the GRID, so low pitch = lower line. */
    {
        toHome();
        setZoom(4);
        const f = frame();
        /* a voice line is a SATURATED hue12 colour; the in-loop band is a 15%
         * hue tint (~[32,68,56]) and the bed is dark. Require a clear chroma
         * spread so the band itself is never mistaken for a note. */
        const band = (x0, len, yTop) => {
            for (let dy = 1; dy <= 10; dy++) {
                for (let x = x0; x < x0 + len; x++) {
                    const p = px(f, x, yTop + dy).split(',').map(Number);
                    const mx = Math.max(p[0], p[1], p[2]), mn = Math.min(p[0], p[1], p[2]);
                    if (mx > 110 && mx - mn > 60) return dy;
                }
            }
            return -1;
        };
        /* trk5 (row index 4): C5@0, E5@3, G5@6, C6@11 (C6 prob=55, dimmer) */
        const yTop5 = 24 + 4 * 12;
        const dy0 = band(HX0, 2, yTop5);          /* C5, lowest */
        const dy3 = band(HX0 + 9, 2, yTop5);      /* E5  x=47+tx(18)=56 */
        const dy6 = band(HX0 + 18, 2, yTop5);     /* G5  x=47+tx(36)=65 */
        check(dy0 > dy3 && dy3 > dy6,
              'HERO: melodic pitch rises upward (C5 < E5 < G5)',
              'dy C5=' + dy0 + ' E5=' + dy3 + ' G5=' + dy6);
        /* trk4 (row index 3): C2@0, D#2@6, G2@10 */
        const yTop4 = 24 + 3 * 12;
        const b0 = band(HX0, 2, yTop4);
        const b6 = band(HX0 + 18, 2, yTop4);
        const b10 = band(HX0 + 30, 2, yTop4);
        check(b0 > b6 && b6 > b10,
              'HERO: melodic pitch rises upward (C2 < D#2 < G2)',
              'dy C2=' + b0 + ' D#2=' + b6 + ' G2=' + b10);
    }

    /* ---- 4c. every track draws its OWN playhead (rev.23) ----
     * John: "这样每个trk都有自己的playhead，可以表现出drift". With loops of
     * different lengths running off one clock, the heads must NOT all sit in
     * one column — that is the whole point. Assert at least three distinct
     * head positions while playing. */
    btn(B_PLAY);
    {
        /* The demo's 16 tracks cluster into a few (len, division) groups, so a
         * single frame can legitimately show only two positions — two groups
         * that happen to coincide at that instant. Sample a sweep instead: the
         * contract is "the heads do not all move as one", i.e. at SOME moment
         * three or more distinct positions exist. */
        let live = 0, best = 0, bestAt = 0;
        for (let i = 0; i < 10; i++) {
            M._ds_ui_frame(0.2);
            const f = frame();
            const heads = [];
            for (let t = 0; t < 16; t++) {
                const y = 24 + t * 12 + 5;
                let hx = -1;
                for (let x = 47; x < 239; x++) {
                    const c = px(f, x, y);
                    if (c === '248,252,248') { hx = x; break; }   /* C_WHITE, quantised */
                }
                heads.push(hx);
            }
            live = Math.max(live, heads.filter((h) => h >= 0).length);
            const d = new Set(heads.filter((h) => h >= 0)).size;
            if (d > best) { best = d; bestAt = 0.2 * (i + 1); }
        }
        check(live === 16, 'HERO: all 16 rows draw a playhead', 'heads=' + live);
        check(best >= 3,
              'HERO: playheads sit at different x per track (drift is visible)',
              'max ' + best + ' distinct positions at +' + bestAt.toFixed(1) + 's');
    }
    btn(B_PLAY);

    /* ---- 5. the zoom readout says N BAR, and N tracks the zoom ----
     * John 2026-10-20: "顶部直接写1 bar 4bar这样，明确表示目前hero显示出几个bar".
     * The old detent label ("x4b") was read as "X2B" in the field, i.e. the
     * unit was invisible. Two contracts now: (a) all four zooms render a
     * DIFFERENT glyph signature (a stale variable would be byte-equal), and
     * (b) the readout stays inside its column and cannot collide with the
     * loop-start "S" readout at x=204 — the unit word made the string longer
     * than the old two-character label, so its width is now a real risk. */
    const READOUT = (fb) => {
        let sig = '';
        for (let y = 2; y < 14; y++)
            for (let x = 160; x < 200; x++) {
                const c = px(fb, x, y).split(',').map(Number);
                sig += (c[0] > 40 || c[1] > 40 || c[2] > 40) ? '#' : '.';
            }
        return sig;
    };
    const readoutInk = (fb) => {          /* rightmost inked column of the readout */
        let last = -1;
        for (let y = 3; y <= 12; y++)
            for (let x = 160; x < 200; x++) {
                const c = px(fb, x, y).split(',').map(Number);
                if (c[0] > 40 || c[1] > 40 || c[2] > 40) last = Math.max(last, x);
            }
        return last;
    };
    toHome();
    const sigs = {};
    for (const z of [1, 2, 4, 8]) {
        setZoom(z);
        const fb = frame();
        sigs[z] = READOUT(fb);
        check(sigs[z].includes('#'), 'HOME: zoom readout renders at x' + z);
        check(readoutInk(fb) < 204,
              'HOME: "' + z + ' BAR" clears the S readout at x=204',
              'rightmost ink x=' + readoutInk(fb));
    }
    check(new Set([sigs[1], sigs[2], sigs[4], sigs[8]]).size === 4,
          'HOME: all four zoom readouts differ (1/2/4/8 BAR)');

    /* the ARP strip calls itself the same time axis as the hero, so it must
     * not describe that axis in a different notation either (rev.25). */
    {
        const band = (fb, x0) => {
            let s = '';
            for (let y = 4; y < 13; y++)
                for (let x = x0; x < x0 + 34; x++) {
                    const c = px(fb, x, y).split(',').map(Number);
                    s += (c[0] > 40 || c[1] > 40 || c[2] > 40) ? '#' : '.';
                }
            return s;
        };
        setZoom(4);
        const homeBand = band(frame(), 168);      /* "4 BAR" on HOME */
        btn(B_ARP);
        const arpBand = band(frame(), 112);       /* same spot on ARP */
        check(arpBand.includes('#'), 'ARP: zoom readout renders');
        check(arpBand === homeBand,
              'ARP: zoom readout uses the same "N BAR" string as HOME');
        btn(B_SEQ);
    }

    /* ---- 6. DIVISION lives on the GRID page (rev.22) ----
     * John: "DIV现在没有设定的方法，在grid screen设置比较好". Two contracts:
     * (a) GLO+K3 on the GRID changes the track division; (b) the change is
     * visible on the page that owns it — a blind twist with no feedback is the
     * same as no control at all. HOME only displays the value. */
    const DIV_X0 = 96, DIV_X1 = 120;          /* the DIV column, both grid pages */
    const GRID_DIV = (fb) => {
        let sig = '';
        for (let y = 2; y < 14; y++)
            for (let x = DIV_X0; x < DIV_X1; x++) sig += px(fb, x, y) === '24,24,32' ? '.' : '#';
        return sig;
    };
    toHome(); toGrid();
    const div0 = GRID_DIV(frame());
    M._ds_web_btn(B_GLO, 1);
    M._ds_web_enc(EN_K3, 1);                  /* 1/16 -> 1/16T */
    M._ds_web_btn(B_GLO, 0);
    const div1 = GRID_DIV(frame());
    check(div0.includes('#') && div0 !== div1,
          'GRID: GLO+K3 changes DIV and the readout repaints on the page itself');

    /* The same readout must sit in the SAME column on the melodic grid too:
     * one parameter, one place. A per-track-type column means the eye has to
     * re-find the value every time SELECT crosses the drum track. */
    const inkLeft = (fb) => {
        for (let x = 60; x < 150; x++)
            for (let y = 2; y < 14; y++)
                if (px(fb, x, y) !== '24,24,32') return x;
        return -1;
    };
    const drumLeft = inkLeft(frame());
    M._ds_web_enc(EN_SELECT, 1);              /* drum trk0 -> melodic trk1 */
    const melLeft = inkLeft(frame());
    check(drumLeft !== -1 && melLeft === drumLeft,
          'GRID: DIV readout keeps its column across drum/melodic tracks',
          'drum starts at ' + drumLeft + ', melodic at ' + melLeft);
    M._ds_web_enc(EN_SELECT, -1);             /* back to trk0 */

    /* ---- 7. the GLO layer names itself in the footer ----
     * While GLO is held the four big boxes stop being STEP/LEN/MUTE/PROB; a
     * footer that keeps printing the bare names is actively lying about what
     * the encoder will do. The layer row must differ from the resting row. */
    const FOOT = (fb) => {
        let sig = '';
        for (let y = 220; y < 234; y++)
            for (let x = 4; x < 200; x++) sig += px(fb, x, y) === '24,24,32' ? '.' : '#';
        return sig;
    };
    const footRest = FOOT(frame());
    M._ds_web_btn(B_GLO, 1);
    const footGlo = FOOT(frame());
    M._ds_web_btn(B_GLO, 0);
    check(footRest.includes('#') && footRest !== footGlo,
          'GRID: holding GLO swaps the footer to the modifier layer names');

    /* ---- 8. white-key LED grammar (03 §3, John 2026-10-20) ----
     * "drum grid中，选中的鼓，对应的鼓点没有点亮白键。理想情况下可以用dim表示
     * 1，5，9，13，亮起白键表示这个step有鼓点"
     *
     * Two separate claims, both worth pinning:
     *  (a) the ruler is BEAT-based, not "every 4th step" — it must land on 1/5/9/13
     *      at 1/16 but on 1,3,5,... at 1/8, because a beat is 24 ticks either way;
     *  (b) lit follows the SELECTED drum only (a white key shows one instrument).
     * The demo gives a known answer: trk1 snare is E(12,5) with loop_start=4 ->
     * absolute slots 5,8,10,13,15. Kick E(16,4) sits exactly on the beat, which
     * is the case where lit and dim coincide and a sloppy implementation still
     * looks right — so the snare, not the kick, is the real test. */
    {
        const bitsOf = (m) => {
            const a = [];
            for (let i = 0; i < 16; i++) if (m & (1 << i)) a.push(i + 1);
            return a.join(',');
        };
        const BLACKK = [1, 3, 5, 8, 10, 13, 15, 17, 20];   /* drum 0..8 */
        toHome();
        M._ds_web_btn(9, 1); M._ds_web_btn(9, 0); frame(); /* SEQ = GRID */
        M._ds_web_enc(0, 1); frame();                      /* SELECT -> trk2 snare */

        check(bitsOf(M._ds_web_dim_k()) === '1,5,9,13',
              'LED: the dim ruler marks beats 1/5/9/13 at DIV 1/16',
              'got ' + bitsOf(M._ds_web_dim_k()));

        M._ds_web_key(BLACKK[1], 1); M._ds_web_key(BLACKK[1], 0); frame();  /* SNR */
        check(bitsOf(M._ds_web_lit_k()) === '5,8,10,13,15',
              'LED: lit keys are the SELECTED drum hits (snare E(12,5) @start 4)',
              'got ' + bitsOf(M._ds_web_lit_k()));

        M._ds_web_key(BLACKK[2], 1); M._ds_web_key(BLACKK[2], 0); frame();  /* CHH */
        check(bitsOf(M._ds_web_lit_k()) === '',
              'LED: switching drum clears the previous drum hits',
              'got ' + bitsOf(M._ds_web_lit_k()));

        /* the ruler must follow the DIVISION, not a hardcoded stride of 4 */
        M._ds_web_btn(5, 1); M._ds_web_enc(5, -1); M._ds_web_btn(5, 0); frame(); /* GLO+K3 -> 1/8T */
        M._ds_web_btn(5, 1); M._ds_web_enc(5, -1); M._ds_web_btn(5, 0); frame(); /* -> 1/8 */
        check(bitsOf(M._ds_web_dim_k()) === '1,3,5,7,9,11,13,15',
              'LED: at DIV 1/8 the ruler is every 2 steps (a beat is still 24 ticks)',
              'got ' + bitsOf(M._ds_web_dim_k()));
        /* restore 1/16 so later sections see the demo's default */
        M._ds_web_btn(5, 1); M._ds_web_enc(5, 1); M._ds_web_btn(5, 0); frame();
        M._ds_web_btn(5, 1); M._ds_web_enc(5, 1); M._ds_web_btn(5, 0); frame();
        M._ds_web_enc(0, -1); frame();                     /* SELECT back to trk1 */
    }

    console.log(fails ? '== ' + fails + ' UI PROBE FAILURES ==' : 'UI PROBE OK');
    process.exit(fails ? 1 : 0);
});
