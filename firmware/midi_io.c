#include "midi_io.h"
#include "leds.h"

midi_transport_fn midi_transport_send = 0;

void midi_io_init(void) {}

/* host -> device: route feedback per spec §5.
 * CC 0..15   -> LED grid rows   (map-convention outputs.ledGrid)
 * note on/off-> key LED mirror  (map-convention outputs.keyLeds)
 * everything else ignored */
void midi_recv3(uint8_t st, uint8_t d1, uint8_t d2) {
    uint8_t t = st & 0xF0;
    if (t == 0xB0) {
        leds_on_cc(d1, d2);
    } else if (t == 0x90 || t == 0x80) {
        leds_on_note(d1, (t == 0x90) && (d2 != 0)); /* vel-0 note-on = off */
    }
}

#ifdef MIDI_HOST
static midi_msg3 cap[64];
static int cap_n;

void      midi_cap_clear(void) { cap_n = 0; }
int       midi_cap_len(void)   { return cap_n; }
midi_msg3 midi_cap_get(int i)  { return cap[i]; }
#endif

void midi_emit3(uint8_t st, uint8_t d1, uint8_t d2) {
#ifdef MIDI_HOST
    if (cap_n < 64) {
        cap[cap_n].b[0] = st; cap[cap_n].b[1] = d1; cap[cap_n].b[2] = d2;
        cap_n++;
    }
#else
    if (midi_transport_send)
        midi_transport_send(st, d1, d2);
#endif
}
