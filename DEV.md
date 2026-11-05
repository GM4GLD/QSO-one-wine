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
