// The home page: the words from web/home.md, put into the layout in web/home-template.html.
//   node tools/build_home.mjs web/home.md web/home-template.html site/index.html
// home.md: a # title, the paragraph under it (the line beneath the title), then ## sections (a box each), with
// paragraphs, **bold**, *italic*, [links](address), `code`, and ``` blocks. <!-- notes --> are left out.
import { readFileSync, writeFileSync } from 'fs';

const [mdPath, tplPath, outPath] = process.argv.slice(2);
const md = readFileSync(mdPath, 'utf8').replace(/<!--[\s\S]*?-->/g, '').replace(/\r\n/g, '\n');
const tpl = readFileSync(tplPath, 'utf8');

const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;').replace(/"/g, '&quot;');
const inline = (s) => {
  const codes = []; s = s.replace(/`([^`]+)`/g, (_, c) => { codes.push(c); return `\u0000${codes.length - 1}\u0000`; });
  s = esc(s)
    .replace(/\[([^\]]+)\]\(([^)\s]+)\)/g, (_, t, u) => `<a href="${u}">${t}</a>`)
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/\*([^*]+)\*/g, '<em>$1</em>');
  return s.replace(/\u0000(\d+)\u0000/g, (_, i) => `<code class="inline">${esc(codes[+i])}</code>`);
};

// blocks: paragraphs (lines joined), ``` code, # and ## headings
const blocks = []; const lines = md.split('\n'); let para = [];
const flush = () => { if (para.length) { blocks.push({ k: 'p', t: para.join(' ') }); para = []; } };
for (let i = 0; i < lines.length; i++) {
  const l = lines[i];
  if (l.trim().startsWith('```')) { flush(); const code = []; i++; while (i < lines.length && !lines[i].trim().startsWith('```')) code.push(lines[i++]); blocks.push({ k: 'code', t: code.join('\n') }); continue; }
  const h = l.match(/^(#{1,2})\s+(.*)$/);
  if (h) { flush(); blocks.push({ k: h[1].length === 1 ? 'h1' : 'h2', t: h[2].trim() }); continue; }
  if (!l.trim()) { flush(); continue; }
  para.push(l.trim());
}
flush();

let title = 'Timbient Modules', tagline = ''; const sections = []; let cur = null;
for (const b of blocks) {
  if (b.k === 'h1') { title = b.t; continue; }
  if (b.k === 'h2') { cur = { title: b.t, body: [] }; sections.push(cur); continue; }
  if (!cur) { if (b.k === 'p' && !tagline) tagline = b.t; continue; }
  cur.body.push(b.k === 'code' ? `<code>${esc(b.t)}</code>` : `<p>${inline(b.t)}</p>`);
}
const accents = ['var(--cyan)', 'var(--amber)', 'var(--pink)'];
const cards = sections.map((s, i) => `    <div class="card">
      <h2><span style="background: ${accents[i % 3]}"></span>${inline(s.title)}</h2>
      ${s.body.join('\n      ')}
    </div>`).join('\n');

const out = tpl.replaceAll('{{TITLE}}', inline(title)).replaceAll('{{TITLE_TEXT}}', esc(title)).replaceAll('{{TAGLINE}}', inline(tagline)).replaceAll('{{TAGLINE_TEXT}}', esc(tagline)).replace('{{SECTIONS}}', cards);
writeFileSync(outPath, out);
console.log(`home page: "${title}", ${sections.length} sections -> ${outPath}`);
