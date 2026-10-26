/* ui_gallery.js — a second view of the screen, integer-scaled.
 *
 * Why this exists: the panel canvas has width=240 height=240 attributes but its
 * CSS width is a percentage of the device box (panel.js sets it to
 * (ss-2*inset)/L.w of the panel, i.e. 248/1000). On a normal desktop that
 * works out to roughly 1.04x, and with `image-rendering: pixelated` a 1px line
 * lands 1px wide on some columns and 2px on others. The panel is the right
 * thing to play with and the wrong thing to judge a 1-pixel detail on.
 *
 * This canvas shows the same 240x240 framebuffer at an exact integer scale, so
 * one source pixel is exactly N device pixels. Pick N with the wheel; the
 * default is the largest integer that fits the column and stays at least 2x,
 * because 1x is too small to look at.
 *
 * It reads the SAME framebuffer as the panel, so there is nothing to keep in
 * sync and no second copy of the pixels: emu.js calls DSGallery.mirror(imgData)
 * right after it blits the panel.
 */
'use strict';

const DSGallery = (() => {
    const SRC = 240;                       /* framebuffer is 240x240 */
    let cv = null, ctx = null, img = null, scale = 0, userPicked = false;

    function fit() {
        /* Largest integer scale that fits the column, clamped to [2, 6]. The
         * available width is whatever the flex layout leaves us; measuring the
         * canvas' own parent is the only reliable way after a resize. */
        const avail = cv.parentElement.clientWidth || 2 * SRC;
        let s = Math.floor(avail / SRC);
        if (s < 2) s = 2;
        if (s > 6) s = 6;
        return s;
    }

    function apply() {
        if (!scale) scale = fit();
        cv.width = SRC;                     /* attributes stay 1:1 with the fb */
        cv.height = SRC;
        cv.style.width = (SRC * scale) + 'px';
        cv.style.height = (SRC * scale) + 'px';
        const cap = document.getElementById('magcap');
        if (cap) {
            cap.textContent = scale + '\u00d7 exact  (' + SRC + '\u00d7' + SRC +
                              ' px \u2192 ' + (SRC * scale) + ' px)' +
                              (scale === 1 ? '  \u2014 1:1 with the device' : '');
        }
        /* resizing a canvas clears it; repaint from the last mirrored frame */
        if (img) ctx.putImageData(img, 0, 0);
    }

    function init() {
        cv = document.getElementById('magnified');
        if (!cv) return;
        ctx = cv.getContext('2d');
        apply();

        cv.addEventListener('wheel', (e) => {
            e.preventDefault();
            userPicked = true;
            const next = scale + (e.deltaY < 0 ? 1 : -1);
            if (next < 1 || next > 8) return;
            scale = next;
            apply();
        }, { passive: false });

        cv.addEventListener('dblclick', () => {      /* back to the auto fit */
            userPicked = false;
            scale = 0;
            apply();
        });

        window.addEventListener('resize', () => {
            if (userPicked) return;                  /* respect a deliberate choice */
            const want = fit();
            if (want !== scale) { scale = want; apply(); }
        });
    }

    /* emu.js blit() hands us the ImageData it just drew: same pixels, no copy */
    function mirror(imageData) {
        if (!ctx) return;
        img = imageData;
        ctx.putImageData(img, 0, 0);
    }

    return { init, mirror, get scale() { return scale; } };
})();

/* A top-level `const` in a classic script is NOT a property of window, so
 * emu.js's `if (window.DSGallery)` guard would never fire. Assign explicitly. */
window.DSGallery = DSGallery;

if (document.readyState === 'loading')
    document.addEventListener('DOMContentLoaded', DSGallery.init);
else
    DSGallery.init();
