# QSO One under Wine

Workarounds for Wine compatibility issues affecting QSO One, a Flutter
application, on Linux.

**Verified working:** QSO One 4.1.665.0, 4.1.8 (build 673), and 4.2.2,
tested on Arch Linux (Wine 11.20, PipeWire) and Debian 13 trixie
(Wine 10.0).

## What this fixes

- **Microphone:** restores transmit audio by providing Media Foundation
  capture through WASAPI (`mf.dll`).
- **Notifications:** prevents crashes caused by Wine's incomplete WinRT
  toast notification support. Desktop toast notifications are disabled
  as a trade-off.
- **Timezone plugin:** installs `icu.dll`, which
  `flutter_timezone_plugin.dll` needs but some Wine builds don't ship.
- **Idle CPU:** works around ~60-70% of a core being burned at idle by
  launching the app with `TZ` set. A "QSO One (TZ fixed)" menu entry is
  created and the Wine-generated "QSO One" entry is hidden.

It also includes a **browser-side fix** for the "Connect ... on QSO One"
buttons on qso1.net, which otherwise bounce Linux users to the download
page — see [Browser connect links](#browser-connect-links).

## Install

**Prerequisite:** install QSO One under Wine first — download the
Windows installer from the QSO One website and run it with Wine
(e.g. `wine ~/Downloads/<installer-file>.exe`).

Then run this one-liner:

```bash
curl -fsSL https://raw.githubusercontent.com/GM4GLD/QSO-one-wine/main/install.sh | sh
```

It finds your QSO One install (including per-user installs under
`drive_c/users/*`), places the DLLs next to `qso_one.exe`, writes the
"QSO One (TZ fixed)" launcher, and hides the default menu entry.

**Re-run it after every QSO One update** — updates are manual installs
of the latest version and overwrite the DLLs and the menu entry.

Then launch **"QSO One (TZ fixed)"** from your app menu.

To verify the microphone is working, make a test transmission and check
that QSO One has opened an audio capture stream:

```bash
pactl list source-outputs
```

Look for `application.name = "qso_one"`.

## Reverting

Close QSO One and run:

```bash
APP="$HOME/.wine/drive_c/Program Files/QSOone"   # adjust to your install folder

rm -f "$APP/mf.dll" "$APP/icu.dll" \
      "$APP/flutter_local_notifications_windows.dll"
mv "$APP/flutter_local_notifications_windows.dll.disabled" \
   "$APP/flutter_local_notifications_windows.dll"
rm -f ~/.local/share/applications/qso-one-fixed.desktop
```

To un-hide the original menu entry, remove the `Hidden=true` line from
the Wine-generated QSO One `.desktop` file under
`~/.local/share/applications/wine/` (or delete the file and let Wine
regenerate it).

This restores the original notification plugin and removes the
microphone shim. It does not affect QSO One's synced data or system
audio configuration.

## Browser connect links

The "Connect ... on QSO One" buttons on qso1.net only fire the
`qsoone://` link when `navigator.userAgent` says Windows (Android gets
an `intent://` URL); Linux browsers are sent straight to `/download`.
The Wine-installed `qsoone://` handler already works — the site just
never attempts it. [`browser/`](browser) contains a small page patch,
installable as a userscript or an unpacked extension, that fixes this.
See [DEV.md](DEV.md#the-website-connect-buttons) for how it works and
how to apply it.

## Notes

- Toast notifications are disabled entirely, including the
  microphone-active notification.
- The shim supports the PCM capture path required for transmit audio.
  It does not implement encoder transforms required by some
  file-recording formats.
- Other distributions and Wine versions have not been independently
  tested.

See [DEV.md](DEV.md) for build instructions, the diagnostic probe, and
the technical details behind the fixes.
