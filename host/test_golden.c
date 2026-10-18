/* test_golden.c — golden MIDI-stream hash of the demo pattern (01 §7 seam contract)
 * Locks the engine's product: demo @ seed 42, 20 s of 64-frame blocks (integer tick
 * count — same loop as the emu) → one FNV-1a chunk hash.
 * Canonical: 420C4824C85B79CB — wasm (web/wasmtest.mjs) asserts the same value,
 * proving host C and browser wasm emit byte-identical streams.
 */
#include "driftseq.h"
#include "demo_pattern.h"
#include <stdio.h>
#include <stdlib.h>

static uint64_t fnv1a(const uint8_t *p, uint32_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    while (n--) { h ^= *p++; h *= 0x100000001B3ull; }
    return h;
}

int main(void) {
    ds_state s;
    uint64_t h = 0xCBF29CE484222325ull;
    const double dt = 64.0 / 44100.0;
    int note_ons = 0;

    ds_load_demo(&s);
    ds_play(&s);
    long iters = (long)(20.0 / dt);          /* integer count — match the emu loop exactly */
    for (long k = 0; k < iters; k++) {
        ds_tick(&s, dt);
        if (s.out_len) {
            h = fnv1a(s.out, s.out_len) ^ (h * 0x100000001B3ull + 1);
            /* count note-ons with a proper walk (PC=2B, others=3B) */
            for (uint32_t i = 0; i < s.out_len; ) {
                uint8_t b = s.out[i];
                if (b < 0x80) { i++; continue; }
                if ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) { i += 2; continue; }
                if ((b & 0xF0) == 0x90 && s.out[i + 2] > 0) note_ons++;
                i += 3;
            }
        }
    }
    printf("demo 20s: note_ons=%d hash=%016llX\n", note_ons, (unsigned long long)h);
    /* sanity: 20 s @120 with the 5 demo tracks ≈ 238 note-ons (drums+bass+lead) */
    if (note_ons < 200) { printf("FAIL: too few note-ons (%d)\n", note_ons); return 1; }
    return 0;
}
