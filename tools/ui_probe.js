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
            const wantX = 47 + Math.floor(len * 6 * 192 / 384);   /* tx(len*ct) */
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

    /* ---- 5. the zoom readout actually redraws ----
     * Reading "x8" out of glyph pixels would need OCR, but the property that
     * matters is weaker and testable: the readout band must CHANGE when the
     * zoom changes. A readout wired to a stale variable would be byte-equal. */
    const READOUT = (fb) => {
        let sig = '';
        for (let y = 2; y < 14; y++)
            for (let x = 160; x < 192; x++) sig += px(fb, x, y) === '24,24,32' ? '.' : '#';
        return sig;
    };
    toHome();
    setZoom(4);
    const r4 = READOUT(frame());
    setZoom(8);
    const r8 = READOUT(frame());
    check(r4.includes('#') && r4 !== r8,
          'HOME: zoom readout repaints when the zoom changes');

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

    console.log(fails ? '== ' + fails + ' UI PROBE FAILURES ==' : 'UI PROBE OK');
    process.exit(fails ? 1 : 0);
});
