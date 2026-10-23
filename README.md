# QSO One under Wine: Microphone and Notification Fixes

Workarounds for two Wine compatibility issues affecting QSO One, a Flutter application, on Linux.

**Verified working:** QSO One 4.1.665.0 and 4.1.8 (build 673), tested on Arch Linux with Wine 11.20 and PipeWire.

## What this fixes

- **Microphone:** restores transmit audio by providing Media Foundation capture through WASAPI.
- **Notifications:** prevents crashes caused by Wine's incomplete WinRT toast notification support. Desktop toast notifications are disabled as a trade-off.

## Installation (precompiled DLLs)

No compilation is required. The repository includes the compiled DLLs.

### 1. Download the DLLs

Download or clone this repository so you have the `mf-shim/` and `notif-stub/` directories.

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

### 3. Launch QSO One

```bash
WINEPREFIX="$HOME/.wine" wine \
    "$HOME/.wine/drive_c/Program Files/QSOone/qso_one.exe"
```

Test your microphone by making a test transmission. While the microphone is active, you can check whether QSO One has opened an audio capture stream:

```bash
pactl list source-outputs
```

Look for `application.name = "qso_one"`.

### 4. Wine older than 11.x only: copy `icu.dll`

`flutter_timezone_plugin.dll` imports `icu.dll`, which Wine only ships
in recent versions (missing on Debian's wine-10.0, for example). If you
see errors like `Library icu.dll ... not found`, copy it as well:

```bash
cp icu/icu.dll "$APP/"
```

It is Wine's own self-contained build of ICU — no Wine upgrade needed.
On Wine 11 or newer you can skip this step.

### Reverting the changes

Close QSO One and run:

```bash
APP="$HOME/.wine/drive_c/Program Files/QSOone"

rm -f "$APP/mf.dll"
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
- **Compatibility:** verified on Arch Linux, Wine 11.20, PipeWire and QSO One 4.1.8 (build 673). Other configurations have not been independently tested.
