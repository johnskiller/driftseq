/* DriftSeq control map values — see map.h for why there is no JSON/map2c step.
 * Panel order (keyscan.h): FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC
 * OCT- OCT+  ->  CC 40..53.  Encoders: K1..K4 + GLO/? per spec 02 §4 -> 20..26.
 */
#include "map.h"

static const uint8_t enc_cc[MAP_NENC] = { 20, 21, 22, 23, 24, 25, 26 };
static const uint8_t btn_cc[MAP_NBTN] = { 40, 41, 42, 43, 44, 45, 46, 47,
                                          48, 49, 50, 51, 52, 53 };

uint8_t map_encoder_cc(int i) { return (i >= 0 && i < MAP_NENC) ? enc_cc[i] : 0; }
uint8_t map_btn_cc(int i)     { return (i >= 0 && i < MAP_NBTN) ? btn_cc[i] : 0; }

/* DriftSeq has no RGB LED grid: the panel's own LEDs are driven by the host
 * over MIDI (spec 02 §4), so the pad-grid feedback path stays off. */
map_led_cfg_t map_led(void)   { return (map_led_cfg_t){ 0, 0, 0 }; }
uint8_t map_mirror(void)      { return 1; }   /* key LEDs mirror MIDI notes */
uint8_t map_btn_led0(void)    { return 54; }  /* button LEDs: CC 54..67     */
uint8_t map_btn_mode(void)    { return 0; }   /* buttons are CCs, not notes */
