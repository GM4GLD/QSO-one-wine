# QSO One under Wine: Microphone and Notification Fixes

Workarounds for two Wine compatibility issues affecting QSO One, a Flutter application, on Linux.

**Verified working:** QSO One 4.1.665.0 and 4.1.8 (build 673), tested on Arch Linux (Wine 11.20, PipeWire) and Debian 13 trixie (Wine 10.0, `icu.dll` required — see step 4).

## What this fixes

- **Microphone:** restores transmit audio by providing Media Foundation capture through WASAPI.
- **Notifications:** prevents crashes caused by Wine's incomplete WinRT toast notification support. Desktop toast notifications are disabled as a trade-off.
- **Idle CPU (optional):** fixes ~60-70% of a core being burned at idle by launching the app with `TZ` set explicitly (see [High idle CPU](#high-idle-cpu-under-wine)).

## Installation

Run this one-liner in a terminal — it finds your QSO One install,
downloads the DLLs from this repository, and restores them. Re-run it
after every QSO One update:

```bash
curl -fsSL https://raw.githubusercontent.com/GM4GLD/QSO-one-wine/main/install.sh | sh
```

The steps below show the same process manually, for reference.

### 1. Clone the repo

```bash
git clone https://github.com/GM4GLD/QSO-one-wine ~/QSO-one-wine
cd ~/QSO-one-wine
```

### 2. Copy the DLLs into QSO One's installation directory

The DLLs must go next to `qso_one.exe`. The install location varies —
check which folder exists on your system:

- `~/.wine/drive_c/Program Files/QSO One/` (with a space)
- `~/.wine/drive_c/Program Files/QSOone/` (no space)
- A portable/extracted copy, e.g. `~/Downloads/qsoone/`

The examples below use the no-space folder; adjust `APP` to match yours.

Open a terminal in the repository directory and run:

```bash
APP="$HOME/.wine/drive_c/Program Files/QSOone"

cp "$APP/flutter_local_notifications_windows.dll" \
   "$APP/flutter_local_notifications_windows.dll.disabled"

cp mf-shim/mf.dll "$APP/mf.dll"
cp notif-stub/flutter_local_notifications_windows.dll "$APP/"
```

The first command backs up the original notification plugin. If you have already renamed it to `.disabled`, skip that command.

### 3. Optional — Wine older than 11.x: copy `icu.dll`

`flutter_timezone_plugin.dll` imports `icu.dll`, which Wine only ships
in recent versions (missing on Debian's wine-10.0, for example). On
Wine 11 or newer you can skip this step entirely. If you are on an
older Wine, or see errors like `Library icu.dll ... not found`, copy
it as well:

```bash
cp icu/icu.dll "$APP/"
```

It is Wine's own self-contained build of ICU — no Wine upgrade needed.

### 4. Launch QSO One

```bash
WINEPREFIX="$HOME/.wine" wine \
    "$HOME/.wine/drive_c/Program Files/QSOone/qso_one.exe"
```

Test your microphone by making a test transmission. While the microphone is active, you can check whether QSO One has opened an audio capture stream:

```bash
pactl list source-outputs
```

Look for `application.name = "qso_one"`.

## High idle CPU under Wine

Symptom: QSO One holds ~60-70% of a CPU core while sitting idle.

Cause: the app's timers and log timestamps perform `DateTime` local-time
conversions many times per second. On Windows each call is effectively free;
under Wine every call becomes a wineserver RPC round-trip — registry
open/query/close on `Time Zones\<zone>`, `tzres.dll` loads, and repeated
`/etc/localtime` stats — hundreds of syscalls per second.

Workaround: launch the app with `TZ` set to your local zone. This makes
glibc cache the timezone once instead of re-statting `/etc/localtime` per
call. (The wineserver registry RPC side is an app-level cost; setting
`TZ` still removes the largest share of the burn.)

### 1-click install (creates a separate launcher)

```bash
curl -fsSL https://raw.githubusercontent.com/GM4GLD/QSO-one-wine/main/tzfix.sh | sh
```

This finds your QSO One install and your local timezone, then writes
`~/.local/share/applications/qso-one-fixed.desktop` — a **"QSO One (TZ
fixed)"** menu entry that launches the app with `TZ` set.

It is safe from updates: Wine only auto-regenerates launchers inside
`~/.local/share/applications/wine/`, so this file is never overwritten.
Your original "QSO One" entry is untouched; use either.

### Manual equivalent

Add a desktop entry yourself, or launch from a terminal:

```bash
WINEPREFIX="$HOME/.wine" TZ='Europe/London' wine \
    "$HOME/.wine/drive_c/Program Files/QSO One/qso_one.exe"
```

(Substitute your own IANA zone for `Europe/London`.)

To remove the fix: `rm ~/.local/share/applications/qso-one-fixed.desktop`

### Reverting the changes

Close QSO One and run:

```bash
APP="$HOME/.wine/drive_c/Program Files/QSOone"

rm -f "$APP/mf.dll"
rm -f "$APP/icu.dll"
rm -f "$APP/flutter_local_notifications_windows.dll"
mv "$APP/flutter_local_notifications_windows.dll.disabled" \
   "$APP/flutter_local_notifications_windows.dll"
```

This restores the original notification plugin and removes the microphone shim. It does not affect QSO One's synced data or system audio configuration.

## Developer instructions

### Building the DLLs

On Arch Linux, install the MinGW-w64 compiler:

```bash
sudo pacman -S mingw-w64-gcc
```

Build the microphone shim:

```bash
cd mf-shim
x86_64-w64-mingw32-gcc -shared -O2 -o mf.dll mfshim.c mfshim.def \
    -lole32 -lmfplat -lmfuuid -luuid -lwinpthread
```

Build the notification stub:

```bash
cd ../notif-stub
x86_64-w64-mingw32-gcc -shared -O2 \
    -o flutter_local_notifications_windows.dll notifstub.c
```

Copy the resulting DLLs using the installation instructions above.

### Diagnostic probe

The `probe/` directory contains `capprobe.exe`, which tests raw WASAPI capture and the Media Foundation capture path independently of QSO One.

Build it with:

```bash
cd probe
x86_64-w64-mingw32-gcc -O0 -g -o capprobe.exe capprobe.c \
    -lole32 -lmfplat -lmfreadwrite -lmf -lmfuuid -luuid -loleaut32
```

Run it inside the Wine prefix with a test audio signal playing.

## Notes

- **Updates:** QSO One updates may overwrite the replacement DLLs. Copy them back if necessary.
- **Notifications:** toast notifications are disabled, including the microphone-active notification.
- **Audio:** the shim supports the PCM capture path required for transmit audio. It does not implement encoder transforms required by some file-recording formats.
- **Compatibility:** verified on Arch Linux (Wine 11.20, PipeWire) and Debian 13 trixie (Wine 10.0 — requires `icu.dll` from this repo), with QSO One 4.1.665.0 and 4.1.8 (build 673). Other configurations have not been independently tested.

