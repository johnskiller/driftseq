// euclid canonical phase + rot + fill idempotence tests — appended semantics per 03 §4.2
#include "driftseq.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define FAIL(...) do { printf("FAIL: " __VA_ARGS__); printf("\n"); exit(1); } while (0)

static int run_one(const char *name, int n, int k, const char *want) {
    ds_state s;
    int i, ok = 1;
    ds_init(&s, 1);
    s.pat.track[0].eu_on = 1; s.pat.track[0].eu_k = (uint8_t)k; s.pat.track[0].eu_n = (uint8_t)n;
    s.pat.track[0].loop_len = (uint8_t)n;
    s.pat.track[0].loop_start = 0;
    ds_euclid_fill(&s.pat.track[0]);
    for (i = 0; i < n; i++)
        if (!!s.pat.track[0].steps[i].on != (want[i] == '1')) ok = 0;
    if (!ok) {
        printf("  got  : ");
        for (i = 0; i < n; i++) putchar(s.pat.track[0].steps[i].on ? '1' : '0');
        printf("\n  want : %s\n", want);
        FAIL("%s", name);
    }
    printf("ok %s\n", name);
    return 0;
}

static void test_rot(void) {
    ds_state a, b;
    int i;
    ds_init(&a, 1); ds_init(&b, 1);
    a.pat.track[0].eu_on = 1; a.pat.track[0].eu_k = 3; a.pat.track[0].eu_n = 8; a.pat.track[0].eu_rot = 0;
    b.pat.track[0].eu_on = 1; b.pat.track[0].eu_k = 3; b.pat.track[0].eu_n = 8; b.pat.track[0].eu_rot = 1;
    a.pat.track[0].loop_len = 8; b.pat.track[0].loop_len = 8;
    ds_euclid_fill(&a.pat.track[0]); ds_euclid_fill(&b.pat.track[0]);
    /* rot=1 shifts every pulse one step later: on(i) == on_rot1(i-1) */
    for (i = 1; i < 8; i++)
        if (a.pat.track[0].steps[i - 1].on != b.pat.track[0].steps[i].on)
            FAIL("rot: rot+1 must shift pulses +1 step (i=%d)", i);
    printf("ok rot+1 = pulses shift one step later\n");
}

static void test_idempotent(void) {
    ds_state s;
    int i;
    ds_init(&s, 1);
    s.pat.track[0].eu_on = 1; s.pat.track[0].eu_k = 5; s.pat.track[0].eu_n = 12;
    s.pat.track[0].loop_len = 12;
    ds_euclid_fill(&s.pat.track[0]);
    for (i = 0; i < 12; i++) s.pat.track[0].steps[i].prob = 77;   /* voices/prob untouched marker */
    ds_euclid_fill(&s.pat.track[0]);                              /* again */
    for (i = 0; i < 12; i++)
        if (s.pat.track[0].steps[i].prob != 77) FAIL("idempotent: prob clobbered at %d", i);
    printf("ok re-fill twice == once (only en bits written)\n");
}

int main(void) {
    run_one("E(8,3) = tresillo x..x..x.", 8, 3, "10010010");
    run_one("E(16,4) = four-on-floor x...x...", 16, 4, "1000100010001000");
    run_one("E(12,3) = every 4th", 12, 3, "100010001000");
    run_one("E(12,5) = Venda clap x..x.x..x.x.", 12, 5, "100101001010");
    run_one("E(16,5) = bossa necklace x..x..x..x..x....", 16, 5, "1001001001001000");
    run_one("E(16,7) = samba necklace x..x.x.x..x.x.x.", 16, 7, "1001010100101010");
    test_rot();
    test_idempotent();
    printf("EUCLID PAPER-PHASE TESTS PASSED\n");
    return 0;
}
