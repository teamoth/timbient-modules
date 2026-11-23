// The site's home page, as CI puts it together: every picture it shows is there, it links to the web player (which is there
// too), and each download button points at a file the release really carries (by name: the build names them).
//   node tests/web_home_check.mjs site
import { readFileSync, existsSync } from 'fs';
import { join } from 'path';
const dir = process.argv[2] || 'site'; let fails = 0; const out = [];
const chk = (ok, what) => { out.push(`${what}: ${ok ? 'ok' : 'MISSING  WRONG'}`); if (!ok) fails++; };
const page = readFileSync(join(dir, 'index.html'), 'utf8');
const local = [...page.matchAll(/(?:src|href)="(home\/[^"]+|CREDITS\.md)"/g)].map((m) => m[1]);
chk(local.length >= 4 && local.every((f) => existsSync(join(dir, f))), `its pictures and files (${local.length})`);
chk(page.includes('href="player/"') && existsSync(join(dir, 'player', 'index.html')), 'the web player link');
const made = ['Timbient-Modules-Mac.zip', 'Timbient-Modules-Windows.zip', 'Timbient-Modules-Windows-32bit.zip', 'Timbient-Modules-Linux.tar.gz'];
const wf = readFileSync(new URL('../.github/workflows/build.yml', import.meta.url), 'utf8');
for (const f of made) chk(page.includes('/releases/download/latest/' + f) && wf.includes(f), 'download: ' + f);
console.log(out.join('\n')); process.exit(fails ? 1 : 0);
