# icu.dll

This is **not** source we wrote — it is Wine's own prebuilt `icu.dll`,
taken verbatim from a Wine 11.x install
(`/usr/lib/wine/x86_64-windows/icu.dll`).

## Why it is here

`flutter_timezone_plugin.dll` imports `icu.dll`. Wine only began
shipping that DLL recently, so on older Wine (e.g. Debian's
wine-10.0) the plugin fails to load with
`err:module:import_dll Library icu.dll ... not found`.

Dropping this file into the QSO One install directory (next to
`qso_one.exe`) satisfies the import on older Wine — no system Wine
upgrade needed. On Wine 11 or newer, this file is not required.

It is self-contained: it imports only `advapi32`, `kernel32`,
`ucrtbase` and `vcruntime140`, all of which any Wine provides.

## Source

Wine builds `icu.dll` from the upstream ICU4C project, vendored in
the Wine source tree (`dlls/icu`, linking the i18n and common
libraries).

- Wine source: https://gitlab.winehq.org/wine/wine (LGPLv2.1+)
- Upstream ICU: https://github.com/unicode-org/icu
  (Unicode/ICU licence, BSD-style)

If you would rather build or fetch it yourself, extract
`x86_64-windows/icu.dll` from any Wine ≥11 package or build Wine
from source.
