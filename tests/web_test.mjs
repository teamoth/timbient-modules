// The web player as built (timbient.js + timbient.wasm), run in Node: open the test session (a sample packed inside it),
// play it, draw it; and refuse a file that isn't a session. Also how fast it works: how much of real time a second of
// sound takes. Run by CI after the web build:  node tests/web_test.mjs build-web/timbient.js tests/fixtures/web_test.tmbnt
import { createRequire } from 'module';
import { readFileSync } from 'fs';
import { resolve } from 'path';
const require = createRequire(import.meta.url);
const TimbientModule = require(resolve(process.argv[2]));
const file = readFileSync(process.argv[3]);
const M = await TimbientModule();
let fails = 0;
const give = (bytes) => { const p = M._tb_alloc(bytes.length); M.HEAPU8.set(bytes, p); return p; };
const str = (s) => { const n = M.lengthBytesUTF8(s) + 1, p = M._malloc(n); M.stringToUTF8(s, p, n); return p; };

M._tb_init(48000);
const ok = M._tb_load(give(file), file.length, str('web_test.tmbnt')); const note = M.UTF8ToString(M._tb_note());
let s2 = 0, bad = 0, peak = 0; const N = 4096, blocks = Math.ceil(4 * 48000 / N); const t0 = performance.now();
for (let b = 0; b < blocks; b++) { const p = M._tb_render(N) >> 2; const h = M.HEAPF32; for (let i = 0; i < 2 * N; i++) { const x = h[p + i]; if (!Number.isFinite(x)) bad++; else { s2 += x * x; peak = Math.max(peak, Math.abs(x)); } } }
const secs = (performance.now() - t0) / 1000, db = 10 * Math.log10(s2 / (blocks * N * 2) + 1e-20);
const px = M._tb_frame(240, 160, 3, 0.016) >>> 0, W = M._tb_imgW(), H = M._tb_imgH(); const seen = new Set();
for (let i = 0; i < W * H; i += 37) { seen.add(M.HEAPU32[(px >> 2) + i]); if (seen.size > 64) break; }
const opaque = (M.HEAPU32[(px >> 2)] >>> 24) === 255;
const good = ok === 1 && note === '' && bad === 0 && db > -45 && peak < 1.5 && W === 720 && H === 480 && seen.size > 4 && opaque;
if (!good) fails++;
console.log(`web player: opened ${ok} (note "${note}"), ${db.toFixed(1)} dB, peak ${peak.toFixed(2)}, ${bad} bad; 4 s of sound took ${secs.toFixed(2)} s (${(secs / 4 * 100).toFixed(0)}% of real time); picture ${W}x${H}, ${seen.size} colours, solid ${opaque}${good ? '' : '  WRONG'}`);
const no = M._tb_load(give(Buffer.from('{"hello":1}')), 11, str('x.json')); const why = M.UTF8ToString(M._tb_note());
const refuses = no === 0 && why.includes('NOT A TIMBIENT'); if (!refuses) fails++;
console.log(`a file that isn't a session: refused ${refuses} ("${why}")${refuses ? '' : '  WRONG'}`);
process.exit(fails ? 1 : 0);
