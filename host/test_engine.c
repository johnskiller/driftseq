/* test_engine.c — golden-hash + behavioral tests for the DriftSeq engine (01 §7)
 * plain asserts, cc -O2 -Wall; exit 0 = PASS
 */
#include "driftseq.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static uint64_t fnv1a(const uint8_t *p, uint32_t n) {
    uint64_t h = 0xCBF29CE484222325ull;
    while (n--) { h ^= *p++; h *= 0x100000001B3ull; }
    return h;
}

#define FAIL(...) do { printf("FAIL: " __VA_ARGS__); printf("\n"); exit(1); } while (0)

/* walk the raw stream properly: PC is 2 bytes, note/CC events are 3 */
static int count_noteon(const ds_state *s, uint8_t note) {
    uint32_t i = 0;
    int n = 0;
    while (i < s->out_len) {
        uint8_t b = s->out[i];
        if (b < 0x80) { i++; continue; }             /* stray data byte (shouldn't happen) */
        if ((b & 0xF0) == 0xC0 || (b & 0xF0) == 0xD0) { i += 2; continue; }
        if (i + 2 < s->out_len && (b & 0xF0) == 0x90 && s->out[i + 1] == note && s->out[i + 2] > 0) n++;
        i += 3;
    }
    return n;
}
/* stream hash over a run (test only; engine remains side-effect-free) */
static uint64_t run_hash(ds_state *s, double seconds) {
    uint64_t h = 0xCBF29CE484222325ull;
    double dt = 64.0 / 44100.0;                    /* one audio block */
    double t;
    for (t = 0; t < seconds; t += dt) {
        ds_tick(s, dt);
        h = fnv1a(s->out, s->out_len) ^ (h * 0x100000001B3ull + 1);
        h ^= fnv1a(s->out, s->out_len);
    }
    return h;
}

static void test_determinism(void) {
    ds_state a, b;
    ds_init(&a, 42); ds_init(&b, 42);
    if (run_hash(&a, 4.0) != run_hash(&b, 4.0)) FAIL("determinism: same seed diverged");
    ds_init(&a, 42); ds_init(&b, 43);
    /* different seed may or may not differ in silence, but must not crash */
    run_hash(&b, 0.5);
    printf("ok determinism\n");
}

static void test_basic_fire(void) {
    ds_state s;
    int fired = 0;
    double dt = 64.0 / 44100.0, t;
    ds_init(&s, 7);
    ds_voice_add(&s.pat.track[0], 0, 60, 100, 1);   /* C on loop_start, fires AT t=0 */
    ds_play(&s);
    /* run 5.5 s: launch at t=0 + wraps at t=2,4 (2.0 s/cycle @120) */
    for (t = 0; t < 5.5; t += dt) {
        ds_tick(&s, dt);
        fired += count_noteon(&s, 60);
    }
    if (fired != 3) FAIL("basic fire: launch + 2 wraps in 5.5s, got %d", fired);
    printf("ok basic fire (launch + wraps @2.0s/cycle)\n");
}

static void test_window(void) {
    ds_state s;
    int hit_start = 0, hit_outside = 0;
    double dt = 64.0 / 44100.0, t;
    ds_init(&s, 7);
    s.pat.track[0].loop_start = 5;                   /* window [5, 21) */
    s.pat.track[0].loop_len = 16;
    ds_voice_add(&s.pat.track[0], 5, 60, 100, 1);    /* window start */
    ds_voice_add(&s.pat.track[0], 0, 62, 100, 1);    /* outside window */
    ds_play(&s);                                     /* playhead launches at 5 */
    for (t = 0; t < 5.5; t += dt) {                  /* launch + 2 wraps */
        ds_tick(&s, dt);
        hit_start += count_noteon(&s, 60);
        hit_outside += count_noteon(&s, 62);
    }
    if (hit_start != 3) FAIL("window: start-step should fire 3x (launch+2 wraps), got %d", hit_start);
    if (hit_outside != 0) FAIL("window: outside-window step fired %d times", hit_outside);
    printf("ok window [5,21): launches at start, never plays outside\n");
}

static void test_drift_lcm(void) {
    /* the product: track0 len 16 + track1 len 12 => re-align every LCM(16,12)=48 steps */
    ds_state s;
    int hit0 = 0, hit1 = 0, simu = 0, hits_same_tick = 0;
    double dt = 64.0 / 44100.0, t;
    ds_init(&s, 7);
    ds_voice_add(&s.pat.track[0], 0, 36, 100, 1);    /* len 16 */
    ds_voice_add(&s.pat.track[1], 0, 38, 100, 1);
    s.pat.track[1].loop_len = 12;                    /* len 12 */
    ds_play(&s);
    for (t = 0; t < 11.5; t += dt) {                 /* 11.5 s: no boundary at 12 */
        int now0 = 0, now1 = 0;
        ds_tick(&s, dt);
        now0 = count_noteon(&s, 36); now1 = count_noteon(&s, 38);
        hit0 += now0; hit1 += now1;
        if (now0 && now1) simu = hits_same_tick++;
    }
    if (hit0 != 6) FAIL("drift: trk0 (2.0s cycle) expect 6 hits in 11.5s, got %d", hit0);
    if (hit1 != 8) FAIL("drift: trk1 (1.5s cycle) expect 8 hits in 11.5s, got %d", hit1);
    if (hits_same_tick != 2) FAIL("drift: 16+12 re-align at t=0 and t=6 (LCM=48 16ths), got %d", hits_same_tick);
    if (simu == 0) FAIL("drift: no simultaneous hit found");
    printf("ok drift: 16 vs 12 re-align at LCM (2 co-hits in 11.5s)\n");
}

static void test_probability_seeded(void) {
    ds_state a, b;
    double dt = 64.0 / 44100.0, t;
    ds_init(&a, 99); ds_init(&b, 99);
    a.pat.track[0].loop_len = 64; b.pat.track[0].loop_len = 64;
    ds_voice_add(&a.pat.track[0], 0, 60, 100, 1);
    ds_voice_add(&b.pat.track[0], 0, 60, 100, 1);
    a.pat.track[0].steps[0].prob = 50; b.pat.track[0].steps[0].prob = 50;
    ds_play(&a); ds_play(&b);
    {
        uint64_t ha = 0, hb = 0;
        for (t = 0; t < 8.0; t += dt) {
            ds_tick(&a, dt); ha = ha * 131 + a.out_len;
            ds_tick(&b, dt); hb = hb * 131 + b.out_len;
        }
        if (ha != hb) FAIL("probability: same seed gave different streams");
    }
    printf("ok probability deterministic (seeded)\n");
}

static void test_euclid(void) {
    ds_state s;
    int i, on = 0;
    ds_init(&s, 1);
    s.pat.track[0].eu_on = 1; s.pat.track[0].eu_k = 3; s.pat.track[0].eu_n = 8;
    s.pat.track[0].loop_len = 8;
    ds_euclid_fill(&s.pat.track[0]);
    for (i = 0; i < 8; i++) on += s.pat.track[0].steps[i].on;
    if (on != 3) FAIL("euclid E(8,3): expected 3 pulses, got %d", on);
    /* classic: E(16,11) must spread without adjacent clumping beyond 2 */
    ds_init(&s, 1);
    s.pat.track[0].eu_on = 1; s.pat.track[0].eu_k = 11; s.pat.track[0].eu_n = 16;
    s.pat.track[0].loop_len = 16;
    ds_euclid_fill(&s.pat.track[0]);
    {
        int run = 0, maxrun = 0;
        for (i = 0; i < 16; i++) {
            if (s.pat.track[0].steps[i].on) { run++; if (run > maxrun) maxrun = run; }
            else run = 0;
        }
        if (maxrun > 3) FAIL("euclid E(16,11) clumps %d in a row (v1 bucket algo)", maxrun);
    }
    printf("ok euclid E(8,3)=3 pulses, E(16,11) spread\n");
}

static void test_toggle_silent_keep(void) {
    ds_state s;
    ds_init(&s, 1);
    ds_voice_add(&s.pat.track[0], 3, 60, 100, 1);
    if (!ds_step_active(&s.pat.track[0], 3)) FAIL("step should be active after add");
    s.pat.track[0].steps[3].on = 0;                  /* single-click off */
    if (ds_step_active(&s.pat.track[0], 3)) FAIL("step should be silent after toggle off");
    if (s.pat.track[0].steps[3].nvoices != 1) FAIL("silent-keep: voices must survive toggle");
    s.pat.track[0].steps[3].on = 1;                  /* revive */
    if (!ds_step_active(&s.pat.track[0], 3)) FAIL("step should revive");
    printf("ok silent-keep toggle\n");
}

static void test_voice_limit(void) {
    ds_state s;
    int i, r = 0;
    ds_init(&s, 1);
    for (i = 0; i < 4; i++) r |= ds_voice_add(&s.pat.track[0], 0, 60 + i, 100, 1);
    if (r != 0) FAIL("4 voices should fit");
    if (ds_voice_add(&s.pat.track[0], 0, 64, 100, 1) != -1) FAIL("5th voice must be rejected");
    printf("ok 4-voice cap\n");
}

int main(void) {
    test_determinism();
    test_basic_fire();
    test_window();
    test_probability_seeded();
    test_drift_lcm();
    test_euclid();
    test_toggle_silent_keep();
    test_voice_limit();
    printf("ALL ENGINE TESTS PASSED\n");
    return 0;
}
