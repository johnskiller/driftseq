/* fm1-ctrl encoders — 7 rotary encoders, quadrature decode -> relative CC.
 *
 * ab:   2 bits per encoder (A|B pair), encoder i in bits [2i, 2i+1]
 * push: bit i = encoder i switch held (SELECT/ALGO/PRESETS have none)
 *
 * One mechanical detent = one full quadrature cycle (4 valid transitions).
 * turn        -> CC ch1 (0xB0), value 64+delta (MCU-style: 0x41=+1, 0x3F=-1)
 * push + turn -> same relative CC on ch2 (0xB1) — a second layer for the shim
 * CC numbers come from the compiled-in map (map_data.c); melodee-native
 * default = CC 20..26 for SELECT/ALGO/PRESETS/K1..K4.
 */
#ifndef ENCODERS_H
#define ENCODERS_H
#include <stdint.h>

#define NENC 7

void encoders_init(void);
void encoders_poll(uint16_t ab, uint8_t push);

#endif
