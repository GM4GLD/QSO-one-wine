# Developer notes

Build instructions, the diagnostic probe, and the technical background
for the fixes. For installation and usage see [README.md](README.md).

## Building the DLLs

Requires the MinGW-w64 toolchain. On Arch Linux:

```bash
sudo pacman -S mingw-w64-gcc
```

`build.sh` rebuilds all three artifacts, or build them individually:

```bash
# Microphone shim
x86_64-w64-mingw32-gcc -shared -O2 -o mf-shim/mf.dll \
    mf-shim/mfshim.c mf-shim/mfshim.def \
    -lole32 -lmfplat -lmfuuid -luuid -lwinpthread

# Notification stub
x86_64-w64-mingw32-gcc -shared -O2 \
    -o notif-stub/flutter_local_notifications_windows.dll \
    notif-stub/notifstub.c

# Diagnostic probe
x86_64-w64-mingw32-gcc -O0 -g -o probe/capprobe.exe probe/capprobe.c \
    -lole32 -lmfplat -lmfreadwrite -lmf -lmfuuid -luuid -loleaut32
```

`probe/capprobe.exe` tests raw WASAPI capture and the Media Foundation
capture path independently of QSO One. Run it inside the Wine prefix
with a test audio signal playing.

## icu.dll

`icu/icu.dll` is Wine's own prebuilt build, taken verbatim from a
Wine 11.x install — not source we wrote. See [icu/README.md](icu/README.md)
for provenance and licensing. The installer copies it unconditionally:
it is harmless where Wine already ships one and required on builds
that don't (e.g. Debian's wine-10.0).

## Manual install (what install.sh does)

The DLLs must go next to `qso_one.exe`. Install locations vary:

- `~/.wine/drive_c/Program Files/QSO One/` (with a space)
- `~/.wine/drive_c/Program Files/QSOone/` (no space)
- Per-user: `~/.wine/drive_c/users/<name>/AppData/Local/Programs/QSO One/`
- A portable/extracted copy, e.g. `~/Downloads/qsoone/`

```bash
APP="$HOME/.wine/drive_c/Program Files/QSOone"

cp "$APP/flutter_local_notifications_windows.dll" \
   "$APP/flutter_local_notifications_windows.dll.disabled"

cp mf-shim/mf.dll "$APP/mf.dll"
cp notif-stub/flutter_local_notifications_windows.dll "$APP/"
cp icu/icu.dll "$APP/"
```

Launch manually:

```bash
WINEPREFIX="$HOME/.wine" TZ='Europe/London' wine \
    "$HOME/.wine/drive_c/Program Files/QSOone/qso_one.exe"
```

(Substitute your own IANA zone for `Europe/London`.)

## Why the TZ fix works

Symptom: QSO One holds ~60-70% of a CPU core while sitting idle.

Cause: the app's timers and log timestamps perform `DateTime`
local-time conversions many times per second. On Windows each call is
effectively free; under Wine every call becomes a wineserver RPC
round-trip — registry open/query/close on `Time Zones\<zone>`,
`tzres.dll` loads, and repeated `/etc/localtime` stats — hundreds of
syscalls per second.

Setting `TZ` makes glibc cache the timezone once instead of re-statting
`/etc/localtime` per call. (The wineserver registry RPC side is an
app-level cost; setting `TZ` still removes the largest share of the
burn.)

The launcher lives at `~/.local/share/applications/qso-one-fixed.desktop`
— outside `~/.local/share/applications/wine/`, which Wine regenerates
from `.lnk` files — so app updates cannot overwrite it. The installer
adds `Hidden=true` to the Wine-generated "QSO One" entries under
`applications/wine/`, which makes them count as deleted so only the
TZ-fixed launcher appears in menus. Matching is deliberately narrow
(`Name=` of exactly "QSO One" — the "Uninstall QSO One" entry is left
visible — or `qso_one.exe` in `Exec=`) because a `WINEPREFIX` path can
itself be named `qso-one` and would otherwise false-positive on
unrelated entries — including the `qsoone://` protocol handler, which
must not be hidden.
Wine re-creates those entries on updates, hence the instruction to
re-run the installer after every update.

## The website connect buttons

qso1.net's `ConnectButton` click handler sniffs `navigator.userAgent`
(decompiled from the Next.js chunks):

```js
const platform = /android/i.test(navigator.userAgent) ? 'android'
               : /Windows NT/i.test(navigator.userAgent) ? 'windows'
               : 'other';
if (platform === 'android') location.href = links.intent;          // intent://...#Intent;scheme=qsoone;...
else if (platform === 'windows') {
  let succeeded = false;
  document.addEventListener('visibilitychange',
    () => { if (document.hidden) succeeded = true; });
  window.addEventListener('blur', () => { succeeded = true; }, { once: true });
  location.href = links.scheme;                                    // qsoone://connect?...
  setTimeout(() => { if (!succeeded) router.push('/download'); }, 1500);
} else router.push('/download');                                   // Linux lands here
```

So on Linux the `qsoone://` scheme is never attempted — the UA gate is
the whole blocker; the Wine-registered `x-scheme-handler/qsoone` desktop
entry works as-is (verified: `qso_one.exe` launches with
`qsoone://connect?v=1&protocol=allstarlink&identifier=569430&...`).

Two details shape the fix:

- The check reads the **JS property** `navigator.userAgent`, not the
  HTTP header — a UA-spoofing extension that only rewrites request
  headers (e.g. declarativeNetRequest rules) is not enough. The patch
  must run in the page's main world.
- The 1500 ms blur heuristic doesn't fire under Wine (xdg-open and the
  app don't steal focus from the browser), so even a successful launch
  ends on `/download`. The script dispatches `window.dispatchEvent(new
  Event('blur'))`, which truthfully reports "the OS took it" from the
  site's point of view — a plain `Event` (not `FocusEvent`) is what a
  real window blur delivers to `window` listeners anyway.

Button matching uses `aria-label`/`title`/`textContent` matching
`/QSO One/i` because the compact chip variant renders short text like
"EchoLink 106963" with the full "Connect on ... on QSO One" string only
in `aria-label` and `title`.

### Applying the fix

The same patch ships two ways under `browser/`; pick one per browser
profile. Both apply only to qso1.net and take effect on the next page
load — reload any open qso1.net tab after installing.

**Option 1 — unpacked extension** (no extra dependencies):

1. Open `brave://extensions` (Brave) or `chrome://extensions` (Chrome,
   Edge, Vivaldi…).
2. Enable **Developer mode** (top-right toggle).
3. **Load unpacked** → select the `browser/extension/` directory from
   this repo.
4. Reload qso1.net.

Caveat: unpacked extensions trigger the "disable developer mode
extensions?" prompt on browser start in some Chromium builds; choosing
**Cancel** keeps the extension. The extension must stay at the path you
loaded it from — keep the repo cloned rather than copying the folder to
a temp location.

**Option 2 — userscript** (Tampermonkey, Violentmonkey, Greasemonkey,
etc.):

1. Install a userscript manager extension.
2. Open `browser/qso1-connect.user.js` raw — from a clone, open the file
   in the browser (`file://` URL) and the manager will offer to install
   it; or create a new script in the manager and paste the file
   contents. Once this repo is pushed, the raw GitHub URL also works as
   a one-click install link.
3. Reload qso1.net.

The script is written to work in both page context (`@grant none`) and
sandboxed/isolated worlds — in the latter case it also injects itself
into the page via a `<script>` element, which qso1.net's CSP currently
permits. If the site ever ships a `Content-Security-Policy` that blocks
inline scripts, configure your manager to inject into the page context
directly (Violentmonkey: "Inject into: page"; Tampermonkey: `@grant
none` already runs in page context).

**Verify:** click a "Connect ... on QSO One" button on
[Net Finder](https://qso1.net/nets#find-a-net). The browser asks once
whether to open the external app — allow it (tick "always allow" to skip
future prompts). QSO One should launch onto the net's connect screen
while the page stays where it was. If the `/download` fallback still
fires, check that the patch actually ran in the page world
(`navigator.userAgent` in DevTools console should report Windows).

**Side effects, both intentional:**

- The `/download` fallback is suppressed entirely. If QSO One isn't
  actually installed, nothing happens on click — use the "Don't have
  QSO One? Get it free" link on each net card.
- qso1.net's download page will offer the Windows build, which is what
  you want under Wine anyway.

### Removing it

Disable/remove the extension or userscript and reload the page. There
is no persistent state — the patch only ever modifies the live
`Navigator.prototype` of loaded qso1.net pages.
