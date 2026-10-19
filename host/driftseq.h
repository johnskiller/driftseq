/* driftseq.h — core data model (spec 01-core-model, 7c7f9bd) */
#ifndef DRIFTSEQ_H
#define DRIFTSEQ_H

#include <stdint.h>
#include <stddef.h>          /* NULL — freestanding clang ships stddef.h */

#define DS_TRACKS   16
#define DS_STEPS    64
#define DS_VOICES   4
#define DS_PATTERNS 16
#define DS_CHAIN    64
#define DS_GATES    64                          /* scheduled sequenced note-offs */

enum { DS_ROUTE_USB = 0, DS_ROUTE_GM, DS_ROUTE_BOTH };
enum { DS_DIV_8 = 0, DS_DIV_8T, DS_DIV_16, DS_DIV_16T, DS_DIV_32 };
enum { DS_VOICE_POLY = 0, DS_VOICE_MONO };

/* conditions (01 §4) */
enum {
    DS_C_NONE = 0,
    DS_C_EVERY2, DS_C_EVERY3, DS_C_EVERY4, DS_C_EVERY5, DS_C_EVERY6, DS_C_EVERY7,
    DS_C_FIRST,          /* only first cycle of this pattern in the chain */
    DS_C_LAST4,          /* only when <= 4 cycles remain */
    DS_C_FILL,           /* one-shot when FILL flag set */
    DS_C_FLAM            /* only if previous step triggered */
};
#define DS_COND_COUNT 11

typedef struct {
    uint8_t note;   /* 0..127 MIDI, 0xFF = none */
    uint8_t vel;    /* 1..127 */
    uint8_t gate;   /* in steps of this track's division; 0xFF = until next event or loop end */
    uint8_t on;     /* en bit: 0 = silent-keep (single-click toggles this) */
} ds_note;

typedef struct {
    ds_note  v[DS_VOICES];      /* note 0xFF = empty voice */
    uint8_t  nvoices;           /* count of used voices */
    uint8_t  on;                /* en bit: 0 = silent-keep (single-click toggles this) */
    uint8_t  prob;              /* 0..100, 100 default */
    uint8_t  cond;              /* ds_cond enum */
} ds_step;

typedef struct {
    ds_step  steps[DS_STEPS];
    uint8_t  division;          /* DS_DIV_* */
    uint8_t  loop_start;        /* 0..63 — window = [start, start+len) mod 64 */
    uint8_t  loop_len;          /* 1..64 */
    uint8_t  channel;           /* 0..15 */
    uint8_t  route;             /* DS_ROUTE_* */
    uint8_t  prog;              /* GM program (bank MSB ignored v1) */
    uint8_t  mute;
    uint8_t  swing_amt;         /* 0..100: how much of the global swing applies */
    uint8_t  voice_mode;        /* DS_VOICE_* */
    uint8_t  eu_on, eu_k, eu_n, eu_rot;
} ds_track;

typedef struct {
    ds_track track[DS_TRACKS];
} ds_pattern;

typedef struct {
    uint8_t pattern;    /* pattern index */
    uint8_t repeat;     /* cycle count */
} ds_chain_entry;

typedef struct {
    uint64_t state;         /* xorshift64 */
} ds_rng;

/* engine state (one editing pattern lives in RAM per 01 §2) */
typedef struct {
    ds_pattern  pat;                    /* the live pattern */
/* The 16-pattern bank is 323 KB — fine on the host and in the browser, but the
 * FM-1 has 96 KiB of RAM, so the firmware build leaves it out (v1 keeps the
 * live pattern + saved patterns in flash). Nothing reads it but ds_init's
 * clearing loop, so the golden byte stream is identical either way. */
#ifndef DS_PATTERN_BANK_IN_STATE
#define DS_PATTERN_BANK_IN_STATE 1
#endif
#if DS_PATTERN_BANK_IN_STATE
    ds_pattern  flash[DS_PATTERNS];     /* all patterns (host: in RAM; FW: flash partition) */
#endif
    ds_chain_entry chain[DS_CHAIN];
    uint8_t     chain_len;

    double      bpm;                    /* quarter-note BPM */
    uint8_t     global_swing;           /* 0..100 */
    uint8_t     fill_flag;              /* one-shot, consumed on next FILL step */

    /* per-track scheduler phase (all here — engine has no hidden globals) */
    uint32_t    pos16[DS_TRACKS];       /* current cell index (the playhead) */
    double      frac16[DS_TRACKS];      /* sub-cell accumulator, [0,1) */
    uint8_t     pending[DS_TRACKS];     /* fire loop_start on next tick after play */
    uint8_t     pc_sent[DS_TRACKS];     /* program-change sent once per run */
    uint8_t     playing;
    double      tick_acc;               /* absolute time in ticks @24 PPQ since play (hero axis) */
    uint8_t     sel_track;              /* UI-selected track (emu + panel SEL) */
    uint8_t     chain_cycle[DS_TRACKS]; /* completed window passes (for conditions) */

    ds_rng      rng;

    /* MIDI out byte stream (deterministic contract, 01 §7) */
    uint8_t     out[4096];
    uint32_t    out_len;

    /* live keyboard (step-entry preview): small pool of held notes */
    struct { uint8_t trk, note; double t; } live[16];
    uint8_t     live_n;

    /* scheduled note-offs for sequenced notes (gate). Lives in the state, not
     * in a file-static, so two engines in one process cannot cross-talk
     * (host tests run two states side by side). */
    struct { uint8_t trk, note; double t; } gates[DS_GATES];
    uint8_t     gates_n;
} ds_state;

/* rng */
void     ds_rng_seed(ds_rng *r, uint64_t seed);
uint32_t ds_rng_next(ds_rng *r);              /* 0..2^32-1 */

/* lifecycle */
void     ds_init(ds_state *s, uint64_t seed);
void     ds_clear_pattern(ds_pattern *p);
void     ds_clear_track(ds_track *t);

/* transport */
void     ds_play(ds_state *s);
void     ds_stop(ds_state *s);
void     ds_set_bpm(ds_state *s, double bpm);

/* the engine tick: call once per audio block (64 frames @ 44100).
 * advances time, fills s->out with MIDI bytes for this tick, resets out_len.
 * deterministic: same state + same inputs -> same bytes. */
void     ds_tick(ds_state *s, double dt_seconds);

/* live keyboard: direct note on/off through the same out stream (step-entry
 * preview). held notes get a scheduled note-off after gate_steps * cell time. */
int      ds_live_on(ds_state *s, uint8_t trk, uint8_t note, uint8_t vel);
int      ds_live_off(ds_state *s, uint8_t trk, uint8_t note);

/* editing helpers (used by UI / tests) */
int      ds_voice_add(ds_track *t, uint32_t step, uint8_t note, uint8_t vel, uint8_t gate);
int      ds_voice_remove(ds_track *t, uint32_t step, uint8_t note);
void     ds_euclid_fill(ds_track *t);           /* (re)paint steps from eu meta, only 'on' bits */
void     ds_bjorklund(int n, int k, uint8_t *out);  /* painter access (ARP ring/strip) */
uint8_t  ds_step_active(const ds_track *t, uint32_t step);  /* inside window AND has voices */

#endif
