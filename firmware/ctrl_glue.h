/* fm1-ctrl glue — the frame functions that run on-device, in a host-testable
 * form. No HAL includes here: on the FM-1, ctrl.c's unity root feeds these
 * from fm1_in.* / plat_midi_* (see docs/research/usb-midi.md §6).
 */
#ifndef CTRL_GLUE_H
#define CTRL_GLUE_H
#include <stdint.h>

void ctrl_init(void);
/* one scan frame: raw bit i = scan point i closed (27 keys, then 14 buttons) */
void ctrl_frame_scan(uint64_t raw);
/* one encoder frame: ab = 2 bits per encoder, push = bit per encoder */
void ctrl_frame_enc(uint16_t ab, uint8_t push);
/* one MIDI-in message from the host (LED feedback path) */
void ctrl_midi_in3(uint8_t st, uint8_t d1, uint8_t d2);

#endif
