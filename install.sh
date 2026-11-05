#!/bin/sh
# QSO One Wine fixes installer — everything in one step.
#
#   curl -fsSL https://raw.githubusercontent.com/GM4GLD/QSO-one-wine/main/install.sh | sh
#
# Run after installing (or updating) QSO One under Wine. It:
#   * installs mf.dll next to qso_one.exe (microphone fix)
#   * replaces flutter_local_notifications_windows.dll with a stub
#     (crash fix; the original is backed up as .disabled)
#   * installs icu.dll (needed by flutter_timezone_plugin.dll on Wine
#     builds that don't ship it — harmless where Wine already does)
#   * writes a "QSO One (TZ fixed)" menu launcher that starts the app
#     with TZ set (works around ~60-70% idle CPU burn under Wine)
#   * hides the Wine-generated "QSO One" menu entry so only the
#     TZ-fixed launcher appears
#
# Every QSO One install directory found (including per-user installs
# under drive_c/users/*) gets the DLLs. Re-run after every update.

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

# Newline-separated list of every install dir found.
APP_DIRS=""
add_dir() {
  is_app_dir "$1" || return 0
  printf '%s\n' "$APP_DIRS" | grep -qxF "$1" && return 0
  APP_DIRS="${APP_DIRS:+$APP_DIRS
}$1"
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
    "Program Files/QSOone" \
    "Program Files (x86)/QSO One" \
    "Program Files (x86)/QSOOne" \
    "Program Files (x86)/QSOone"
  do
    add_dir "$base/drive_c/$sub"
  done
  # Per-user installs land under %LOCALAPPDATA%\Programs.
  for progs in "$base"/drive_c/users/*/AppData/Local/Programs; do
    [ -d "$progs" ] || continue
    add_dir "$progs/QSO One"
    add_dir "$progs/QSOOne"
    add_dir "$progs/QSOone"
  done
done

# Fall back to a search of the home directory (also finds portable or
# extracted copies). Only directories that look like a real install count.
if [ -z "$APP_DIRS" ]; then
  echo "install.sh: searching for qso_one.exe under $HOME ..."
  APP_DIRS="$(
    find "$HOME" -maxdepth 12 -iname qso_one.exe 2>/dev/null |
    while IFS= read -r exe; do
      d="$(dirname "$exe")"
      if is_app_dir "$d"; then printf '%s\n' "$d"; break; fi
    done
  )"
fi

# Last resort: ask for the path; an empty answer means QSO One is not
# installed yet, so explain that and stop.
if [ -z "$APP_DIRS" ]; then
  echo "install.sh: could not find an installed copy of QSO One."
  printf "Enter the folder containing qso_one.exe, or press Enter to abort: "
  read -r APP_DIRS 2>/dev/null < /dev/tty || APP_DIRS=""
  if [ -z "$APP_DIRS" ]; then
    cat <<'EOF'

QSO One must be installed under Wine before running this script:
  1. Download the QSO One Windows installer from the QSO One website.
  2. Install it with Wine, e.g.:  wine ~/Downloads/<installer-file>.exe
  3. Re-run this script.
EOF
    exit 1
  fi
fi

# Sanity-check every dir we're about to touch.
printf '%s\n' "$APP_DIRS" | while IFS= read -r d; do
  is_app_dir "$d" && continue
  echo "install.sh: '$d' does not look like a QSO One installation" >&2
  echo "  (expected qso_one.exe plus flutter_windows.dll/data in that folder)" >&2
  exit 1
done

DL="$(mktemp -d)"
curl -fsSL "$REPO/notif-stub/flutter_local_notifications_windows.dll" -o "$DL/fln.dll"

printf '%s\n' "$APP_DIRS" | while IFS= read -r APP; do
  [ -n "$APP" ] || continue
  echo "install.sh: installing to $APP"

  curl -fsSL "$REPO/mf-shim/mf.dll" -o "$APP/mf.dll"
  echo "  installed mf.dll                            (microphone fix)"

  NOTIF="$APP/flutter_local_notifications_windows.dll"
  if [ -f "$NOTIF" ] && ! cmp -s "$NOTIF" "$DL/fln.dll" && [ ! -f "$NOTIF.disabled" ]; then
    mv "$NOTIF" "$NOTIF.disabled"
    echo "  backed up original plugin -> flutter_local_notifications_windows.dll.disabled"
  fi
  cp "$DL/fln.dll" "$NOTIF"
  echo "  installed flutter_local_notifications_windows.dll (notification crash fix)"

  curl -fsSL "$REPO/icu/icu.dll" -o "$APP/icu.dll"
  echo "  installed icu.dll                           (timezone plugin support)"
done
rm -rf "$DL"

# --- TZ launcher ---------------------------------------------------------

# Use the first install dir found for the launcher and prefix detection.
APP="$(printf '%s\n' "$APP_DIRS" | sed -n '1p')"

# Derive the Wine prefix from the app path (drive_c parent); default ~/.wine.
case "$APP" in
  */drive_c/*) PREFIX="${APP%%/drive_c/*}" ;;
  *)           PREFIX="$HOME/.wine" ;;
esac

# Detect the local IANA timezone name.
TZ=""
if [ -L /etc/localtime ]; then
  TZ="$(readlink /etc/localtime | sed -n 's|.*/zoneinfo/||p')"
fi
if [ -z "$TZ" ] && command -v timedatectl >/dev/null 2>&1; then
  TZ="$(timedatectl show -p Timezone --value 2>/dev/null || true)"
fi
[ -z "$TZ" ] && TZ="UTC0"

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
echo "install.sh: wrote $DESKTOP (TZ=$TZ, prefix=$PREFIX)"

# Hide the Wine-generated "QSO One" launcher so only the TZ-fixed
# launcher appears in the menu (the "Uninstall QSO One" entry is left
# alone). Hidden=true makes an entry count as deleted; Wine re-creates
# these files on updates, so re-run this script to re-hide.
#
# Only the applications/wine subtree is scanned — it holds the Start-Menu
# launchers Wine regenerates from .lnk files. Top-level wine-* files
# (file-association and protocol handlers such as qsoone://) must be
# left alone. Match only a Name= of exactly "QSO One", or qso_one.exe
# in Exec= — never whole-file content, since a WINEPREFIX path may
# itself be named e.g. "qso-one" and produce false positives.
if [ -d "$DESKTOP_DIR/wine" ]; then
find "$DESKTOP_DIR/wine" -type f -name '*.desktop' \
  ! -name 'qso-one-fixed.desktop' 2>/dev/null |
while IFS= read -r f; do
  grep -qiE '^Name=[[:space:]]*qso[ _-]*one[[:space:]]*$|^Exec=.*qso_one\.exe' "$f" || continue
  if grep -qE '^(Hidden|NoDisplay)=' "$f"; then
    sed -i -e 's/^Hidden=.*/Hidden=true/' -e 's/^NoDisplay=.*/Hidden=true/' "$f"
  else
    sed -i '/^\[Desktop Entry\]/a Hidden=true' "$f"
  fi
  echo "install.sh: hid $f"
done
fi

# Register qsoone:// so "open in QSO One" links on the website hand off
# to the app under Wine. QSO One registers the scheme inside the prefix
# and `wine start` resolves it there. Wine also generates
# wine-protocol-qsoone.desktop, but nothing makes it the default
# handler — so we ship our own (which also gets the TZ fix) and set it.
cat > "$DESKTOP_DIR/qso-one-url.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=QSO One URL handler (Wine)
Comment=Pass qsoone:// links to QSO One running under Wine
Exec=env "WINEPREFIX=$PREFIX" "TZ=$TZ" wine start %u
MimeType=x-scheme-handler/qsoone;
NoDisplay=true
EOF
echo "install.sh: wrote $DESKTOP_DIR/qso-one-url.desktop (qsoone:// handler)"

if command -v xdg-mime >/dev/null 2>&1; then
  xdg-mime default qso-one-url.desktop x-scheme-handler/qsoone || true
fi
# Also write the association into mimeapps.list directly — covers setups
# without xdg-mime and desktops that read a different file than the one
# xdg-mime happens to write (e.g. ~/.config vs applications/).
MIMEAPPS="$DESKTOP_DIR/mimeapps.list"
touch "$MIMEAPPS"
if grep -q '^x-scheme-handler/qsoone=' "$MIMEAPPS"; then
  sed -i 's|^x-scheme-handler/qsoone=.*|x-scheme-handler/qsoone=qso-one-url.desktop|' "$MIMEAPPS"
elif grep -q '^\[Default Applications\]' "$MIMEAPPS"; then
  sed -i '/^\[Default Applications\]/a x-scheme-handler/qsoone=qso-one-url.desktop' "$MIMEAPPS"
else
  printf '[Default Applications]\nx-scheme-handler/qsoone=qso-one-url.desktop\n' >> "$MIMEAPPS"
fi

update-desktop-database "$DESKTOP_DIR" 2>/dev/null || true
echo "install.sh: done. Launch 'QSO One (TZ fixed)' from your menu."
