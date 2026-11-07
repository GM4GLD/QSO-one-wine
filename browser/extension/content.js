// Runs in the page's MAIN world (see manifest.json), so Navigator.prototype
// here is the one the site's code reads.
(function () {
  'use strict';

  // 1. qso1.net only offers qsoone:// deep links when navigator.userAgent
  //    contains "Windows NT". Android gets an intent:// URL; every other
  //    platform is router-pushed straight to /download without the scheme
  //    ever being attempted. The Wine-installed qsoone:// handler works
  //    fine, so we just report Windows for this site.
  var WINDOWS_UA =
    'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 ' +
    '(KHTML, like Gecko) Chrome/142.0.0.0 Safari/537.36';
  try {
    Object.defineProperty(Navigator.prototype, 'userAgent', {
      get: function () { return WINDOWS_UA; },
      configurable: true,
    });
  } catch (e) { /* non-configurable; nothing else we can do at JS level */ }

  // 2. On the "windows" path the site fires the qsoone:// URL, then gives
  //    the OS 1500 ms to steal focus (blur / visibilitychange) before
  //    assuming failure and pushing /download. Under Wine the app launches
  //    but often doesn't focus in time, so the fallback fires anyway.
  //    Dispatch a synthetic blur after a Connect-button click so the
  //    site treats the handoff as successful. The fallback is then
  //    suppressed entirely — if QSO One is ever genuinely missing, use
  //    the "Don't have QSO One? Get it free" link on each net card.
  window.addEventListener('click', function (ev) {
    var btn = ev.target && ev.target.closest
      ? ev.target.closest('button')
      : null;
    if (!btn) return;
    var label =
      (btn.getAttribute('aria-label') || '') + ' ' +
      (btn.getAttribute('title') || '') + ' ' +
      (btn.textContent || '');
    if (/QSO One/i.test(label)) {
      setTimeout(function () {
        window.dispatchEvent(new Event('blur'));
      }, 0);
    }
  }, true);
})();
