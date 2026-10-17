# DriftSeq FM-1 firmware — build with the JieLi pi32v2 toolchain (same as fm1-ctrl).
# Usage: JIELI_TOOLCHAIN=/home/john/.jieli/toolchain python3 tools/build.py
# fm1-ctrl's build.py (tools/) is the proven base — this file adapts the file list.
import os, subprocess, sys, glob

TC = os.environ.get('JIELI_TOOLCHAIN', '/home/john/.jieli/toolchain')
CC = os.path.join(TC, 'pi32v2', 'bin', 'clang')
LD = os.path.join(TC, 'pi32v2', 'bin', 'ld.lld')
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, 'firmware')
BUILD = os.path.join(ROOT, 'build')
os.makedirs(BUILD, exist_ok=True)

PLATFORM = ['src/usb.c', 'src/panel.c', 'src/ota.c', 'src/libc.c', 'src/lcd.c', 'src/gfx.c']
INPUT = ['keyscan.c', 'encoders.c', 'leds.c', 'midi_io.c', 'ctrl_glue.c']
ENGINE = ['host/driftseq.c', 'host/demo_pattern.c']
APP = ['src/ui.c', 'src/app.c']

# fm1-ctrl compiles one unity TU (ctrl.c includes everything) because the
# platform uses file-local statics. Match that: app.c is the unity root here —
# it #includes the rest in order. Build = single clang invocation.
# (See firmware/src/app.c header comment for the include order.)

cmd = [
    CC, '-target', 'pi32v2', '-O2', '-ffreestanding', '-fno-builtin',
    '-I', FW, '-I', os.path.join(FW, 'src'),
    '-DDRIFTSEQ_FW=1',
    os.path.join(FW, 'crt0.S'),
    os.path.join(FW, 'src', 'app.c'),      # unity root
    '-T', os.path.join(FW, 'app.ld'),
    '-nostdlib', '-Wl,--gc-sections',
    '-o', os.path.join(BUILD, 'driftseq.elf'),
]
print(' '.join(cmd))
r = subprocess.run(cmd, cwd=ROOT)
sys.exit(r.returncode)
