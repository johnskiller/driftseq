/* tools/ui_shot.js — dump a UI page's framebuffer to a PNG.
 *
 * The wasm build is the authoritative renderer (web/ui_web.c unity-includes the
 * same painters the firmware runs), so a PNG from here is what the device
 * shows, minus the panel hardware. Useful when a change is visual: a pixel
 * assertion proves the property, this proves the picture.
 *
 *   node tools/ui_shot.js arp out.png
 *   node tools/ui_shot.js home out.png
 */
'use strict';
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');
const WEB = path.join(__dirname, '..', 'web');
const createMod = require(path.join(WEB, 'ui.js'));

const PAGE = (process.argv[2] || 'arp').toLowerCase();
const OUT = process.argv[3] || '/tmp/ds_' + PAGE + '.png';
/* optional: node tools/ui_shot.js arp out.png <n>  -- euclid ring length.
 * n=64 is the case worth looking at: it is the only length where i*64/n
 * divides exactly, so the ring uses all 64 table directions. */
const RING_N = parseInt(process.argv[4] || '0', 10);

const B_SEQ = 9, B_ARP = 8, B_HOME = 6, B_EDIT = 4, B_GLO = 5;
const EN_K2 = 4, EN_K4 = 6;

/* RGB565 fb -> PNG (8-bit RGB). No deps: the whole point is that this runs
 * anywhere node runs, including a bare container with no canvas package. */
function png(w, h, rgb) {
    const raw = Buffer.alloc((w * 3 + 1) * h);
    for (let y = 0; y < h; y++) {
        raw[y * (w * 3 + 1)] = 0;                       /* filter: none */
        rgb.copy(raw, y * (w * 3 + 1) + 1, y * w * 3, (y + 1) * w * 3);
    }
    const chunk = (type, data) => {
        const len = Buffer.alloc(4); len.writeUInt32BE(data.length);
        const td = Buffer.concat([Buffer.from(type, 'ascii'), data]);
        const crc = Buffer.alloc(4); crc.writeUInt32BE(zlib.crc32(td) >>> 0);
        return Buffer.concat([len, td, crc]);
    };
    const ihdr = Buffer.alloc(13);
    ihdr.writeUInt32BE(w, 0); ihdr.writeUInt32BE(h, 4);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    return Buffer.concat([
        Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
        chunk('IHDR', ihdr),
        chunk('IDAT', zlib.deflateSync(raw, { level: 9 })),
        chunk('IEND', Buffer.alloc(0)),
    ]);
}

createMod().then((M) => {
    const btn = (b) => { M._ds_web_btn(b, 1); M._ds_web_btn(b, 0); };
    btn(B_SEQ);
    if (PAGE === 'arp') {
        btn(B_ARP);
        /* a fresh track shows the "K4 - generate" hint page instead of the
         * ring, so generate first (same trap the ring probe documents) */
        M._ds_web_enc(EN_K4, 1);
        /* K2 is the ring length, stepping 2..64. A track starts at 16 and
         * generate keeps the k/n meta, so to reach RING_N step up from 16. */
        if (RING_N >= 2 && RING_N <= 64) {
            const steps = RING_N >= 16 ? RING_N - 16 : -(16 - RING_N);
            for (let i = 0; i < Math.abs(steps); i++)
                M._ds_web_enc(EN_K2, steps > 0 ? 1 : -1);
        }
    } else if (PAGE === 'edit') {
        btn(B_EDIT);
    } else if (PAGE === 'home') {
        btn(B_HOME);
    } else if (PAGE === 'grid') {
        /* SEQ alone lands on GRID */
    } else {
        console.error('page must be one of: arp edit home grid');
        process.exit(2);
    }
    M._ds_ui_frame(0.016);

    const W = 240, H = 224;                      /* visible area; fb is 240x240 */
    const base = M._ds_web_fb();
    const rgb = Buffer.alloc(W * H * 3);
    for (let y = 0; y < H; y++)
        for (let x = 0; x < W; x++) {
            const v = M.HEAPU8[base + (y * 240 + x) * 2] |
                      (M.HEAPU8[base + (y * 240 + x) * 2 + 1] << 8);
            const o = (y * W + x) * 3;
            rgb[o]     = ((v >> 11) & 31) * 255 / 31 | 0;
            rgb[o + 1] = ((v >> 5) & 63) * 255 / 63 | 0;
            rgb[o + 2] = (v & 31) * 255 / 31 | 0;
        }
    fs.writeFileSync(OUT, png(W, H, rgb));
    console.log(OUT + '  ' + W + 'x' + H + '  ' + fs.statSync(OUT).size + ' B');
});
