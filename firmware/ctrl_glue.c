#include "ctrl_glue.h"
#include "keyscan.h"
#include "encoders.h"
#include "leds.h"
#include "midi_io.h"

void ctrl_init(void) {
    keyscan_init(); encoders_init(); leds_init(); midi_io_init();
}

void ctrl_frame_scan(uint64_t raw) { keyscan_poll(raw); }
void ctrl_frame_enc(uint16_t ab, uint8_t push) { encoders_poll(ab, push); }
void ctrl_midi_in3(uint8_t st, uint8_t d1, uint8_t d2) { midi_recv3(st, d1, d2); }
