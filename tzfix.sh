#!/bin/sh
# QSO One Wine timezone fix installer.
#
#   curl -fsSL https://raw.githubusercontent.com/GM4GLD/QSO-one-wine/main/tzfix.sh | sh
#
# Creates a separate "QSO One (TZ fixed)" desktop launcher that starts the app
# with TZ set to your local zone. This works around a Wine issue where
# per-tick Dart DateTime local-time conversions become wineserver registry
# round-trips (open/query/close on "Time Zones\<zone>") plus /etc/localtime
# stat storms, burning ~60-70% of a core at idle.
#
# The launcher lives in ~/.local/share/applications (NOT under wine/Programs,
# which Wine regenerates from .lnk files), so app updates cannot overwrite it.
# Your original "QSO One" entry is left untouched.

set -e

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
  echo "tzfix.sh: searching for qso_one.exe under $HOME ..."
  EXE="$(find "$HOME" -maxdepth 7 -name qso_one.exe -print -quit 2>/dev/null)"
  [ -n "$EXE" ] && APP="$(dirname "$EXE")"
fi

if [ -z "$APP" ] || [ ! -f "$APP/qso_one.exe" ]; then
  echo "tzfix.sh: could not find qso_one.exe" >&2
  exit 1
fi
echo "tzfix.sh: app dir   -> $APP"

# Derive the Wine prefix from the app path (drive_c parent); default ~/.wine.
case "$APP" in
  */drive_c/*) PREFIX="${APP%%/drive_c/*}" ;;
  *)           PREFIX="$HOME/.wine" ;;
esac
echo "tzfix.sh: prefix    -> $PREFIX"

# Detect the local IANA timezone name.
TZ=""
if [ -L /etc/localtime ]; then
  TZ="$(readlink /etc/localtime | sed -n 's|.*/zoneinfo/||p')"
fi
if [ -z "$TZ" ] && command -v timedatectl >/dev/null 2>&1; then
  TZ="$(timedatectl show -p Timezone --value 2>/dev/null || true)"
fi
[ -z "$TZ" ] && TZ="UTC0"
echo "tzfix.sh: timezone  -> $TZ"

DESKTOP_DIR="$HOME/.local/share/applications"
DESKTOP="$DESKTOP_DIR/qso-one-fixed.desktop"
mkdir -p "$DESKTOP_DIR"

cat > "$DESKTOP" <<EOF
[Desktop Entry]
Name=QSO One (TZ fixed)
Comment=QSO One with TZ=$TZ to avoid Wine timezone syscall storm
Exec=env "WINEPREFIX=$PREFIX" "TZ=$TZ" wine "$APP/qso_one.exe"
Type=Application
StartupNotify=true
Path=$APP/data
Icon=8915_qso_one.0
StartupWMClass=qso_one.exe
EOF

update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
echo "tzfix.sh: wrote $DESKTOP"
echo "tzfix.sh: launch 'QSO One (TZ fixed)' from your menu. Original entry untouched."
echo "tzfix.sh: to remove: rm '$DESKTOP'"
