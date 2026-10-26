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

if [ "$(id -u)" = 0 ]; then
  echo "install.sh: do not run as root/sudo — Wine prefixes live in your" >&2
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
  echo "install.sh: searching for qso_one.exe under $HOME ..."
  APP="$(
    find "$HOME" -maxdepth 12 -iname qso_one.exe 2>/dev/null |
    while IFS= read -r exe; do
      d="$(dirname "$exe")"
      if is_app_dir "$d"; then printf '%s\n' "$d"; break; fi
    done
  )"
fi

# Last resort: ask for the path; an empty answer means QSO One is not
# installed yet, so explain that and stop.
if [ -z "$APP" ]; then
  echo "install.sh: could not find an installed copy of QSO One."
  printf "Enter the folder containing qso_one.exe, or press Enter to abort: "
  read -r APP 2>/dev/null < /dev/tty || APP=""
  if [ -z "$APP" ]; then
    cat <<'EOF'

QSO One must be installed under Wine before running this script:
  1. Download the QSO One Windows installer from the QSO One website.
  2. Install it with Wine, e.g.:  wine ~/Downloads/<installer-file>.exe
  3. Re-run this script.
EOF
    exit 1
  fi
fi

is_app_dir "$APP" || {
  echo "install.sh: '$APP' does not look like a QSO One installation" >&2
  echo "  (expected qso_one.exe plus flutter_windows.dll/data in that folder)" >&2
  exit 1
}
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
