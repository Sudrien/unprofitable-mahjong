/*
 * sw.js -- the service worker that lets Unprofitable Mahjong run offline,
 * and be installed as an app.
 *
 * The page itself is fetched network-first: online, every load is the
 * newest deploy, as it is without a service worker; offline, the copy
 * from the last visit. The icons and manifest are kept the same way.
 * The fonts come from Google, so they are cached as they are first used
 * and served from the cache after that: they never change at those URLs.
 *
 * Nothing about a game is here. The game, best scores and settings stay
 * in the page's localStorage, as before.
 *
 * Bump VERSION when the list of files below changes; the old cache is
 * dropped when the new worker takes over.
 *
 * SPDX-License-Identifier: MIT
 */
'use strict';

const VERSION = 'v1';
const PAGE = 'um-page-' + VERSION;
const FONTS = 'um-fonts-' + VERSION;

/* Relative to this file, so it works at /unprofitable-mahjong/ on Pages
 * or at the root of any other host. */
const FILES = [
  './',
  'manifest.webmanifest',
  'apple-touch-icon.png',
  'icons/icon-192.png',
  'icons/icon-512.png',
  'icons/maskable-512.png',
];

self.addEventListener('install', e => {
  e.waitUntil(caches.open(PAGE).then(c => c.addAll(FILES)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', e => {
  e.waitUntil(
    caches.keys()
      .then(keys => Promise.all(keys.filter(k => k.startsWith('um-') && k !== PAGE && k !== FONTS).map(k => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});

const ROOT = new URL('./', self.location).pathname;
const OWN = new Set(FILES.map(f => new URL(f, self.location).pathname));

const FONT_HOSTS = ['fonts.googleapis.com', 'fonts.gstatic.com'];

self.addEventListener('fetch', e => {
  const req = e.request;
  if (req.method !== 'GET') return;
  const url = new URL(req.url);

  if (FONT_HOSTS.includes(url.hostname)) {
    e.respondWith(caches.open(FONTS).then(async c => {
      const hit = await c.match(req);
      if (hit) return hit;
      const res = await fetch(req);
      // the stylesheets are no-cors (opaque, status 0); the font files
      // come back with CORS. Keep either, but never an error page.
      if (res.ok || res.type === 'opaque') c.put(req, res.clone());
      return res;
    }));
    return;
  }

  if (url.origin !== self.location.origin) return;
  // Only the app's own files. On Pages the rest of the repository is
  // served under the same path (the firmware, the sims, the READMEs), and
  // a visit to one of those must not be taken for the page.
  const page = url.pathname === ROOT || url.pathname === ROOT + 'index.html';
  if (!page && !OWN.has(url.pathname)) return;
  const key = page ? './' : req;

  e.respondWith((async () => {
    const c = await caches.open(PAGE);
    try {
      const res = await fetch(req);
      if (res.ok) c.put(key, res.clone());
      return res;
    } catch (err) {
      // offline: the copy from the last visit, whatever its ?query
      const hit = await c.match(key, { ignoreSearch: true });
      if (hit) return hit;
      throw err;
    }
  })());
});
