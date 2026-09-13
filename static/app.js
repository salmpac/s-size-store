// Impression tracking.
//
// Everything else in the funnel is counted on the server, which is exact and
// cannot be blocked. Impressions are the exception: only the browser knows what
// actually entered the viewport, so they are reported from here.
//
// See docs/analytics.md.

(function () {
  'use strict';

  var base = document.body.dataset.base || '';
  var endpoint = base + '/api/ev';

  // A card counts as seen once it has been at least half visible for a full
  // second — scrolling past at speed is not a view.
  var VISIBLE_RATIO = 0.5;
  var DWELL_MS = 1000;
  var FLUSH_MS = 5000;
  var MAX_BATCH = 200;

  var queue = [];
  // Deduplicated for the life of the page: scrolling up and down past the same
  // card must not inflate its impression count.
  var seen = new Set();
  var timers = new Map();

  function enqueue(el) {
    var id = parseInt(el.dataset.item, 10);
    if (!id || seen.has(id)) return;
    seen.add(id);

    if (queue.length >= MAX_BATCH) return;
    queue.push({
      item: id,
      p: parseInt(el.dataset.pos, 10) || 0,
      s: el.dataset.surface || 'feed'
    });
  }

  function flush(useBeacon) {
    if (!queue.length) return;
    var batch = queue;
    queue = [];

    var body = JSON.stringify(batch);
    // sendBeacon survives the page being closed, which is exactly when the last
    // batch would otherwise be lost.
    if (useBeacon && navigator.sendBeacon) {
      navigator.sendBeacon(endpoint, new Blob([body], { type: 'application/json' }));
      return;
    }
    fetch(endpoint, {
      method: 'POST',
      body: body,
      headers: { 'Content-Type': 'application/json' },
      keepalive: true,
      credentials: 'same-origin'
    }).catch(function () { /* metrics must never break the page */ });
  }

  if ('IntersectionObserver' in window) {
    var observer = new IntersectionObserver(function (entries) {
      entries.forEach(function (entry) {
        var el = entry.target;
        if (entry.isIntersecting && entry.intersectionRatio >= VISIBLE_RATIO) {
          if (timers.has(el)) return;
          timers.set(el, setTimeout(function () {
            enqueue(el);
            timers.delete(el);
            observer.unobserve(el);
          }, DWELL_MS));
        } else {
          var t = timers.get(el);
          if (t) { clearTimeout(t); timers.delete(el); }
        }
      });
    }, { threshold: [VISIBLE_RATIO] });

    document.querySelectorAll('[data-item]').forEach(function (el) {
      observer.observe(el);
    });
  }

  setInterval(function () { flush(false); }, FLUSH_MS);

  // pagehide rather than unload: it is the one that fires reliably on mobile
  // Safari, and it fires when the user taps through to the partner.
  window.addEventListener('pagehide', function () { flush(true); });
  document.addEventListener('visibilitychange', function () {
    if (document.visibilityState === 'hidden') flush(true);
  });
})();
