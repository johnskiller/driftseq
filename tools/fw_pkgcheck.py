"""Independent read-back of build/driftseq.fwsc.

Written from the device side (firmware/src/ota.c ota_ufw) and the packer
(tools/fm1pkg_make.py), NOT by reusing the packer's own output buffers, so it
can actually disagree with the build.

Three cipher details that are easy to get wrong (all three bit me):
  * ENC resets its key at every call, and both enc() and sfc() are applied per
    32-byte block, so a region is decrypted block-by-block, never as one stream.
  * the LDR1 loader header is 16 bytes (4sHHII), not 20.
  * JLFS entry dcrc covers the padded app, i.e. the full APP_SLOT, not the
    unpadded image.
"""
import struct
import sys
from pathlib import Path

R = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(R / 'tools'))
import fm1pkg_make as F

APP_XIP = 0x02000120
# Optional args so this can also be pointed at a KNOWN-GOOD package (fm1-ctrl's
# ctrl.fwsc) — a checker that only agrees with itself proves nothing:
#   fw_pkgcheck.py [pkg.fwsc] [app.bin] [identity]
pkg = Path(sys.argv[1] if len(sys.argv) > 1 else R / 'build' / 'driftseq.fwsc').read_bytes()
img = Path(sys.argv[2] if len(sys.argv) > 2 else R / 'build' / 'driftseq.bin').read_bytes()
PRODUCT = sys.argv[3] if len(sys.argv) > 3 else 'FM-1_91001'
fails = []


def ck(cond, label, detail=''):
    print(('ok   ' if cond else 'FAIL ') + label + (('  [' + detail + ']') if detail else ''))
    if not cond:
        fails.append(label)


def enc_blocks(buf, size, base=0, key=0xFFFF, stride=32):
    """decrypt `size` bytes in `stride`-sized blocks, key reset per block"""
    for i in range(0, size, stride):
        F.enc(buf, base + i, min(stride, size - i), key)


def jlfs(b):
    crc = struct.unpack_from('<H', b, 0)[0]
    body = bytes(b[2:32])
    dcrc, off, size, flags, resvd, index = struct.unpack_from('<HIIBBH', body, 0)
    return dict(crc_ok=F.crc16(body) == crc, dcrc=dcrc, off=off, size=size,
                flags=flags, resvd=resvd, index=index,
                name=bytes(body[14:30]).rstrip(b'\x00\xff').decode('latin-1'))


def ufw_entry(b):
    typ, idx, crc, rsvd, off, ln, aln = struct.unpack_from('<HHHHIII', b, 0)
    return dict(type=typ, crc=crc, off=off, len=ln,
                name=bytes(b[0x40:0x50]).rstrip(b'\x00\xff').decode())


# ---------------------------------------------------------------- .fwsc identity
marks = ''
for i in range(20):
    m = pkg[i * 0x30 + 0x2F]
    if m == 0x7D:
        break
    marks += chr((m - i - 1) & 0xFF)
ck(marks == PRODUCT, 'identity marker = PRODUCT (device answers this on 0x11)',
   repr(marks) + ' vs ' + PRODUCT)

# ---------------------------------------------------------------- UFW layer
raw = bytearray()
for i in range(20):
    raw += pkg[i * 0x30:(i * 0x30) + 0x2F]
raw += pkg[20 * 0x30:]
# the UFW header is ONE continuous ENC pass over 0x40 bytes — the key does NOT
# reset per 32-byte block here, unlike the JLFS head and the SFC app area.
# (firmware/src/ota.c ota_ufw does exactly this: ota_jl_enc(hdr, 0x40).)
hdr = bytearray(raw[0:0x40])
F.enc(hdr, 0, 0x40)
ck(F.crc16(bytes(hdr[2:0x40])) == struct.unpack_from('<H', hdr, 0)[0], 'UFW header crc')
total, nent = struct.unpack_from('<IH', hdr, 4)
ck(total == len(raw), 'UFW total length', f'{total} vs {len(raw)}')
lst = bytearray(raw[0x40:0x40 + nent * 0x50])
ck(F.crc16(bytes(lst)) == struct.unpack_from('<H', hdr, 2)[0], 'UFW entry list crc')
lst_d = bytearray(lst)
enc_blocks(lst_d, len(lst_d), stride=0x50)

files = {}
for i in range(nent):
    e = ufw_entry(lst_d[i * 0x50:(i + 1) * 0x50])
    body = raw[e['off']:e['off'] + e['len']]
    files[e['name']] = body
    ck(F.crc16(body) == e['crc'], f"UFW entry {e['name']} crc16", f"{e['len']} B")
ck(set(files) == {'flash.bin', 'ota.bin'}, 'UFW carries flash.bin + ota.bin',
   ','.join(sorted(files)))

# ---------------------------------------------------------------- flash.bin head
flash = files['flash.bin']
HEAD = 2 + len(F.FLASH_HDR) + 32 * 4
head = bytearray(flash[0:HEAD])
enc_blocks(head, HEAD)
ck(struct.unpack_from('<H', head, 0)[0] == F.crc16(F.FLASH_HDR),
   'flash head crc covers FLASH_HDR (not the entry list)')
top = [jlfs(head[2 + len(F.FLASH_HDR) + k * 32:2 + len(F.FLASH_HDR) + (k + 1) * 32])
       for k in range(4)]
ck(top[0]['name'] == 'uboot.boot', 'head entry 0 = uboot.boot', top[0]['name'])
ck(top[1]['name'] == 'isd_config.ini', 'head entry 1 = isd_config.ini', top[1]['name'])
ck(top[3]['name'] == 'key_mac', 'head entry 3 = key_mac', top[3]['name'])
ck(all(e['crc_ok'] for e in top), 'all 4 head entries have valid own-crc')

# ---------------------------------------------------------------- app area
area = bytearray(flash[0x4000:0x4000 + 0x120 + F.APP_SLOT])
F.sfc(area, 0, len(area), 0, F.KEY)          # sfc walks 32-byte blocks internally
app_slot = bytes(area[0x120:0x120 + F.APP_SLOT])
ck(app_slot[:len(img)] == img, 'app.bin inside flash.bin == build/driftseq.bin',
   f'{len(img)} B')
ck(img[:4] == bytes.fromhex('04818000'), 'image entry stub 04818000')
ck(app_slot[len(img):] == b'\xff' * (F.APP_SLOT - len(img)), 'rest of the app slot is 0xFF')

a = jlfs(area[0:32])
ck(a['crc_ok'], 'app_area_head own-crc')
ck(a['off'] == APP_XIP, 'app_area_head targets APP_XIP', hex(a['off']))
s = jlfs(area[32:64])
ck(s['name'] == 'app.bin', 'first inner entry = app.bin', s['name'])
ck(s['flags'] == 0x82, 'app.bin flags 0x82 (encrypted, SFC)', hex(s['flags']))
ck(s['off'] == 0x120, 'app.bin at area offset 0x120', hex(s['off']))
ck(s['size'] == F.APP_SLOT, 'app.bin declared size = APP_SLOT', str(s['size']))
ck(s['dcrc'] == F.crc16(app_slot), 'app.bin dcrc == crc16(the whole padded slot)')

# ---------------------------------------------------------------- loader
ota = files['ota.bin']
magic, ver, crc, load, ln = struct.unpack_from('<4sHHII', ota, 0)
ck(magic == b'LDR1', 'loader magic LDR1', repr(magic))
ck(load == 0x01C0A800, 'loader load address', hex(load))
i, nblk, dec = 16, 0, 0          # LDR1 header = 4sHHII = 16 B
while i + 6 <= len(ota):
    tag, cl, ul = struct.unpack_from('<HHH', ota, i)
    if tag == 0 and cl == 0 and ul == 0:
        break
    dec += ul
    nblk += 1
    i += 6 + cl
ck(dec == ln, 'lz4 blocks decompress to the declared length', f'{nblk} blocks, {dec} B vs {ln}')
ck(i + 4 == len(ota), 'loader stream ends exactly at the 0,0 terminator',
   f'terminator at {i}, blob {len(ota)} B')
ck(ota[i:i + 4] == b'\x00\x00\x00\x00', 'terminator is two zero u16s', ota[i:i + 4].hex())

print()
print('== ' + (f'{len(fails)} PACKAGE CHECKS FAILED' if fails else 'PACKAGE OK') + ' ==')
sys.exit(1 if fails else 0)
