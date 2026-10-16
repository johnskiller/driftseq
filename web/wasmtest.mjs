/* wasmtest.mjs — golden-hash parity: browser-class wasm engine must produce the
 * exact same MIDI byte stream as host C (host/test_golden.c). Canonical 20 s demo
 * hash: 5828982717F322ED. Run: node web/wasmtest.mjs
 */
import { createRequire } from 'module';
import { fileURLToPath } from 'url';
import path from 'path';
const require = createRequire(import.meta.url);
const DriftSeqModule = require(path.join(path.dirname(fileURLToPath(import.meta.url)), 'driftseq.js'));

const GOLDEN = 'F907E068E55AA89E';
const M = await DriftSeqModule();
const S = M._ds_new(42n) | 0;
const H8 = M.HEAPU8, dv = new DataView(H8.buffer);
const outLen = M._ds_offset_out_len(), out = M._ds_offset_out();

const MASK = (1n << 64n) - 1n, BASIS = 0xCBF29CE484222325n, PRIME = 0x100000001B3n;
function fnv1aChunk(bytes) {
  let x = BASIS;
  for (const b of bytes) { x ^= BigInt(b); x = (x * PRIME) & MASK; }
  return x;
}

M._ds_play_wasm();
const dt = 64 / 44100;
let h = BASIS, noteOns = 0;
for (let i = 0, N = Math.round(20 / dt); i < N; i++) {
  M._ds_tick_wasm(dt);
  const n = dv.getInt32(S + outLen, true);
  if (n > 0) {
    const bytes = H8.slice(S + out, S + out + n);
    h = fnv1aChunk(bytes) ^ ((h * PRIME + 1n) & MASK);
    for (let j = 0; j < n;) {
      const b = bytes[j];
      if (b < 0x80) { j++; continue; }
      const t = b & 0xF0;
      if (t === 0xC0 || t === 0xD0) { j += 2; continue; }
      if (t === 0x90 && bytes[j + 2] > 0) noteOns++;
      j += 3;
    }
  }
}
const hex = h.toString(16).toUpperCase().padStart(16, '0');
console.log(`wasm demo 20s: note_ons=${noteOns} hash=${hex}`);
if (hex !== GOLDEN || noteOns < 200) {
  console.error(`WASM-GOLDEN-MISMATCH (want ${GOLDEN})`);
  process.exit(1);
}
console.log('WASM-GOLDEN-MATCH');
