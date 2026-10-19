/* DriftSeq control map — the interface the input modules see.
 *
 * DriftSeq ships ONE panel layout (FM-1 melodee): 14 buttons -> CC 40..53 in
 * panel order, 7 encoders -> CC 20..26, no LED grid, key-LED mirror on. So
 * unlike fm1-ctrl there is no docs/maps JSON and no map2c.py step: the values
 * in map.c ARE the map. The accessor interface is kept identical to fm1-ctrl's
 * so a runtime SysEx map can be dropped in later without touching keyscan.c /
 * encoders.c / leds.c.
 */
#ifndef MAP_H
#define MAP_H
#include <stdint.h>

#define MAP_NENC 7
#define MAP_NBTN 14

typedef struct { uint8_t present, lo, hi; } map_led_cfg_t;

uint8_t map_encoder_cc(int idx);      /* encoder i -> CC number (0..6)      */
uint8_t map_btn_cc(int i);            /* button i -> CC number (0..13)      */
map_led_cfg_t map_led(void);          /* LED grid CC range (present flag)   */
uint8_t map_mirror(void);             /* key-LED note mirror enabled        */
uint8_t map_btn_led0(void);           /* button-LED feedback base CC        */
uint8_t map_btn_mode(void);           /* 0 = buttons send CC, 1 = notes     */

#endif
