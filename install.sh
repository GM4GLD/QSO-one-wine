#!/bin/sh
# QSO One Wine fixes installer.
#
#   curl -fsSL https://raw.githubusercontent.com/GM4GLD/QSO-one-wine/main/install.sh | sh
#
# Finds the QSO One install directory, downloads the workaround DLLs
# from this repository, and places them next to qso_one.exe.
# Safe to run again after every QSO One update.

set -e
REPO="${QSO_WINE_REPO:-https://raw.githubusercontent.com/GM4GLD/QSO-one-wine/main}"

APP=""
for d in \
  "$HOME/.wine/drive_c/Program Files/QSO One" \
  "$HOME/.wine/drive_c/Program Files/QSOOne" \
  "$HOME/.local/share/wineprefixes/qso-one/drive_c/Program Files/QSO One" \
  "$HOME/.local/share/wineprefixes/qso-one/drive_c/Program Files/QSOOne"
do
  [ -f "$d/qso_one.exe" ] && { APP="$d"; break; }
done

if [ -z "$APP" ]; then
  echo "install.sh: searching for qso_one.exe under $HOME ..."
  EXE="$(find "$HOME" -maxdepth 7 -name qso_one.exe -print -quit 2>/dev/null)"
  [ -n "$EXE" ] && APP="$(dirname "$EXE")"
fi

if [ -z "$APP" ]; then
  printf "Could not find qso_one.exe. Enter the folder containing it and press Enter: "
  read -r APP < /dev/tty || exit 1
fi

[ -f "$APP/qso_one.exe" ] || { echo "install.sh: no qso_one.exe in $APP" >&2; exit 1; }
echo "install.sh: installing to $APP"

DL="$(mktemp -d)"

curl -fsSL "$REPO/mf-shim/mf.dll" -o "$APP/mf.dll"
echo "  installed mf.dll                 (microphone fix)"

NOTIF="$APP/flutter_local_notifications_windows.dll"
curl -fsSL "$REPO/notif-stub/flutter_local_notifications_windows.dll" -o "$DL/fln.dll"
if [ -f "$NOTIF" ] && ! cmp -s "$NOTIF" "$DL/fln.dll" && [ ! -f "$NOTIF.disabled" ]; then
  mv "$NOTIF" "$NOTIF.disabled"
  echo "  backed up original plugin ->     flutter_local_notifications_windows.dll.disabled"
fi
cp "$DL/fln.dll" "$NOTIF"
echo "  installed flutter_local_notifications_windows.dll (notification crash fix)"

MAJOR="$(wine --version 2>/dev/null | sed -n 's/^wine-\([0-9][0-9]*\).*/\1/p')"
if [ -z "$MAJOR" ] || [ "$MAJOR" -lt 11 ]; then
  curl -fsSL "$REPO/icu/icu.dll" -o "$APP/icu.dll"
  echo "  installed icu.dll                (timezone plugin, Wine < 11)"
else
  echo "  wine-$MAJOR detected: icu.dll not needed, skipped"
fi

rm -rf "$DL"
echo "install.sh: done."
