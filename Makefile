# DriftSeq — host tests + web/emu build
#
#   make test      — engine behavioral tests (plain C, no deps)
#   make golden    — golden MIDI-stream hash (canonical 5828982717F322ED)
#   make wasm      — build web/driftseq.js+wasm (needs emsdk; source emsdk_env first)
#   make wasmtest  — node parity check: wasm byte stream == host golden
#   make gh-pages  — refresh the gh-pages worktree from web/ (push manually)

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
EMCC    ?= emcc

HOST_SRC = host/driftseq.c host/demo_pattern.c

.PHONY: all test golden wasm wasmtest gh-pages clean

all: test golden

test:
	$(CC) $(CFLAGS) -o /tmp/ds_test host/test_engine.c $(HOST_SRC) -lm
	/tmp/ds_test

golden:
	$(CC) $(CFLAGS) -o /tmp/ds_golden host/test_golden.c $(HOST_SRC) -lm
	/tmp/ds_golden

wasm:
	$(EMCC) -O2 -Wall web/drift_web.c $(HOST_SRC) -Ihost \
	  -o web/driftseq.js -s MODULARIZE=1 -s EXPORT_NAME=DriftSeqModule \
	  -s ALLOW_MEMORY_GROWTH=0 \
	  -s EXPORTED_RUNTIME_METHODS='["getValue","setValue","HEAPU8"]' \
	  -s EXPORTED_FUNCTIONS='["_malloc","_free"]'

wasmtest: wasm
	node web/wasmtest.mjs

gh-pages:
	git worktree add -f /tmp/ds-pages gh-pages 2>/dev/null || git worktree add /tmp/ds-pages gh-pages
	cp web/index.html web/emu.js web/driftseq.js web/driftseq.wasm /tmp/ds-pages/
	cd /tmp/ds-pages && git add -A && git commit -m "emu refresh $$(git log -1 --format=%h main 2>/dev/null)" || true

clean:
	rm -f /tmp/ds_test /tmp/ds_golden
