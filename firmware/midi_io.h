/* fm1-ctrl MIDI I/O — the only module that knows about the transport.
 *
 * Direction names are from the device's point of view:
 *   midi_emit3(): device -> host  (USB MIDI out on hardware)
 *   midi_recv3(): host  -> device (USB MIDI in on hardware)
 *
 * Backends (compile-time):
 *   MIDI_HOST  capture emitted messages into a ring for tests/host sim
 *   MIDI_USB   JieLi SDK USB-MIDI; routes through midi_transport_send,
 *              which the unity root (ctrl.c) points at plat_midi_out.
 */
#ifndef MIDI_IO_H
#define MIDI_IO_H
#include <stdint.h>

typedef struct { uint8_t b[3]; } midi_msg3;

/* The portability boundary: on hardware, ctrl.c sets this to a thunk that
 * packs (st,d1,d2) into a USB-MIDI event packet for plat_midi_out(). The
 * host backend ignores it and captures into the test ring instead. */
typedef void (*midi_transport_fn)(uint8_t st, uint8_t d1, uint8_t d2);
extern midi_transport_fn midi_transport_send;

void midi_io_init(void);
void midi_emit3(uint8_t st, uint8_t d1, uint8_t d2);
void midi_recv3(uint8_t st, uint8_t d1, uint8_t d2);

/* Host-backend capture (tests + sim only). */
#ifdef MIDI_HOST
void      midi_cap_clear(void);
int       midi_cap_len(void);
midi_msg3 midi_cap_get(int i);
#endif

#endif
