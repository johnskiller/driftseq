/* Proof that ds_tick advances by the delta it is GIVEN (rev.33).
 *
 * The bug being fixed was in the caller, but the fix is only worth anything if
 * ds_tick() actually integrates real deltas: a hardcoded 0.001 in the caller
 * would still run slow, and equally a ds_tick() that ignored dt would make the
 * new caller pointless. So assert on the accumulated musical position.
 *
 * Reference: x0x seq_advance() moves every track by `dq = k << 8` frames, i.e.
 * by the real elapsed frames. Position after T seconds must be bpm/60*T
 * quarters, no matter HOW the time was split into calls.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "driftseq.h"

static int fails;
static void ck(int cond, const char *msg, const char *detail)
{
    printf("%s  %s%s%s\n", cond ? "ok  " : "FAIL", msg,
           detail && *detail ? "  [" : "", detail && *detail ? detail : "");
    if (detail && *detail)
        printf("]\n");
    if (!cond)
        fails++;
}

/* run `total` seconds in n equal slices; return tick_acc */
static double run_sliced(ds_state *s, double total, int n)
{
    int i;
    for (i = 0; i < n; i++)
        ds_tick(s, total / (double)n);
    return s->tick_acc;
}

int main(void)
{
    static ds_state a, b, c;
    const double T = 4.0;                    /* 4 s */

    /* one slice of 4 s, vs 4000 slices of 1 ms, vs 137 uneven-but-summing slices */
    ds_init(&a, 1);
    a.playing = 1;
    ds_init(&b, 1);
    b.playing = 1;
    ds_init(&c, 1);
    c.playing = 1;

    ds_tick(&a, T);
    run_sliced(&b, T, 4000);
    {
        /* an uneven train that still sums to exactly T: the shape of a real
         * main loop, where one iteration may take 8 ms and the next 0.2 ms */
        static const double wobble[] = {0.008, 0.0002, 0.0031, 0.0009, 0.012};
        double sum = 0.0;
        int i = 0;
        while (sum < T - 1e-9) {
            double d = wobble[i++ % 5];
            if (sum + d > T)
                d = T - sum;
            ds_tick(&c, d);
            sum += d;
        }
    }

    char buf[160];
    snprintf(buf, sizeof buf, "1 slice=%.6f  4000 slices=%.6f  137 uneven=%.6f",
             a.tick_acc, b.tick_acc, c.tick_acc);
    printf("tick_acc: %s\n", buf);

    /* bpm 120 -> 2 quarters/s -> 24-tick units: 4 s = 8 quarters = 192 ticks */
    ck(fabs(a.tick_acc - 192.0) < 1e-6, "ds_tick integrates the delta it is given",
       buf);
    ck(fabs(b.tick_acc - a.tick_acc) < 1e-6, "slicing into 1 ms pieces changes nothing",
       "");
    ck(fabs(c.tick_acc - a.tick_acc) < 1e-6, "an uneven slice train sums to the same position",
       "");

    /* the old bug, reproduced: 4000 calls of a FIXED 0.001 s against 4 s of
     * reality gives exactly the right answer only because the loop happened to
     * run at 1 kHz. Feed 500 calls instead (a loop that takes 8 ms) and the
     * position collapses to 1/8 -- that is the drift, quantified. */
    {
        static ds_state d;
        ds_init(&d, 1);
        d.playing = 1;
        {   int i; for (i = 0; i < 500; i++) ds_tick(&d, 0.001); }  /* 4 s of wall clock */
        snprintf(buf, sizeof buf, "500 calls x 0.001 s = %.1f ticks, want 192.0", d.tick_acc);
        ck(fabs(d.tick_acc - 24.0) < 1e-6,
           "an 8 ms loop with a hardcoded 1 ms step runs 8x slow (the old bug)", buf);
    }

    printf("\n== %s ==\n", fails ? "CLOCK CHECKS FAILED" : "CLOCK OK");
    return fails ? 1 : 0;
}
