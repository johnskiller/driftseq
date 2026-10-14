/* drift_web.c — wasm bridge for the emu. Exposes the engine + rendering-independent
 * state to JS; the 240×240 screen is drawn in JS canvas (specs 03), audio = WebMIDI out
 * or WebAudio beeps (v1). Deterministic: JS only feeds wall-clock deltas.
 */
#include "driftseq.h"
#include "demo_pattern.h"
#include <emscripten/emscripten.h>
#include <string.h>

static ds_state g;

EMSCRIPTEN_KEEPALIVE
void *ds_new(uint64_t seed) { ds_load_demo(&g); ds_rng_seed(&g.rng, seed); return &g; }

EMSCRIPTEN_KEEPALIVE
ds_state *ds_get(void) { return &g; }

EMSCRIPTEN_KEEPALIVE
void ds_tick_wasm(double dt) { ds_tick(&g, dt); }

EMSCRIPTEN_KEEPALIVE
void ds_play_wasm(void)  { ds_play(&g); }
EMSCRIPTEN_KEEPALIVE
void ds_stop_wasm(void)  { ds_stop(&g); }
EMSCRIPTEN_KEEPALIVE
void ds_set_bpm_wasm(double bpm) { ds_set_bpm(&g, bpm); }

/* ---- editing from the emu UI (indices validated in JS) ---- */
EMSCRIPTEN_KEEPALIVE
int ds_voice_add_wasm(int trk, int step, int note, int vel, int gate) {
    if (trk < 0 || trk >= DS_TRACKS || step < 0 || step >= DS_STEPS) return -3;
    return ds_voice_add(&g.pat.track[trk], (uint32_t)step, (uint8_t)note, (uint8_t)vel, (uint8_t)gate);
}
EMSCRIPTEN_KEEPALIVE
int ds_voice_remove_wasm(int trk, int step, int note) {
    if (trk < 0 || trk >= DS_TRACKS || step < 0 || step >= DS_STEPS) return -3;
    return ds_voice_remove(&g.pat.track[trk], (uint32_t)step, (uint8_t)note);
}
EMSCRIPTEN_KEEPALIVE
void ds_toggle_step_wasm(int trk, int step) {
    if (trk < 0 || trk >= DS_TRACKS || step < 0 || step >= DS_STEPS) return;
    g.pat.track[trk].steps[step].on = !g.pat.track[trk].steps[step].on;
}
EMSCRIPTEN_KEEPALIVE
void ds_clear_track_wasm(int trk) {
    if (trk < 0 || trk >= DS_TRACKS) return;
    ds_clear_track(&g.pat.track[trk]);
}
EMSCRIPTEN_KEEPALIVE
void ds_euclid_wasm(int trk, int on, int k, int n, int rot) {
    if (trk < 0 || trk >= DS_TRACKS) return;
    g.pat.track[trk].eu_on = (uint8_t)on;
    g.pat.track[trk].eu_k = (uint8_t)k;
    g.pat.track[trk].eu_n = (uint8_t)n;
    g.pat.track[trk].eu_rot = (uint8_t)rot;
    if (on) ds_euclid_fill(&g.pat.track[trk]);
}
EMSCRIPTEN_KEEPALIVE
void ds_set_loop_wasm(int trk, int start, int len) {
    if (trk < 0 || trk >= DS_TRACKS) return;
    if (start >= 0 && start < 64) g.pat.track[trk].loop_start = (uint8_t)start;
    if (len >= 1 && len <= 64)    g.pat.track[trk].loop_len = (uint8_t)len;
}
EMSCRIPTEN_KEEPALIVE
void ds_set_prog_wasm(int trk, int prog) {
    if (trk < 0 || trk >= DS_TRACKS || prog < 0 || prog > 127) return;
    g.pat.track[trk].prog = (uint8_t)prog;
    g.pc_sent[trk] = 0;   /* resend on next tick */
}
EMSCRIPTEN_KEEPALIVE
void ds_set_swing_wasm(int global, int amt) {
    if (global) g.global_swing = (uint8_t)amt;
    else g.pat.track[g.sel_track].swing_amt = (uint8_t)amt;
}
EMSCRIPTEN_KEEPALIVE
void ds_select_track_wasm(int trk) { if (trk >= 0 && trk < DS_TRACKS) g.sel_track = (uint8_t)trk; }
EMSCRIPTEN_KEEPALIVE
void ds_mute_wasm(int trk, int m) {
    if (trk >= 0 && trk < DS_TRACKS) g.pat.track[trk].mute = (uint8_t)(m != 0);
}

/* ---- exact field offsets so JS reads volatiles without copy APIs ---- */
EMSCRIPTEN_KEEPALIVE
int ds_offset_out(void)      { return (int)((uintptr_t)&g.out - (uintptr_t)&g); }
EMSCRIPTEN_KEEPALIVE
int ds_offset_out_len(void)  { return (int)((uintptr_t)&g.out_len - (uintptr_t)&g); }
EMSCRIPTEN_KEEPALIVE
int ds_offset_pos16(void)    { return (int)((uintptr_t)&g.pos16 - (uintptr_t)&g); }
EMSCRIPTEN_KEEPALIVE
int ds_offset_frac16(void)   { return (int)((uintptr_t)&g.frac16 - (uintptr_t)&g); }
EMSCRIPTEN_KEEPALIVE
int ds_offset_bpm(void)      { return (int)((uintptr_t)&g.bpm - (uintptr_t)&g); }
EMSCRIPTEN_KEEPALIVE
int ds_offset_sel_track(void){ return (int)((uintptr_t)&g.sel_track - (uintptr_t)&g); }
EMSCRIPTEN_KEEPALIVE
int ds_offset_mute(void)     { return (int)((uintptr_t)&g.pat.track[0].mute - (uintptr_t)&g); }
EMSCRIPTEN_KEEPALIVE
int ds_track_stride(void)    { return (int)sizeof(ds_track); }
EMSCRIPTEN_KEEPALIVE
int ds_track_off(void)       { return (int)((uintptr_t)&g.pat.track[0] - (uintptr_t)&g); }
/* ds_track field offsets */
EMSCRIPTEN_KEEPALIVE
int ds_trk_off_channel(void) { ds_track t; return (int)((uintptr_t)&t.channel - (uintptr_t)&t); }
EMSCRIPTEN_KEEPALIVE
int ds_trk_off_prog(void)    { ds_track t; return (int)((uintptr_t)&t.prog - (uintptr_t)&t); }
EMSCRIPTEN_KEEPALIVE
int ds_trk_off_loop_start(void){ ds_track t; return (int)((uintptr_t)&t.loop_start - (uintptr_t)&t); }
EMSCRIPTEN_KEEPALIVE
int ds_trk_off_loop_len(void){ ds_track t; return (int)((uintptr_t)&t.loop_len - (uintptr_t)&t); }
EMSCRIPTEN_KEEPALIVE
int ds_trk_off_division(void){ ds_track t; return (int)((uintptr_t)&t.division - (uintptr_t)&t); }
EMSCRIPTEN_KEEPALIVE
int ds_trk_off_mute(void)    { ds_track t; return (int)((uintptr_t)&t.mute - (uintptr_t)&t); }
EMSCRIPTEN_KEEPALIVE
int ds_trk_off_steps(void)   { ds_track t; return (int)((uintptr_t)&t.steps - (uintptr_t)&t); }
EMSCRIPTEN_KEEPALIVE
int ds_step_size(void)       { return (int)sizeof(ds_step); }
EMSCRIPTEN_KEEPALIVE
int ds_step_off_on(void)     { ds_step s; return (int)((uintptr_t)&s.on - (uintptr_t)&s); }
EMSCRIPTEN_KEEPALIVE
int ds_step_off_nvoices(void){ ds_step s; return (int)((uintptr_t)&s.nvoices - (uintptr_t)&s); }
EMSCRIPTEN_KEEPALIVE
int ds_step_off_prob(void)   { ds_step s; return (int)((uintptr_t)&s.prob - (uintptr_t)&s); }
EMSCRIPTEN_KEEPALIVE
int ds_voice_off_note(void)  { ds_note n; return (int)((uintptr_t)&n.note - (uintptr_t)&n); }
EMSCRIPTEN_KEEPALIVE
int ds_voice_size(void)      { return (int)sizeof(ds_note); }
EMSCRIPTEN_KEEPALIVE
int ds_step_off_voices(void) { ds_step s; return (int)((uintptr_t)&s.v - (uintptr_t)&s); }
EMSCRIPTEN_KEEPALIVE
int ds_trk_off_eu_k(void)    { ds_track t; return (int)((uintptr_t)&t.eu_k - (uintptr_t)&t); }
EMSCRIPTEN_KEEPALIVE
int ds_trk_off_eu_n(void)    { ds_track t; return (int)((uintptr_t)&t.eu_n - (uintptr_t)&t); }
