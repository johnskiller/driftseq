/* demo_pattern.c — the default DriftSeq demo: instant polymetric drift.
 *
 * Windows 16 / 12 / 8 / 12 with different divisions so the hero screen
 * drifts immediately; drums on GM ch10 (channel index 9), bass+lead on ch0/ch1.
 */
#include "demo_pattern.h"

void ds_load_demo(ds_state *s) {
    ds_track *t;
    ds_init(s, 42);

    /* trk0: kick — E(16,4) four-on-floor, ch10 */
    t = &s->pat.track[0];
    t->channel = 9; t->prog = 0; t->loop_len = 16;
    t->eu_on = 1; t->eu_k = 4; t->eu_n = 16; t->eu_rot = 0;
    ds_euclid_fill(t);
    {   int i;
        for (i = 0; i < 64; i++)
            if (t->steps[i].on) ds_voice_add(t, i, 36, 127, 1);
    }

    /* trk1: snare — E(12,5) tresillo-ish, ch10, offset start */
    t = &s->pat.track[1];
    t->channel = 9; t->prog = 0; t->loop_len = 12; t->loop_start = 4;
    t->eu_on = 1; t->eu_k = 5; t->eu_n = 12; t->eu_rot = 0;
    ds_euclid_fill(t);
    {   int i;
        for (i = 0; i < 64; i++)
            if (t->steps[i].on) ds_voice_add(t, i, 38, 110, 1);
    }

    /* trk2: hats — E(8,3), ch10 */
    t = &s->pat.track[2];
    t->channel = 9; t->prog = 0; t->loop_len = 8;
    t->eu_on = 1; t->eu_k = 3; t->eu_n = 8; t->eu_rot = 0;
    ds_euclid_fill(t);
    {   int i;
        for (i = 0; i < 64; i++)
            if (t->steps[i].on) ds_voice_add(t, i, 42, 70, 1);
    }

    /* trk3: bass (GM ch0, prog 38 synth bass) — 12-step loop, syncopated */
    t = &s->pat.track[3];
    t->channel = 0; t->prog = 38; t->loop_len = 12; t->division = DS_DIV_16;
    ds_voice_add(t, 0, 36 + 12, 100, 2);   /* C2 */
    ds_voice_add(t, 6, 39 + 12, 90, 2);    /* D#2 */
    ds_voice_add(t, 10, 43 + 12, 95, 1);   /* G2 */

    /* trk4: lead (GM ch1, prog 81 lead square) — 16-step arp with prob */
    t = &s->pat.track[4];
    t->channel = 1; t->prog = 81; t->loop_len = 16; t->division = DS_DIV_16;
    ds_voice_add(t, 0, 72, 80, 1);   /* C5 */
    ds_voice_add(t, 3, 76, 70, 1);   /* E5 */
    ds_voice_add(t, 6, 79, 80, 1);   /* G5 */
    ds_voice_add(t, 11, 84, 75, 1);  /* C6, later prob'd down */
    t->steps[11].prob = 55;

    /* trk5..: silent */
}
