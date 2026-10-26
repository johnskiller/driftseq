/* SPDX-License-Identifier: Apache-2.0 */
/* <string.h> for Sonivox EAS on the FM-1 — drop-in replacement for the
 * GPL-3.0-only header that ships inside Polly/CTL-1's EAS tree
 * (firmware/src/dsp/eas/fm1inc/string.h).
 *
 * WHY THIS EXISTS AT ALL. The pi32v2 toolchain's clang cannot find a C library
 * header: `#include <string.h>` fails even though the toolchain's own
 * pi32v2/include/string.h is on disk — that copy belongs to the older pi32
 * compiler, and `clang -target pi32v2 -print-search-dirs` reports only
 * lib/clang/4.0.1. The EAS sources are third-party Apache-2.0 code
 * that must not be edited, so they need these declarations on the include path
 * (driftseq's build puts this directory on it).
 *
 * WHY IT IS APACHE-2.0 AND NOT GPL-3.0-ONLY. The header is compiled into every
 * image that contains the synth. Apache-2.0 combines freely with MPL-2.0 (the
 * fm1-base platform licence) and with the Apache-2.0 binary Bluetooth libraries
 * JieLi ships; GPL-3.0-only combines with neither, and would have blocked
 * shipping a distributable binary that contains both the synth and BLE.
 *
 * WHAT IS ACTUALLY NEEDED, measured against the EAS sources rather than assumed:
 *   strcmp  — eas_sndlibmgt.c, 3 calls, always against its own string literals
 *   memset  — eas_wtsynth.c, 3 calls (filter state, mix-buffer tail)
 *   memcpy  — no explicit call, but struct assignment and array copies can be
 *             lowered to it, so it is declared rather than left to chance
 *   memcmp  — no call site found in this EAS drop; declared for the same reason
 *
 * strcmp is defined here rather than merely declared because the link has no
 * libc to resolve it against. The body below is the one C requires — it is the
 * same two-line loop the GPL header carried, and there is no other way to spell
 * "compare as unsigned char, stop at the terminator". Rewriting it differently
 * to look like original work would be dishonest; re-licensing the file is what
 * the licence question actually turns on. memset/memcpy/memcmp are supplied by
 * the platform (fm1-base's libc, or driftseq's firmware/src/libc.c today), so
 * only their declarations belong here. */
#pragma once

void *memset(void *d, int c, unsigned n);
void *memcpy(void *d, const void *s, unsigned n);
int memcmp(const void *a, const void *b, unsigned n);

static inline int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return (unsigned char)*a - (unsigned char)*b;
}
