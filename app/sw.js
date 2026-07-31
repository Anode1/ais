// sw.js -- minimal service worker: cache the app shell so it installs and opens
// offline. The API (recall/put) is NEVER cached -- those must hit the live engine.
var CACHE = 'ais-v2';
var SHELL = ['/', '/app.css', '/manifest.webmanifest', '/icon.png'];

self.addEventListener('install', function (e) {
  e.waitUntil(caches.open(CACHE).then(function (c) { return c.addAll(SHELL); }));
  self.skipWaiting();
});

self.addEventListener('activate', function (e) {
  e.waitUntil(caches.keys().then(function (keys) {
    return Promise.all(keys.filter(function (k) { return k != CACHE; })
                           .map(function (k) { return caches.delete(k); }));
  }).then(function () { return self.clients.claim(); }));
});

// NETWORK-FIRST for the shell, cache only as the offline fallback. Cache-first
// meant an installed app kept serving the page it was installed with: every edit
// to index.html or app.css needed CACHE bumped by hand, and a missed bump was
// invisible to the developer (whose browser runs no service worker) while users
// saw a frozen app. The shell is a few KB, so re-fetching is cheap, and a fresh
// copy is written back on every success, so offline still works.
self.addEventListener('fetch', function (e) {
  var url = new URL(e.request.url);
  if (url.pathname.indexOf('/api/') === 0) return;   // live data, no cache
  if (e.request.method !== 'GET') return;
  e.respondWith(
    fetch(e.request).then(function (r) {
      if (r && r.ok) {
        var copy = r.clone();
        // waitUntil, or the worker can be killed once respondWith settles and the
        // refreshed copy is silently never written -- leaving the shell frozen at
        // whatever install cached, the exact failure this rewrite exists to fix.
        e.waitUntil(caches.open(CACHE).then(function (c) { return c.put(e.request, copy); }));
      }
      return r;
    }).catch(function () {
      return caches.match(e.request).then(function (r) {
        if (r) return r;
        // Only a NAVIGATION may fall back to the shell. Answering, say, an image
        // request with index.html hands the caller a 200 full of HTML.
        return e.request.mode === 'navigate' ? caches.match('/') : Response.error();
      });
    })
  );
});
