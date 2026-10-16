// Sebi's DAW mobile - keeps the app on the phone so it opens without a connection.
const VERSION = 'sebis-daw-1.1.0';
const FILES = ['./', 'index.html', 'style.css', 'manifest.webmanifest', 'dawcore.wasm', 'worklet.js',
  'js/app.js', 'js/ctx.js', 'js/dawcore.js', 'js/songfile.js', 'js/canvas.js', 'js/arrange.js', 'js/roll.js', 'js/play.js', 'js/mixer.js', 'js/songs.js',
  'icons/icon-180.png', 'icons/icon-192.png', 'icons/icon-512.png'];
self.addEventListener('install', e => e.waitUntil(caches.open(VERSION).then(c => c.addAll(FILES)).then(() => self.skipWaiting())));
self.addEventListener('activate', e => e.waitUntil(caches.keys().then(ks => Promise.all(ks.filter(k => k !== VERSION).map(k => caches.delete(k)))).then(() => self.clients.claim())));
// network first (so updates arrive), the saved copy when offline
self.addEventListener('fetch', e => {
  if (e.request.method !== 'GET' || new URL(e.request.url).origin !== location.origin) return;
  e.respondWith(fetch(e.request).then(r => { const copy = r.clone(); caches.open(VERSION).then(c => c.put(e.request, copy)); return r; }).catch(() => caches.match(e.request, { ignoreSearch: true })));
});
