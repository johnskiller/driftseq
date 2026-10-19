# DriftSeq FM-1 firmware build — JieLi pi32v2 toolchain.
#
# Tiered on purpose, because the full on-device image needs the AC79 SDK to
# package (uboot.boot + lz4 blocks), which is a separate checkout:
#
#   python3 tools/build.py            compile + link build/driftseq.elf
#                                     FAILS LOUDLY if the image does not link.
#   python3 tools/build.py --check    also verify the entry stub, section sizes
#                                     and that .ram_text has no calls
#                                     (fm1-ctrl's proven checks)
#
# Why the flags look like this (all learned the hard way, keep them):
#  - FPU: -mcpu=r3 -mfprev1 -ffp-contract=off. The engine is float heavy
#    (cell_seconds, ds_tick); without the FPU flags soft-float falls back to
#    __divdf3/__adddf3, which have no implementation in this freestanding
#    build and fail at LINK time, not compile time.
#  - HAL asm: crt0.S + fm1_vec.S + fm1_isr.S + fm1_cpu1.S must be compiled and
#    linked separately (they define _start / vectors / _ustack_top / _sstack_top);
#    passing crt0.S to clang in one invocation does not produce those symbols.
#  - No -isystem <toolchain>/include: it pulls in newlib's string.h, which
#    collides with firmware/src/libc.c (memcpy/memset/memcmp).
import os, subprocess, sys
from pathlib import Path

TC = Path(os.environ.get('JIELI_TOOLCHAIN', '/home/john/.jieli/toolchain'))
ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / 'firmware'
OUT = ROOT / 'build'
OUT.mkdir(exist_ok=True)

CFLAGS = ['-Os', '-ffunction-sections', '-fno-builtin', '-Wall', '-Wno-unused-function']
FPU = ['-mcpu=r3', '-mfprev1', '-ffp-contract=off']
INC = ['-Ifirmware/hal', '-Ifirmware/src', '-Ifirmware']
# RAM budget: ds_state carries the 323 KB pattern bank by default (host/web);
# the FM-1 has 96 KiB of RAM, so the firmware state drops it.
# DRIFTSEQ_FW: host/driftseq.c needs it to skip <string.h> (no libc headers on
# pi32v2; the unity root's own libc.c provides memset before the engine).
DEFS = ['-DFELUCCA_UAC=0', '-DFELUCCA_CDC=0', '-DDS_PATTERN_BANK_IN_STATE=0',
        '-DDRIFTSEQ_FW=1']
# soft-float/double runtime: the engine is double based (cell_seconds, ds_tick)
# and the pi32v2 FPU only covers single precision.
LIBS = [str(TC / 'pi32v2' / 'lib' / 'r3' / 'libcompiler_rt.a')]


def tc(tool, *args, check=True):
    cmd = [str(TC / 'pi32v2' / 'bin' / tool), *args]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f'{tool} failed')
    return r


def main():
    flags = ['-target', 'pi32v2', *CFLAGS, *FPU, *INC, *DEFS]
    # the .S units need -target too: clang's integrated assembler otherwise
    # defaults to the host triple and rejects pi32v2 mnemonics
    units = [
        ('clang', '-target', 'pi32v2', '-c', FW / 'crt0.S', '-o', OUT / 'crt0.o'),
        ('clang', '-target', 'pi32v2', '-c', FW / 'hal' / 'fm1_vec.S', '-o', OUT / 'fm1_vec.o'),
        ('clang', '-target', 'pi32v2', '-c', FW / 'hal' / 'fm1_isr.S', '-o', OUT / 'fm1_isr.o'),
        ('clang', '-target', 'pi32v2', '-c', FW / 'hal' / 'fm1_cpu1.S', '-o', OUT / 'fm1_cpu1.o'),
        ('clang', *flags, '-c', FW / 'src' / 'app.c', '-o', OUT / 'app.o'),
    ]
    for unit in units:
        r = tc(*[str(a) for a in unit], check=False)
        diag = [l for l in (r.stdout + r.stderr).splitlines() if 'error' in l or 'warning' in l]
        if r.returncode or diag:
            print('\n'.join(diag[:40]))
        if r.returncode:
            raise SystemExit(f'compile failed: {unit[2]}')

    objs = [OUT / f'{n}.o' for n in ('crt0', 'fm1_vec', 'fm1_isr', 'fm1_cpu1', 'app')]
    elf = OUT / 'driftseq.elf'
    tc('ld', '--gc-sections', '-e', '_start', '-T', str(FW / 'app.ld'),
       *[str(o) for o in objs], *LIBS, '-o', str(elf))
    print(f'ok  linked {elf.relative_to(ROOT)} ({elf.stat().st_size} B)')

    if '--check' in sys.argv:
        syms = tc('objdump', '-t', str(elf)).stdout
        rt = tc('objdump', '-d', '-j', '.ram_text', str(elf)).stdout
        binp = OUT / 'text.bin'
        tc('objcopy', '-O', 'binary', '-j', '.text', str(elf), str(binp))
        img = binp.read_bytes()
        errs = []
        # note: bare endswith('_start') also matches usb_start — match the
        # whole symbol name
        start = [l for l in syms.splitlines() if l.split() and l.split()[-1] == '_start']
        if not start:
            errs.append('_start not in symbol table')
        else:
            addr = int(start[0].split()[0], 16)
            if addr != 0x02000120:
                errs.append(f'_start is at {addr:#x}, not 0x02000120 (APP_XIP)')
        # the FM-1 boot stub: first insn must be the pi32v2 entry sequence
        if img[:4] != bytes.fromhex('04818000'):
            errs.append(f'image starts with {img[:4].hex()}, not the entry stub 04818000')
        calls = [l for l in rt.splitlines() if '\tcall' in l]
        if calls:
            errs.append(f'.ram_text has {len(calls)} calls (must run from RAM without XIP)')
        if errs:
            print('\n'.join('ERR ' + e for e in errs))
            raise SystemExit(1)
        print('ok  entry/ram_text checks pass')


if __name__ == '__main__':
    main()
