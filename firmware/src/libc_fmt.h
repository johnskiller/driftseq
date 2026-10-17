/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* libc_fmt.h — wasm include of libc.c's number formatters only: the memset/
 * memcpy/memcmp/rng of libc.c would clash with real libc / duplicate across
 * TUs in the web build, so we rename-include the tail of libc.c from the
 * fmt_ section (libc.c line 38..) guarded by DS_LIBC_FMT_ONLY there. */
#define DS_LIBC_FMT_ONLY 1
#include "libc.c"
#undef DS_LIBC_FMT_ONLY
