/* driftseq.c — deterministic sequencer engine (spec 01-core-model)
 *
 * Contract: ds_tick(state, dt) emits this tick's MIDI bytes into state->out.
 * Same state + same dt sequence -> same bytes (seeded RNG, all scheduler state
 * in ds_state — no hidden globals). The product IS the MIDI byte stream (01 §7).
 *
 * Core semantics:
 *  - Each track's loop window [loop_start, +loop_len) IS the loop: the playhead
 *    wraps inside it (SQ-64 RANGE). Different window lengths + divisions => the
 *    tracks drift apart and re-align at LCM — that is the product.
 *  - pos16[] counts CELLS of the track's division (16th grid slots, re-timed by
 *    division). Play launches every track on its own loop_start (aligned at t=0,
 *    drifting after).
 *  - Live window morph (K3/K4) is just a param read per tick: playhead outside
 *    the new window snaps to the new start, notes are never hard-cut.
 */
#include "driftseq.h"
#include <string.h>

/* ---------- rng (xorshift64) ---------- */
void ds_rng_seed(ds_rng *r, uint64_t seed) { r->state = seed ? seed : 0x9E3779B97F4A7C15ull; }
uint32_t ds_rng_next(ds_rng *r) {
    uint64_t x = r->state;
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    r->state = x;
    return (uint32_t)(x >> 16);
}

/* ---------- init ---------- */
void ds_clear_track(ds_track *t) {
    ds_step *st;
    uint32_t i;
    memset(t, 0, sizeof *t);
    for (i = 0; i < DS_STEPS; i++) {
        st = &t->steps[i];
        memset(st->v, 0xFF, sizeof st->v);   /* note 0xFF = empty */
        st->prob = 100;
        st->cond = DS_C_NONE;
    }
    t->loop_len = 16;
    t->loop_start = 0;
    t->division = DS_DIV_16;
    t->route = DS_ROUTE_BOTH;
    t->prog = 0;
    t->swing_amt = 100;
    t->voice_mode = DS_VOICE_POLY;
    t->eu_n = 16; t->eu_k = 4;
}
void ds_clear_pattern(ds_pattern *p) {
    int i;
    for (i = 0; i < DS_TRACKS; i++) ds_clear_track(&p->track[i]);
}
void ds_init(ds_state *s, uint64_t seed) {
    int i;
    memset(s, 0, sizeof *s);
    ds_clear_pattern(&s->pat);
    for (i = 0; i < DS_PATTERNS; i++) ds_clear_pattern(&s->flash[i]);
    s->bpm = 120.0;
    s->global_swing = 0;
    ds_rng_seed(&s->rng, seed);
}

/* ---------- transport ---------- */
void ds_play(ds_state *s) {
    int i;
    s->playing = 1;
    for (i = 0; i < DS_TRACKS; i++) {
        s->pc_sent[i] = 0;
        s->pos16[i] = s->pat.track[i].loop_start;  /* each track launches on its own start */
        s->frac16[i] = 0.0;
        s->pending[i] = 1;                          /* start step sounds at t=0 */
        s->chain_cycle[i] = 0;
    }
}
void ds_stop(ds_state *s) {
    int i;
    s->playing = 0;
    for (i = 0; i < DS_TRACKS; i++) {               /* all notes off, part of the stream */
        s->out[s->out_len++] = 0xB0 | (s->pat.track[i].channel & 0x0F);
        s->out[s->out_len++] = 123;
        s->out[s->out_len++] = 0;
    }
}
void ds_set_bpm(ds_state *s, double bpm) { if (bpm >= 20.0 && bpm <= 300.0) s->bpm = bpm; }

/* ---------- editing ---------- */
int ds_voice_add(ds_track *t, uint32_t step, uint8_t note, uint8_t vel, uint8_t gate) {
    ds_step *st = &t->steps[step & 63];
    int i;
    if (st->nvoices >= DS_VOICES) return -1;
    for (i = 0; i < st->nvoices; i++)
        if (st->v[i].note == note) return -2;
    st->v[st->nvoices].note = note;
    st->v[st->nvoices].vel = vel;
    st->v[st->nvoices].gate = gate ? gate : 1;
    st->nvoices++;
    st->on = 1;                       /* adding a voice lights the step (single-click revives) */
    return 0;
}
int ds_voice_remove(ds_track *t, uint32_t step, uint8_t note) {
    ds_step *st = &t->steps[step & 63];
    int i, j;
    for (i = 0; i < st->nvoices; i++) {
        if (st->v[i].note == note) {
            for (j = i; j < st->nvoices - 1; j++) st->v[j] = st->v[j + 1];
            st->v[st->nvoices - 1].note = 0xFF;
            st->nvoices--;
            if (!st->nvoices) st->on = 0;
            return 0;
        }
    }
    return -1;
}
uint8_t ds_step_active(const ds_track *t, uint32_t step) {
    uint32_t i = step & 63;
    uint32_t rel = (i + 64 - t->loop_start) & 63;
    if (rel >= t->loop_len) return 0;
    return t->steps[i].nvoices > 0 && t->steps[i].on;
}

/* Bjorklund: k pulses over n slots (Toussaint's euclidean rhythm paper) */
static void bjorklund(int n, int k, uint8_t *out) {
    int bucket = 0, i;
    for (i = 0; i < n; i++) {
        bucket += k;
        if (bucket >= n) { bucket -= n; out[i] = 1; }
        else out[i] = 0;
    }
}
void ds_euclid_fill(ds_track *t) {
    uint8_t pat[64];
    int i, n = t->eu_n ? t->eu_n : 16, k = t->eu_k;
    if (k > n) k = n;
    bjorklund(n, k, pat);
    for (i = 0; i < DS_STEPS; i++) {
        uint32_t rel = (i + 64 - t->loop_start) & 63;
        t->steps[i].on = rel < (uint32_t)t->loop_len ? pat[rel % n] : 0;
    }
}

/* ---------- conditions (01 §4) ---------- */
static int cond_ok(const ds_step *st, const ds_state *s, int trk) {
    switch (st->cond) {
    case DS_C_EVERY2: return ((s->chain_cycle[trk] + 1) % 2) == 0;  /* pass N reads N-1 done */
    case DS_C_EVERY3: return ((s->chain_cycle[trk] + 1) % 3) == 0;
    case DS_C_EVERY4: return ((s->chain_cycle[trk] + 1) % 4) == 0;
    case DS_C_EVERY5: return ((s->chain_cycle[trk] + 1) % 5) == 0;
    case DS_C_EVERY6: return ((s->chain_cycle[trk] + 1) % 6) == 0;
    case DS_C_EVERY7: return ((s->chain_cycle[trk] + 1) % 7) == 0;
    case DS_C_FIRST:  return s->chain_cycle[trk] == 0;
    case DS_C_LAST4:  return 1;      /* v1: chain remainder per track is TODO */
    case DS_C_FILL:   return s->fill_flag != 0;
    case DS_C_FLAM:   return 1;      /* v1: previous-step result tracking TODO */
    default:          return 1;
    }
}

/* ---------- the tick ---------- */
static double cell_seconds(double bpm, int div) {
    double sixteenth = 60.0 / bpm / 4.0;
    switch (div) {
    case DS_DIV_8:   return sixteenth * 2.0;
    case DS_DIV_8T:  return sixteenth * 4.0 / 3.0;
    case DS_DIV_16:  return sixteenth;
    case DS_DIV_16T: return sixteenth * 2.0 / 3.0;
    case DS_DIV_32:  return sixteenth * 0.5;
    default:         return sixteenth;
    }
}

static void emit_note(ds_state *s, const ds_track *t, const ds_note *n) {
    if (s->out_len + 3 > sizeof s->out) return;
    s->out[s->out_len++] = 0x90 | (t->channel & 0x0F);
    s->out[s->out_len++] = n->note & 0x7F;
    s->out[s->out_len++] = n->vel & 0x7F;
}
static void emit_pc(ds_state *s, const ds_track *t) {
    if (s->out_len + 2 > sizeof s->out) return;
    s->out[s->out_len++] = 0xC0 | (t->channel & 0x0F);
    s->out[s->out_len++] = t->prog & 0x7F;
}

/* land on a position: fire its voices per on/cond/prob */
static void land(ds_state *s, ds_track *t, int trk, uint32_t pos) {
    ds_step *st = &t->steps[pos & 63];
    if (!st->on || !st->nvoices) return;
    if (!cond_ok(st, s, trk)) return;
    {
        uint32_t r = ds_rng_next(&s->rng);
        if ((int)(r % 100u) < (int)st->prob) {
            int vi;
            for (vi = 0; vi < st->nvoices; vi++)
                emit_note(s, t, &st->v[vi]);
        }
    }
    if (st->cond == DS_C_FILL) s->fill_flag = 0;    /* one-shot consumed */
}

void ds_tick(ds_state *s, double dt_seconds) {
    int trk;
    if (!s->playing) { s->out_len = 0; return; }
    s->out_len = 0;

    for (trk = 0; trk < DS_TRACKS; trk++) {
        ds_track *t = &s->pat.track[trk];
        double adv;
        if (t->mute) continue;
        if (!s->pc_sent[trk]) { emit_pc(s, t); s->pc_sent[trk] = 1; }

        if (s->pending[trk]) {                       /* launch: loop_start sounds now */
            land(s, t, trk, s->pos16[trk]);
            s->pending[trk] = 0;
        }

        adv = dt_seconds / cell_seconds(s->bpm, t->division);
        /* swing: cells leading INTO odd steps are longer, odd->even shorter,
         * pair stays 2 cells (downbeat never slides). global × per-track, 100 => 3:1 */
        if (s->global_swing && t->swing_amt) {
            double sw = (s->global_swing / 100.0) * (t->swing_amt / 100.0) * 0.5;
            adv *= (s->pos16[trk] & 1u) ? (1.0 - sw) : (1.0 + sw);
        }
        s->frac16[trk] += adv;

        while (s->frac16[trk] >= 1.0) {
            uint32_t start = t->loop_start, len = t->loop_len;
            uint32_t cur, rel, next;
            s->frac16[trk] -= 1.0;
            cur = s->pos16[trk];
            rel = (cur + 64 - start) & 63;
            if (rel >= len)          next = start;                     /* outside: snap (live morph) */
            else if (rel + 1 >= len) { next = start; s->chain_cycle[trk]++; }  /* wrap: pass done */
            else                     next = (start + rel + 1) & 63;
            s->pos16[trk] = next;
            land(s, t, trk, next);
        }
    }
}
