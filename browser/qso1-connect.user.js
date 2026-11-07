// ==UserScript==
// @name         QSO One connect links on Linux (Wine)
// @namespace    https://github.com/GM4GLD/QSO-one-wine
// @version      1.0.0
// @description  Makes qso1.net "Connect ... on QSO One" buttons fire qsoone://
//               links on Linux/Wine instead of redirecting to /download.
// @match        https://qso1.net/*
// @run-at       document-start
// @grant        none
// ==/UserScript==

(function () {
  'use strict';

  // Self-contained: this function is also injected into the page world, so
  // it must not reference anything outside its own scope.
  function patch(win) {
    // 1. qso1.net only offers qsoone:// deep links when navigator.userAgent
    //    contains "Windows NT". Android gets an intent:// URL; every other
    //    platform is router-pushed straight to /download without the scheme
    //    ever being attempted. The Wine-installed qsoone:// handler works
    //    fine, so we just report Windows for this site.
    var WINDOWS_UA =
      'Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 ' +
      '(KHTML, like Gecko) Chrome/142.0.0.0 Safari/537.36';
    try {
      Object.defineProperty(win.Navigator.prototype, 'userAgent', {
        get: function () { return WINDOWS_UA; },
        configurable: true,
      });
    } catch (e) { /* non-configurable here; the injected copy may still work */ }

    // 2. On the "windows" path the site fires the qsoone:// URL, then gives
    //    the OS 1500 ms to steal focus (blur / visibilitychange) before
    //    assuming failure and pushing /download. Under Wine the app launches
    //    but often doesn't focus in time, so the fallback fires anyway.
    //    Dispatch a synthetic blur after a Connect-button click so the
    //    site treats the handoff as successful. The fallback is then
    //    suppressed entirely — if QSO One is ever genuinely missing, use
    //    the "Don't have QSO One? Get it free" link on each net card.
    win.addEventListener('click', function (ev) {
      var btn = ev.target && ev.target.closest
        ? ev.target.closest('button')
        : null;
      if (!btn) return;
      var label =
        (btn.getAttribute('aria-label') || '') + ' ' +
        (btn.getAttribute('title') || '') + ' ' +
        (btn.textContent || '');
      if (/QSO One/i.test(label)) {
        win.setTimeout(function () {
          win.dispatchEvent(new win.Event('blur'));
        }, 0);
      }
    }, true);
  }

  // Userscript managers may run in an isolated world whose
  // Navigator.prototype is a different object from the page's. Patch this
  // world (covers @grant none / inject-into page) and inject a copy into
  // the page world (covers default sandboxed execution).
  patch(window);
  try {
    var s = document.createElement('script');
    s.textContent = '(' + patch.toString() + ')(window)';
    (document.head || document.documentElement).appendChild(s);
    s.remove();
  } catch (e) { /* injection blocked; page-world patch skipped */ }
})();
