# DriftSeq — host tests + web/emu build
#
#   make test      — engine behavioral tests (plain C, no deps)
#   make golden    — golden MIDI-stream hash (canonical 420C4824C85B79CB)
#   make wasm      — build web/driftseq.js+wasm (engine bridge for wasmtest)
#   make uiwasm    — build web/ui.js+wasm (FULL UI painters; batch 2 webemu)
#   make wasmtest  — node parity check: wasm byte stream == host golden
#   make fwcheck   — host compile + behavior check of the FM-1 app layer
#   make fwbuild   — REAL pi32v2 firmware: compile+link+image checks
#   make uiprobe   — pixel assertions on the real painters (needs uiwasm)
#   make gh-pages  — refresh the gh-pages worktree from web/ (push manually)

CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=c11
EMCC    ?= emcc

HOST_SRC = host/driftseq.c host/demo_pattern.c

.PHONY: all test golden wasm uiwasm wasmtest fwcheck fwbuild check uiprobe gh-pages clean

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

# the UI module: app layer (painters/routing) + engine, ONE instance.
# Fonts come from firmware/gen_font_pregen.h (Barlow, same as the FM-1).
uiwasm:
	$(EMCC) -O2 -Wall -DDS_WEB_UI=1 web/ui_web.c $(HOST_SRC) \
	  -Ifirmware -Ifirmware/src -Ihost \
	  -o web/ui.js -s MODULARIZE=1 -s EXPORT_NAME=DriftUiModule \
	  -s ALLOW_MEMORY_GROWTH=0 \
	  -s EXPORTED_RUNTIME_METHODS='["HEAPU8"]' \
	  -s EXPORTED_FUNCTIONS='["_malloc","_free","_ds_ui_boot","_ds_ui_frame","_ds_web_btn","_ds_web_enc","_ds_web_key","_ds_web_fb","_ds_web_out","_ds_web_out_len","_ds_web_out_clear","_ds_web_lit_k","_ds_web_dim_k","_ds_web_lit_b","_ds_web_dim_b"]'

wasmtest: wasm
	node web/wasmtest.mjs

fwcheck:
	python3 tools/fw_check.py

# the real thing: pi32v2 cross build + entry/ram_text/image checks. Needs the
# JieLi toolchain (JIELI_TOOLCHAIN=...). This target exists because the fw
# build silently rotted while only fwcheck/uiprobe were run.
fwbuild:
	JIELI_TOOLCHAIN=$(JIELI_TOOLCHAIN) python3 tools/build.py --check

# everything that must be green before a push
check: test golden fwcheck fwbuild uiprobe

# pixel-level contracts the hostcheck cannot see (it stubs cv_rect out):
# meter spacing per zoom, the ARP strip sharing the hero axis, which page owns
# the zoom wheel. Needs the wasm build, hence uiwasm first.
uiprobe: uiwasm
	node tools/ui_probe.js

gh-pages:
	git worktree add -f /tmp/ds-pages gh-pages 2>/dev/null || git worktree add /tmp/ds-pages gh-pages
	cp web/index.html web/panel.js web/emu.js web/ui.js web/ui.wasm web/driftseq.js web/driftseq.wasm /tmp/ds-pages/ 2>/dev/null || true
	cd /tmp/ds-pages && git add -A && git commit -m "emu refresh $$(git log -1 --format=%h main 2>/dev/null)" || true

clean:
	rm -f /tmp/ds_test /tmp/ds_golden
