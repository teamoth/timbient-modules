// The web player's helper for installing and for opening without a connection: each file is fetched fresh when
// online (so a new release shows at once) and kept, so the player still opens offline with the last version seen.
'use strict';
const KEEP = 'timbient-player';
const FILES = ['./', 'index.html', 'player.js', 'timbient.js', 'timbient.wasm', 'manifest.webmanifest', 'icon-192.png', 'icon-512.png', 'icon-maskable-512.png'];
self.addEventListener('install', (e) => { e.waitUntil(caches.open(KEEP).then((c) => c.addAll(FILES)).catch(() => {}).then(() => self.skipWaiting())); });
self.addEventListener('activate', (e) => { e.waitUntil(self.clients.claim()); });
self.addEventListener('fetch', (e) => {
  if (e.request.method !== 'GET' || new URL(e.request.url).origin !== location.origin) return;
  e.respondWith(fetch(e.request).then((r) => { if (r.ok) { const copy = r.clone(); caches.open(KEEP).then((c) => c.put(e.request, copy)); } return r; })
    .catch(() => caches.match(e.request, { ignoreSearch: true }).then((r) => r || caches.match('index.html'))));
});
