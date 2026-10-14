# DriftSeq

16-track polymetric master MIDI sequencer for the M-VAVE FM-1.

**SQ-64's step workflow + QY100's 16 polyphonic tracks + built-in GM monitoring,
on one 240×240 device** — a pattern instrument, not a workstation. Per-track loop
lengths run simultaneously; phase drift is *seen* on the screen, not guessed by ear.
Euclid / per-step probability / conditional triggers generate; USB-MIDI out drives
the rig (MPC et al.); a looped-back Sonivox EAS GM engine plays drafts in headphones
when nothing is connected.

## Paper-first (the Polly pipeline: spec → host sim → web emu → firmware)

Specs in `docs/specs/` (2026-10-14 series):

| spec | content |
|---|---|
| 00-master | positioning, six design pillars, not-goals |
| 01-core-model | track/step/voice model, RAM+flash budget (pattern = 14.3 KB), conditions, chain |
| 02-input | real panel (14 buttons 2×6+OCT, 7 encoders, keys F3–G5), GRID toggle + STEP-ENTRY, real-time rec |
| 03-screens | 240×240 frame (K1–4 bottom boxes), HOME hero drift view, page routing |
| 04-gm-monitor | Sonivox EAS recipe (Polly-proven), loopback wiring, free MIDI-in module mode |
| 05-vocabulary | locked product language + banned-terms table |

Status: **web/emu is live** — spec phase closed, remaining input-detail decisions are
**play-gated** (test them in the emu or on device).

## Run it

**Browser emu** (`web/`): `python3 -m http.server 8931` in `web/`, open
`http://127.0.0.1:8931/` — 240×240 HOME hero drift screen + SEQ grid, WebMIDI out
(chrome/edge), WebAudio beeps otherwise. Sliders morph the selected track's loop
window live (PLAY: K3=START K4=LEN semantics). SPACE play/stop, 1–8 select, G toggles
HOME/SEQ, click grid cells to toggle steps.

**Host tests** (`host/` + `Makefile`): `make test` — determinism, window semantics,
probability, Euclid, drift re-alignment at LCM, silent-keep toggles, 4-voice cap.
`make golden` — the MIDI byte stream of the 20 s demo locks to hash
**`5828982717F322ED`** (demo pattern @ seed 42, 64-frame ticks).

**wasm parity** (`web/wasmtest.mjs`): `make wasmtest` proves the browser engine emits
a byte-identical stream to host C. The engine is the same C the firmware will use;
the emu is its first consumer.

```
driftseq.h/.c      pure-C deterministic engine (ds_tick -> MIDI bytes, seeded RNG)
demo_pattern.c     default demo (kick/snare/hats Euclid + bass + lead, drifts immediately)
drift_web.c        emscripten bridge (state offsets exported, JS reads volatiles)
emu.js             240×240 canvas (HOME hero + SEQ grid), WebMIDI/WebAudio
```

Next: FM-1 firmware port (keyscan/LED/USB layers from the ecosystem), then EAS GM
monitor on-device.

## License

GPL-3.0-or-later (FM-1 ecosystem convention). GM engine: Sonivox EAS, Apache-2.0
(see 04).
