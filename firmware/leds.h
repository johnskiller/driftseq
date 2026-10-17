/* fm1-ctrl LEDs — 5×7 grid + key/button LEDs, driven FROM MIDI-in.
 * Pure state; the firmware LED matrix driver reads it, the host sim prints it.
 */
#ifndef LEDS_H
#define LEDS_H
#include <stdint.h>

void    leds_init(void);
void    leds_on_cc(uint8_t cc, uint8_t val);   /* CC 0..15 = grid rows */
void    leds_on_note(uint8_t note, int on);    /* mirror, key range only */
uint8_t leds_btn_get(int i);                   /* host/test readback */
uint8_t leds_grid_get(int cc);                 /* host/test readback */
uint8_t leds_key_get(int note);

#endif
