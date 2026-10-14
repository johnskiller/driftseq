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

Status: spec phase closed — remaining input-detail decisions are **play-gated**
(tested on device / on idea-1's web emu once it exists, not on paper).

Next: **host sim** (`host/`) — pure-C engine `drift_tick(state,in)->bytes[]`,
240×240 PNG rendering, `.ctl` scenarios, golden-hash assertions on BOTH the MIDI
byte stream and the screen bitmap.

## License

GPL-3.0-or-later (FM-1 ecosystem convention). GM engine: Sonivox EAS, Apache-2.0
(see 04).
