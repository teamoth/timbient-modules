// Timbient Modules web player: the page's side. The engine and drawing are the app's own (timbient.js / timbient.wasm,
// compiled from the same C++); this file picks the session, feeds the sound card, copies the picture and moves the view.
'use strict';
(function () {
  const CAP = 50 * 1024 * 1024;           // the biggest session opened here: past that, the app is the place for it
  const $ = (id) => document.getElementById(id);
  const msg = (t, warn) => { $('msg').textContent = t || ''; $('msg').classList.toggle('warn', !!warn); };
  let M = null, ctx = null, node = null, bytes = null, name = '', loaded = false, heavyShown = false, load = 0, slowFor = 0;
  let zoom = 2;                             // screen pixels for each of the app's pixels (pinch or the wheel to change)
  const S = 3;                              // the picture's detail: three times the app's pixels (its finer letters need it)
  const dpr = () => window.devicePixelRatio || 1;

  // ---- starting: the engine at the sound card's own rate (a tap first: browsers only play sound after one) ----
  async function start() {
    $('go').disabled = true; $('startNote').textContent = 'Loading…';
    try {
      M = await TimbientModule();
      const AC = window.AudioContext || window.webkitAudioContext; ctx = new AC({ latencyHint: 'playback' });
      M._tb_init(ctx.sampleRate);
      // the sound: 4096 samples at a time (about a tenth of a second: steady rather than quick, as nothing here is played live)
      node = ctx.createScriptProcessor(4096, 0, 2);
      node.onaudioprocess = (e) => {
        const n = e.outputBuffer.length, t0 = performance.now(); const p = M._tb_render(n) >> 2; const h = M.HEAPF32;
        e.outputBuffer.getChannelData(0).set(h.subarray(p, p + n)); e.outputBuffer.getChannelData(1).set(h.subarray(p + n, p + 2 * n));
        load = load * 0.8 + ((performance.now() - t0) / (n / ctx.sampleRate * 1000)) * 0.2;   // how much of the time it had, it needed
      };
      node.connect(ctx.destination); await ctx.suspend();
      $('start').style.display = 'none'; requestAnimationFrame(frame);
      msg('Open a session to play it (or drop one here)');
    } catch (err) { $('go').disabled = false; $('startNote').textContent = 'This browser could not start the player: ' + (err && err.message ? err.message : err); }
  }
  $('go').addEventListener('click', start);

  // ---- opening a session ----
  async function open(file) {
    if (!M) { msg('Tap to start first'); return; }
    if (file.size > CAP) { msg(`That session is ${(file.size / 1048576).toFixed(0)} MB: the web player opens up to 50 MB. Play it in the app.`, true); return; }
    msg('Opening ' + file.name + '…');
    try { bytes = new Uint8Array(await file.arrayBuffer()); name = file.name; } catch (e) { msg('That file could not be read', true); return; }
    reload(true);
  }
  function reload(fresh) {
    if (!bytes) return;
    const p = M._tb_alloc(bytes.length); if (!p) { msg('Not enough memory on this device for that session', true); return; }
    M.HEAPU8.set(bytes, p);                                   // (the player frees it once it has read it)
    const nl = M.lengthBytesUTF8(name) + 1, np = M._malloc(nl); M.stringToUTF8(name, np, nl);
    const ok = M._tb_load(p, bytes.length, np); M._free(np); const note = M.UTF8ToString(M._tb_note());
    if (!ok) { msg(note || 'That is not a Timbient session', true); return; }
    loaded = true; heavyShown = false; slowFor = 0; M._tb_level(parseFloat($('level').value));
    ['play', 'stop', 'restart'].forEach((b) => ($(b).disabled = false));
    msg(note ? note : (fresh ? 'Playing ' + name : 'From the start'), !!note); play();
  }
  $('file').addEventListener('change', (e) => { if (e.target.files[0]) open(e.target.files[0]); e.target.value = ''; });
  const stage = $('stage');
  stage.addEventListener('dragover', (e) => { e.preventDefault(); stage.classList.add('over'); });
  stage.addEventListener('dragleave', () => stage.classList.remove('over'));
  stage.addEventListener('drop', (e) => { e.preventDefault(); stage.classList.remove('over'); const f = e.dataTransfer.files[0]; if (f) open(f); });

  // ---- play, stop, from the start, level ----
  function play() { if (ctx && loaded) ctx.resume(); $('play').classList.add('on'); $('stop').classList.remove('on'); }
  function stop() { if (ctx) ctx.suspend(); $('stop').classList.add('on'); $('play').classList.remove('on'); }
  $('play').addEventListener('click', play); $('stop').addEventListener('click', stop);
  $('restart').addEventListener('click', () => reload(false));
  $('level').addEventListener('input', (e) => { if (M) M._tb_level(parseFloat(e.target.value)); });

  // ---- the picture: the app draws itself (w by h of its pixels), three times over for the letters; shown at zoom ----
  const cv = $('screen'), g = cv.getContext('2d'); let last = 0, acc = 0;
  function frame(t) {
    requestAnimationFrame(frame);
    const dt = last ? Math.min(0.1, (t - last) / 1000) : 0.016; last = t; acc += dt; if (acc < 1 / 30) return;   // 30 a second is plenty here
    const rect = stage.getBoundingClientRect(); const w = Math.max(80, Math.floor(rect.width / zoom)), h = Math.max(60, Math.floor(rect.height / zoom));
    const p = M._tb_frame(w, h, S, acc) >>> 0; acc = 0; const W = M._tb_imgW(), H = M._tb_imgH();
    if (cv.width !== W || cv.height !== H) { cv.width = W; cv.height = H; }
    cv.style.width = w * zoom + 'px'; cv.style.height = h * zoom + 'px';
    g.putImageData(new ImageData(new Uint8ClampedArray(M.HEAPU8.buffer, p, W * H * 4), W, H), 0, 0);
    // too heavy for this device: say so (once), naming the heavier kinds the session uses
    if (loaded && !ctx.state.startsWith('susp')) { slowFor = load > 0.85 ? slowFor + dt : Math.max(0, slowFor - dt);
      if (slowFor > 3 && !heavyShown) { heavyShown = true; const hv = M.UTF8ToString(M._tb_heavy());
        msg('This session is too heavy for this device, so it may crackle' + (hv ? '. The heaviest parts: ' + hv : '') + '. The app plays it properly.', true); } }
  }

  // ---- looking around: drag to move, pinch (or the wheel) to zoom ----
  const pts = new Map(); let pinch0 = 0, zoom0 = zoom, rem = [0, 0];
  stage.addEventListener('pointerdown', (e) => { if (!M) return; stage.setPointerCapture(e.pointerId); pts.set(e.pointerId, [e.clientX, e.clientY]); stage.classList.add('drag');
    if (pts.size === 2) { const [a, b] = [...pts.values()]; pinch0 = Math.hypot(a[0] - b[0], a[1] - b[1]); zoom0 = zoom; } });
  stage.addEventListener('pointermove', (e) => { if (!pts.has(e.pointerId)) return; const was = pts.get(e.pointerId); pts.set(e.pointerId, [e.clientX, e.clientY]);
    if (pts.size === 2) { const [a, b] = [...pts.values()]; const d = Math.hypot(a[0] - b[0], a[1] - b[1]); if (pinch0 > 0) setZoom(zoom0 * d / pinch0); return; }
    rem[0] += (e.clientX - was[0]) / zoom; rem[1] += (e.clientY - was[1]) / zoom; const dx = Math.trunc(rem[0]), dy = Math.trunc(rem[1]);
    if (dx || dy) { M._tb_pan(dx, dy); rem[0] -= dx; rem[1] -= dy; } });
  const up = (e) => { pts.delete(e.pointerId); if (!pts.size) stage.classList.remove('drag'); pinch0 = 0; };
  stage.addEventListener('pointerup', up); stage.addEventListener('pointercancel', up);
  stage.addEventListener('wheel', (e) => { e.preventDefault(); if (e.ctrlKey || Math.abs(e.deltaY) > Math.abs(e.deltaX) * 2) setZoom(zoom * (e.deltaY < 0 ? 1.1 : 1 / 1.1)); else if (M) M._tb_pan(-Math.round(e.deltaX / zoom), 0); }, { passive: false });
  function setZoom(z) { zoom = Math.max(1, Math.min(5, z)); }
  if (window.innerWidth >= 900) zoom = 2.5; if (window.innerWidth < 500) zoom = 1.6;

  if (!window.WebAssembly) $('startNote').textContent = 'This browser cannot run the player (it has no WebAssembly).';
})();
