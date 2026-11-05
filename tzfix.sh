#!/bin/sh
# This fix has been merged into install.sh, which now installs the DLL
# workarounds, icu.dll, and the "QSO One (TZ fixed)" launcher in one step.
# This stub is kept so saved or bookmarked commands keep working.

echo "tzfix.sh: merged into install.sh — running it now..." >&2
REPO="${QSO_WINE_REPO:-https://raw.githubusercontent.com/GM4GLD/QSO-one-wine/main}"
curl -fsSL "$REPO/install.sh" | sh
