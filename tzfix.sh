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

if [ "$(id -u)" = 0 ]; then
  echo "tzfix.sh: do not run as root/sudo — Wine prefixes live in your" >&2
  echo "  user home. Run again without sudo." >&2
  exit 1
fi

# A real QSO One install has the app's Flutter runtime and data folder next
# to qso_one.exe. This tells it apart from a stray qso_one.exe such as a
# downloaded-but-never-run installer.
is_app_dir() {
  [ -f "$1/qso_one.exe" ] &&
  { [ -f "$1/flutter_windows.dll" ] || [ -d "$1/data" ]; }
}

APP=""
try_dir() {
  if [ -z "$APP" ] && is_app_dir "$1"; then APP="$1"; fi
}

# Check drive_c of every likely Wine prefix, in the places installers put it.
for base in \
  ${WINEPREFIX:+"$WINEPREFIX"} \
  "$HOME/.wine" \
  "$HOME"/.local/share/wineprefixes/* \
  "$HOME"/.PlayOnLinux/wineprefix/* \
  "$HOME"/.var/app/com.usebottles.bottles/data/bottles/bottles/*
do
  [ -d "$base/drive_c" ] || continue
  for sub in \
    "Program Files/QSO One" \
    "Program Files/QSOOne" \
    "Program Files (x86)/QSO One" \
    "Program Files (x86)/QSOOne"
  do
    try_dir "$base/drive_c/$sub"
  done
  # Per-user installs land under %LOCALAPPDATA%\Programs.
  for progs in "$base"/drive_c/users/*/AppData/Local/Programs; do
    [ -d "$progs" ] || continue
    try_dir "$progs/QSO One"
    try_dir "$progs/QSOOne"
  done
done

# Fall back to a full search of the home directory (also finds portable or
# extracted copies). Only directories that look like a real install count.
if [ -z "$APP" ]; then
  echo "tzfix.sh: searching for qso_one.exe under $HOME ..."
  APP="$(
    find "$HOME" -maxdepth 12 -iname qso_one.exe 2>/dev/null |
    while IFS= read -r exe; do
      d="$(dirname "$exe")"
      if is_app_dir "$d"; then printf '%s\n' "$d"; break; fi
    done
  )"
fi

if [ -z "$APP" ]; then
  cat <<'EOF' >&2
tzfix.sh: could not find an installed copy of QSO One.

QSO One must be installed under Wine first:
  1. Download the QSO One Windows installer from the QSO One website.
  2. Install it with Wine, e.g.:  wine ~/Downloads/<installer-file>.exe
  3. Re-run this script.
EOF
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
