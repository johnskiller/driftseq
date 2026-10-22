# DriftSeq FM-1 firmware build — JieLi pi32v2 toolchain.
#
#   python3 tools/build.py              compile + link build/driftseq.elf
#   python3 tools/build.py --check      also verify the entry stub, section
#                                       layout, .ram_text and the RAM budget
#   python3 tools/build.py --package    also produce build/driftseq.fwsc
#                                       (needs AC79_SDK: uboot.boot + cfg_tool)
#   python3 tools/build.py --release 1.2   package identity FM-1_9010200
#
# --check is part of `make check`; --package is NOT, because it needs the AC79
# SDK checkout and produces a 600 KB artifact nobody wants in a test loop.
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
#
# IMAGE LAYOUT (the part that bites): the .fwsc carries ONE flat app.bin laid
# out in XIP address order, not just .text. .ram_text / .c1_text / .data are
# linked at RAM addresses but STORED in XIP, so each must be copied out of its
# section and inserted at its own _rt_load / _c1_load / _data_load offset. A
# package built from .text alone links fine, passes the entry-stub check, and
# then boots into a device with no .data and no RAM-resident flash driver.
import argparse
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

TC = Path(os.environ.get('JIELI_TOOLCHAIN', '/home/john/.jieli/toolchain'))
ROOT = Path(__file__).resolve().parent.parent
FW = ROOT / 'firmware'
OUT = ROOT / 'build'
LDR = OUT / 'loader'
OUT.mkdir(exist_ok=True)
sys.path.insert(0, str(ROOT / 'tools'))

APP_XIP = 0x02000120
# RAM address the update loader is staged at (fm1-ctrl's LOADER_LOAD)
LOADER_LOAD = 0x01C0A800

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

# The package identity. MUST equal FELUCCA_ID in firmware/src/app.c: the device
# answers a 0x11 handshake with FELUCCA_ID and the host tool compares it against
# the .fwsc marker string before it will send anything.
PRODUCT = 'FM-1_91001'


def tc(tool, *args, check=True):
    cmd = [str(TC / 'pi32v2' / 'bin' / tool), *args]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if check and r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f'{tool} failed')
    return r


def symtab(elf):
    """symbol -> address, from objdump -t"""
    out = tc('objdump', '-t', str(elf)).stdout
    syms = {}
    for ln in out.splitlines():
        p = ln.split()
        if len(p) >= 2 and re.fullmatch(r'[0-9a-fA-F]+', p[0]):
            syms[p[-1]] = int(p[0], 16)
    return syms


def section_blob(elf, name, out):
    """raw bytes of one section, empty when the section does not exist"""
    out.unlink(missing_ok=True)
    r = tc('objcopy', '-O', 'binary', '-j', name, str(elf), str(out), check=False)
    if r.returncode or not out.exists():
        return b''
    return out.read_bytes()


def build_loader():
    """the update loader, same source and wrapper as fm1-ctrl (x0x's ldr_wrap)"""
    import lz4blk
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / 'loader'
    flags = [*CFLAGS, *FPU, '-Ifirmware/hal', '-Ifirmware/src']
    for unit in (('clang', '-target', 'pi32v2', '-c', src / 'crt0_ldr.S', '-o', LDR / 'crt0_ldr.o'),
                 ('clang', '-target', 'pi32v2', *flags, '-c', src / 'loader.c', '-o', LDR / 'loader.o')):
        tc(*[str(a) for a in unit])
    elf = LDR / 'loader.elf'
    tc('ld', '--gc-sections', '-e', '_start', '-T', str(src / 'loader.ld'),
       str(LDR / 'crt0_ldr.o'), str(LDR / 'loader.o'), '-o', str(elf))
    image = section_blob(elf, '.text', LDR / 'loader.bin')
    if not image or len(image) > 0x14000:
        raise SystemExit('loader: image empty or too big')
    # LDR1 header + lz4 blocks: crc16, load address, length
    crc = 0
    for b in image:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    out = bytearray(struct.pack('<4sHHII', b'LDR1', 1, crc, LOADER_LOAD, len(image)))
    for i in range(0, len(image), 4096):
        blk = image[i:i + 4096]
        comp = lz4blk.compress(blk)
        if len(comp) >= 4096:
            raise SystemExit('loader: block does not compress below 4096 B')
        out += struct.pack('<HHH', 0x4C42, len(comp), len(blk)) + comp
    out += struct.pack('<HH', 0, 0)
    (LDR / 'ota.bin').write_bytes(bytes(out))
    return bytes(out)


def build_image(elf):
    """flat app.bin in XIP order: .text then the RAM-stored sections at their
    load offsets. This is what the .fwsc carries and what the SPL copies."""
    syms = symtab(elf)
    img = bytearray(section_blob(elf, '.text', OUT / 'text.bin'))
    if not img:
        raise SystemExit('image: .text is empty')
    tails = (('.ram_text', '_rt_load'), ('.c1_text', '_c1_load'), ('.data', '_data_load'))
    for sect, lname in tails:
        if lname not in syms:
            raise SystemExit(f'image: {lname} missing (linker script changed?)')
        load = syms[lname]
        if load % 4:
            raise SystemExit(f'image: {lname} {load:#x} is not word aligned')
        blob = section_blob(elf, sect, OUT / (sect.strip('.') + '.bin'))
        if not blob:
            continue
        if load < APP_XIP:
            raise SystemExit(f'image: {lname} {load:#x} is below APP_XIP')
        if load - APP_XIP < len(img):
            raise SystemExit(f'image: {sect} overlaps the image ({lname}={load:#x})')
        img += b'\xff' * (load - APP_XIP - len(img))
        img += blob
    img += b'\xff' * (-len(img) % 4)
    (OUT / 'driftseq.bin').write_bytes(bytes(img))
    return bytes(img), syms


def check(img, syms, rt):
    from fm1pkg_make import APP_SLOT
    errs, notes = [], []
    if syms.get('_start') != APP_XIP:
        errs.append(f"_start is at {syms.get('_start', -1):#x}, not {APP_XIP:#x} (APP_XIP)")
    if img[:4] != bytes.fromhex('04818000'):
        errs.append(f'image starts with {img[:4].hex()}, not the entry stub 04818000')
    calls = [l for l in rt.splitlines() if '\tcall' in l]
    if calls:
        errs.append(f'.ram_text has {len(calls)} calls (must run from RAM without XIP)')
    if len(img) > APP_SLOT:
        errs.append(f'image {len(img)} B exceeds the app slot {APP_SLOT} B')
    # RAM budget: .bss lives in the 96 KiB RAM region, .pool in the 344 KiB POOL
    # region (firmware/app.ld). Checking only .bss is what the old --check did.
    bss = syms.get('_bss_end', 0) - 0x01C08000
    pool = syms.get('_pool_end', 0) - syms.get('_pool_start', 0)
    notes.append(f'image {len(img)} B of {APP_SLOT}; RAM .data+.bss {bss} B of 98304; '
                 f'pool {pool} B of {0x54000}')
    if bss > 96 * 1024:
        errs.append(f'RAM region overflow: .data+.bss {bss} B > 98304 B')
    if pool > 0x54000:
        errs.append(f'POOL region overflow: {pool} B > {0x54000} B')
    # every section the SPL must copy has to be inside the packaged image
    for lname in ('_rt_load', '_c1_load', '_data_load'):
        if lname in syms and syms[lname] - APP_XIP > len(img):
            errs.append(f'{lname} {syms[lname]:#x} lies past the end of the image')
    return errs, notes


def package(img, product):
    import fm1pkg_make
    ota = build_loader()
    pkg = fm1pkg_make.ufw(fm1pkg_make.flash_image(img, fm1pkg_make.KEY), ota, product)
    path = OUT / 'driftseq.fwsc'
    path.write_bytes(pkg)
    return path, len(ota)


def main():
    global PRODUCT
    ap = argparse.ArgumentParser()
    ap.add_argument('--check', action='store_true', help='verify entry/layout/RAM')
    ap.add_argument('--package', action='store_true', help='produce build/driftseq.fwsc')
    ap.add_argument('--release', metavar='X.Y[.Z]', help='package identity FM-1_9XXYYZZ')
    a = ap.parse_args()
    if a.release:
        m = re.fullmatch(r'(\d{1,2})\.(\d{1,2})(?:\.(\d{1,2}))?', a.release)
        if not m:
            raise SystemExit('--release X.Y[.Z]')
        PRODUCT = 'FM-1_9%02d%02d%02d' % (int(m[1]), int(m[2]), int(m[3] or 0))

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
        r = tc(*[str(a2) for a2 in unit], check=False)
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

    img, syms = build_image(elf)
    print(f'ok  image {len(img)} B (flat, XIP order)')

    if a.check or a.package:
        rt = tc('objdump', '-d', '-j', '.ram_text', str(elf)).stdout
        errs, notes = check(img, syms, rt)
        for n in notes:
            print('ok  ' + n)
        if errs:
            print('\n'.join('ERR ' + e for e in errs))
            raise SystemExit(1)
        print('ok  entry/layout/RAM checks pass')

    if a.package:
        path, ota_len = package(img, PRODUCT)
        print(f'ok  loader  {LDR / "ota.bin"} ({ota_len} B)')
        print(f'ok  package {path.relative_to(ROOT)} ({path.stat().st_size} B, '
              f'identity {PRODUCT})')


if __name__ == '__main__':
    main()
