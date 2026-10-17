# QSO One under Wine — microphone capture + notification fixes

Workarounds for two upstream Wine deficiencies that break QSO One
(4.1.665.0, Flutter) on Linux. Developed/verified on Arch Linux,
Wine 11.20, PipeWire, prefix `~/.local/share/wineprefixes/qso-one`.

## TL;DR

Two files must live next to `qso_one.exe` in `~/Downloads/qsoone/`:

```
mf-shim/mf.dll                                    → ~/Downloads/qsoone/mf.dll
notif-stub/flutter_local_notifications_windows.dll → ~/Downloads/qsoone/flutter_local_notifications_windows.dll
```

Both are fully reversible: delete them to restore stock behaviour.
Neither touches the Wine prefix, QSO One's synced data, nor the
system audio configuration.

## Diagnosis (confirmed, not hypothesized)

### Microphone silent under Wine

- `record_windows_plugin.dll` (the Flutter `record` package) captures audio
  via **Media Foundation**: `MFEnumDeviceSources` → `MFCreateDeviceSource` →
  `MFCreateSourceReaderFromMediaSource` → `IMFSourceReader::ReadSample`.
- Wine's `MFEnumDeviceSources` works (that's why the mic appears in
  settings), but **`MFCreateDeviceSource` is a stub returning E_NOTIMPL** —
  `fixme:mfplat:MFCreateDeviceSource ...: stub!` in debug logs. The underlying
  `audio_capture_create_object` in Wine's `dlls/mf/sac.c` is also `E_NOTIMPL`,
  even on master. It is an unimplemented feature, not a regression; no Wine
  version fixes it.
- Meanwhile **raw WASAPI/mmdevapi capture works fine** through
  `winepulse.drv` — verified with `probe/capprobe.exe` (real audio captured
  at 48 kHz float).
- Symptoms matched exactly: no `qso_one` entry in
  `pactl list source-outputs`, and no capture `IAudioClient` in
  `WINEDEBUG=+mmdevapi` logs — because the app dies at
  `MFCreateDeviceSource` before ever opening an audio stream.

### Crash on notifications

- `flutter_local_notifications_windows.dll` (MaikuB's FFI plugin) calls the
  WinRT toast API `Windows.UI.Notifications.ToastNotificationManager`.
  Wine's `RoGetActivationFactory` stub fails; the resulting unhandled C++
  exception kills the process.
- Renaming the DLL avoids the crash but produces a "Module not found"
  plugin error at startup.

## What each artifact does

### `mf-shim/` — app-local `mf.dll` shim

Exports only what the record plugin imports (`MFEnumDeviceSources`,
`MFCreateDeviceSource`, `MFCreateDeviceSourceActivate`) and implements a
real `IMFMediaSource`/`IMFMediaStream`/`IMFPresentationDescriptor`/
`IMFMediaTypeHandler` stack backed by a WASAPI `IAudioCaptureClient`
(winepulse). Because only `record_windows_plugin.dll` imports `mf.dll`,
the shim affects only this application.

Notable implementation details:

- **Device selection**: honours `MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_
  AUDCAP_ENDPOINT_ID` if present (the plugin passes the mmdevapi endpoint
  ID it got from `IMMDevice::GetId`); otherwise the default capture device.
- **Format wildcarding**: the media type handler returns a wildcard
  `IMFMediaType` whose `IsEqual` claims equality with any audio type and
  captures the requester's format — so `SetCurrentMediaType` always
  succeeds without needing an encoder transform, and the source then knows
  the requested rate/channels/depth (QSO One asks for 8 kHz mono PCM16).
- **Conversion**: captures float32 at the device mix rate, converts to the
  requested format. Integer-ratio decimation (48k→8k = 6:1) uses
  boxcar-averaged decimation — plain linear interpolation aliases badly
  and produced the harsh/choppy TX audio observed initially.
- **Cadence**: emits fixed 20 ms chunks (160 samples at 8 kHz), which is
  what AMBE-style vocoders expect; irregular chunk sizes caused audible
  choppiness in the first version.
- Never emits padded silence mid-stream; waits for real data instead.
- Logs diagnostics to stderr (`[mfshim] ...`).

Build:

```
cd mf-shim
x86_64-w64-mingw32-gcc -shared -O2 -o mf.dll mfshim.c mfshim.def \
    -lole32 -lmfplat -lmfuuid -luuid -lwinpthread
```

### `notif-stub/` — no-op notifications plugin

Implements the plugin's dart:ffi C ABI (signatures from upstream
`src/ffi_api.h`) as safe no-ops: `init()` returns true, `showNotification()`
returns true, enumerations return empty lists. Result: the plugin loads,
the app doesn't crash, and no toasts appear.

Trade-off: notifications are silently swallowed (e.g. the
"microphone active" toast will not display). The original DLL remains at
`flutter_local_notifications_windows.dll.disabled` and can be restored if
Wine ever implements the WinRT toast APIs.

Build:

```
cd notif-stub
x86_64-w64-mingw32-gcc -shared -O2 \
    -o flutter_local_notifications_windows.dll notifstub.c
```

### `probe/` — diagnostic probe

`capprobe.exe` tests, inside the Wine prefix:

1. Raw WASAPI capture (mmdevapi `IAudioCaptureClient`) — enumerates
   endpoints, prints the default device, captures ~2 s, reports peak level.
2. The Media Foundation path the plugin uses — `MFEnumDeviceSources`,
   `MFCreateDeviceSource`, `MFCreateSourceReaderFromMediaSource`,
   `SetCurrentMediaType(PCM)`, `ReadSample` loop with peak detection.

Build:

```
cd probe
x86_64-w64-mingw32-gcc -O0 -g -o capprobe.exe capprobe.c \
    -lole32 -lmfplat -lmfreadwrite -lmf -lmfuuid -luuid -loleaut32
```

Run (in the qso-one prefix; play a test tone for a real signal):

```
paplay tone.wav &
WINEPREFIX=~/.local/share/wineprefixes/qso-one wine /path/to/capprobe.exe
```

Put `capprobe.exe` next to a copy of `mf-shim/mf.dll` to test the shim
before deploying it to the app directory.

## Running QSO One

```
cd ~/Downloads/qsoone
WINEPREFIX="$HOME/.local/share/wineprefixes/qso-one" wine ./qso_one.exe
```

Watch stderr for `[mfshim]` and `[notifstub]` lines to confirm both
workarounds are active. `pactl list source-outputs` should show
`application.name = "qso_one"` while the mic is open.

## Notes / caveats

- If a QSO One update overwrites the app directory, re-copy both DLLs.
- The shim does not help with file-recording paths that need encoder MFTs
  (AAC/FLAC); the TX stream path used by QSO One needs only PCM.
- RX audio stutter under heavy CPU load is a Wine scheduling artifact,
  unrelated to these fixes.
- Upstream gap worth a Wine bug report: `MFCreateDeviceSource` and
  `audio_capture_create_object` (`dlls/mf/sac.c`) are unimplemented;
  audio capture via Media Foundation cannot work in any current Wine.
