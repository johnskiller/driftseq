#include "encoders.h"
#include "midi_io.h"
#include "map.h"

static uint8_t prev_q[NENC];
static int     steps[NENC]; /* signed transitions within current detent */

void encoders_init(void) {
    for (int i = 0; i < NENC; i++) { prev_q[i] = 0; steps[i] = 0; }
}

/* gray-code quadrature: [from<<2|to] -> +1 CW / -1 CCW / 0 (invalid = skip) */
static int step_delta(uint8_t from, uint8_t to) {
    static const int8_t tbl[16] = {
        /* from 00 */  0, +1, -1,  0,
        /* from 01 */ -1,  0,  0, +1,
        /* from 10 */ +1,  0,  0, -1,
        /* from 11 */  0, -1, +1,  0,
    };
    return tbl[((from & 3) << 2) | (to & 3)];
}

static void emit(int i, int pushed, int delta) {
    uint8_t st = 0xB0 | (pushed ? 1 : 0);
    midi_emit3(st, map_encoder_cc(i), (uint8_t)(64 + delta));
}

void encoders_poll(uint16_t ab, uint8_t push) {
    for (int i = 0; i < NENC; i++) {
        uint8_t q = (ab >> (i * 2)) & 3;
        if (q == prev_q[i]) continue;
        steps[i] += step_delta(prev_q[i], q);
        prev_q[i] = q;
        if (steps[i] >= 4)  { steps[i] = 0; emit(i, (push >> i) & 1u, +1); }
        if (steps[i] <= -4) { steps[i] = 0; emit(i, (push >> i) & 1u, -1); }
    }
}
