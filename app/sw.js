// sw.js -- minimal service worker: cache the app shell so it installs and opens
// offline. The API (recall/put) is NEVER cached -- those must hit the live engine.
var CACHE = 'ais-v1';
var SHELL = ['/', '/app.css', '/manifest.webmanifest', '/icon.png'];

self.addEventListener('install', function (e) {
  e.waitUntil(caches.open(CACHE).then(function (c) { return c.addAll(SHELL); }));
  self.skipWaiting();
});

self.addEventListener('activate', function (e) {
  e.waitUntil(caches.keys().then(function (keys) {
    return Promise.all(keys.filter(function (k) { return k != CACHE; })
                           .map(function (k) { return caches.delete(k); }));
  }));
});

self.addEventListener('fetch', function (e) {
  var url = new URL(e.request.url);
  if (url.pathname.indexOf('/api/') === 0) return;   // live data, no cache
  e.respondWith(caches.match(e.request).then(function (r) { return r || fetch(e.request); }));
});
