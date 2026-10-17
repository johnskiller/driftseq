# fm1-fw.md — DriftSeq FM-1 firmware tree (v1 batch 1, 2026-10-17)

## Layout
```
firmware/
  src/            app layer (ours, GPL-3.0)
    app.c         unity root: platform -> engine -> app glue, ONE TU
    app_main.c    boot + main loop (fm1-ctrl ctrl_main.c skeleton)
    ui_glue.c     forward decls (included FIRST — unity order independence)
    ui_tables.c   DPAL/RGB565/GM drums/key tables
    ui_text.c     txt_* builders (no printf in Felucca; fmt_int only)
    ui.c          UI state + screen dispatch + key entry + panel roles
    ui_input.c    fm1_input edges -> ui_encoder/ui_button/panel_key
    ui_screens.c  HOME hero / melodic grid / drum grid / footer
    ui_edit.c     EDIT panel + drum readout + ARP ring + enc routing
    (lcd.c gfx.c libc.c usb.c ota.c panel.c plat_ctrl.h = fm1-ctrl platform copy)
  tools/build.py  JieLi pi32v2 wrapper (pattern: fm1-ctrl tools/build.py)
tools/fw_check.py + tools/fw_hostcheck.c
                  host compile + 21 behavior checks (NO toolchain needed)
```

## Unity order (app.c) — melodee lesson: platform FIRST, app glue LAST
1. platform includes + `#include "platform/..."` (fm1-ctrl copies)
2. `host/driftseq.c` + `host/demo_pattern.c` (engine, UNCHANGED)
3. `ui_glue.c` `ui_tables.c` `ui_text.c` `ui.c` `ui_input.c`
   `ui_screens.c` `ui_edit.c` `app_main.c`

## Build
- Host check (fast, no toolchain): `python3 tools/fw_check.py`
  → 21 checks: boot mode, drum/grid routing, black-key lane mapping,
  white-key toggle (idx14 → note67 G4 → white idx 8), cursor dead-stops,
  GLO+K1 x16, drum EDIT vel/gate/prob, ARP FREE meta-only (r16j semantics),
  pattern bits untouched while FREE, K4 re-link, note-on 0x99 in stream.
- Real build: `JIELI_TOOLCHAIN=... python3 tools/build.py` (not yet run —
  batch 2 wires the .elf→.fwsc step; fm1-ctrl's build.py is the template).

## r16j semantics frozen in C (must not drift from emu)
- FREE: K1/K2/K3 = meta-only (k/n/rot, clamped); K4 right re-links.
- cursor dead-stop 0..63; GLO+K1 = ±16; OCT± = ±12 key shift.
- Drum lane = black-key ordinal (11 lanes; GM_DRUMS trimmed to 11).
- key ids 0..26 = notes 53..79 (F3..G5). BLACK = note%12 ∈ {1,3,6,8,10}
  at ids 1,3,5,8,10,13,15,17,20,22,25. White idx = id − blacks-before.
