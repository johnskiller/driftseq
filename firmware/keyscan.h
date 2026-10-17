/* fm1-ctrl key scan — pure logic, no I/O (host-tested per plan Task 1).
 *
 * Real panel topology (Felucca-lineage panel.c, verified against CTL-1):
 *   27 keys   idx 0..26 = notes 53..79 (F3..G5), CTL-1 KEY0_NOTE 53
 *   14 buttons idx 27..40, panel order:
 *     FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+
 *     -> CC 40..53 (default map, JSON-overridable), 127 on press, 0 on release
 *   7 encoders (encoders.c): SELECT ALGORITHM PRESETS KNOB1..4 -> CC 20..26
 *
 * Key velocity fixed 100 (0x64) in v1; buttons are momentary CCs.
 */
#ifndef KEYSCAN_H
#define KEYSCAN_H
#include <stdint.h>

#define NKEYS   27
#define NBUTTONS 14
#define NSTATE  (NKEYS + NBUTTONS)   /* 41 scan points */

void keyscan_init(void);
/* raw_mask bit i = scan point i contact closed this frame (raw, pre-debounce) */
void keyscan_poll(uint64_t raw_mask);

#endif
