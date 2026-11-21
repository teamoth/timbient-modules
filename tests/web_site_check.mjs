// The web player's site, as CI puts it together: can a phone install it? The page names its app description; that
// names an icon of 192 and of 512 that exist, opens full screen, and starts here; every file the offline helper keeps exists.
//   node tests/web_site_check.mjs site
import { readFileSync, existsSync } from 'fs';
import { join } from 'path';
const dir = process.argv[2] || 'site'; let fails = 0; const out = [];
const chk = (ok, what) => { out.push(`${what}: ${ok ? 'ok' : 'MISSING  WRONG'}`); if (!ok) fails++; };
const page = readFileSync(join(dir, 'index.html'), 'utf8');
chk(page.includes('rel="manifest" href="manifest.webmanifest"'), 'page names its app description');
let m = {}; try { m = JSON.parse(readFileSync(join(dir, 'manifest.webmanifest'), 'utf8')); } catch (e) { chk(false, 'app description reads'); }
chk(!!m.name && !!m.short_name && m.start_url === './' && m.display === 'fullscreen', 'name, start, full screen');
for (const size of ['192x192', '512x512']) chk((m.icons || []).some((i) => i.sizes === size && existsSync(join(dir, i.src))), `icon ${size}`);
const sw = readFileSync(join(dir, 'sw.js'), 'utf8'); const kept = JSON.parse(sw.match(/FILES = (\[[^\]]*\])/)[1].replace(/'/g, '"'));
chk(kept.every((f) => f === './' || existsSync(join(dir, f))), `offline helper's files (${kept.length})`);
chk(sw.includes("addEventListener('fetch'"), 'offline helper answers requests');
console.log(out.join('\n')); process.exit(fails ? 1 : 0);
