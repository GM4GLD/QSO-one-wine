#!/bin/sh
# auto-update.sh — QSO One update watcher for Wine.
#
# QSO One's built-in updater downloads QSOOneSetup_v<ver>.exe into the Wine
# %TEMP% dir, then refuses to keep working until the update is applied —
# and applying it by hand undoes the Wine fixes from install.sh.
#
# This watcher does not check for or download updates itself: the user
# still presses Download inside QSO One. It polls %TEMP% and, when that
# download finishes:
#
#   1. closes QSO One
#   2. runs the NSIS installer silently   (wine QSOOneSetup_vNNN.exe /S)
#   3. re-runs install.sh to reapply the Wine fixes
#   4. relaunches QSO One via the "QSO One (TZ fixed)" desktop entry
#
# It is installed and started by install.sh only after the user opts in,
# via ~/.config/autostart/qso-one-wine-updater.desktop, so it runs inside
# the desktop session with DISPLAY/DBus available for the relaunch.
#
# Usage:
#   auto-update.sh          watch for the next download
#   auto-update.sh --now    first apply the newest already-downloaded update
#
# State and logs: ${XDG_STATE_HOME:-~/.local/state}/qso-one-wine/
# Needs only POSIX sh, curl, wine, GNU coreutils/findutils.

set -u
PATH="$PATH:/usr/local/bin:/usr/bin:/bin"

REPO="${QSO_WINE_REPO:-https://raw.githubusercontent.com/GM4GLD/QSO-one-wine/main}"
STATE="${XDG_STATE_HOME:-$HOME/.local/state}/qso-one-wine"
PROCESSED="$STATE/processed"
LOGFILE="$STATE/auto-update.log"
POLL=3

mkdir -p "$STATE"
touch "$PROCESSED"

log()  { printf '%s %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*" | tee -a "$LOGFILE"; }
note() { command -v notify-send >/dev/null 2>&1 && notify-send "QSO One auto-update" "$1"; return 0; }

# Single instance.
exec 9>"$STATE/lock"
if command -v flock >/dev/null 2>&1 && ! flock -n 9; then
  echo "auto-update.sh: already running" >&2
  exit 0
fi

# Every Wine %TEMP% dir in the prefixes install.sh knows about.
temp_dirs() {
  for base in \
    ${WINEPREFIX:+"$WINEPREFIX"} \
    "$HOME/.wine" \
    "$HOME"/.local/share/wineprefixes/* \
    "$HOME"/.PlayOnLinux/wineprefix/* \
    "$HOME"/.var/app/com.usebottles.bottles/data/bottles/bottles/*
  do
    [ -d "$base/drive_c" ] || continue
    for t in "$base"/drive_c/users/*/AppData/Local/Temp; do
      [ -d "$t" ] && printf '%s\n' "$t"
    done
  done
}

# Newest not-yet-handled QSOOneSetup_v*.exe, or empty.
detect() {
  temp_dirs | while IFS= read -r t; do
    find "$t" -maxdepth 1 -name 'QSOOneSetup_v*.exe' -printf '%T@ %p\n' 2>/dev/null
  done | sort -rn | cut -d' ' -f2- | while IFS= read -r f; do
    grep -qxF "$f" "$PROCESSED" || { printf '%s\n' "$f"; break; }
  done
}

# Returns once the file size is unchanged for two polls (download done),
# or fails if the file disappears.
wait_stable() {
  _prev=-1; _same=0
  while [ "$_same" -lt 2 ]; do
    _size=$(stat -c %s "$1" 2>/dev/null) || return 1
    if [ "$_size" = "$_prev" ]; then
      _same=$((_same + 1))
    else
      _same=0; _prev=$_size
    fi
    sleep "$POLL"
  done
}

close_qso() {
  pgrep -f 'qso_one\.exe' >/dev/null 2>&1 || return 0
  log "closing QSO One"
  WINEPREFIX="$1" wine taskkill /IM qso_one.exe >/dev/null 2>&1 || true
  _i=0
  while pgrep -f 'qso_one\.exe' >/dev/null 2>&1; do
    [ "$_i" -eq 10 ] && pkill -f 'qso_one\.exe'
    [ "$_i" -eq 15 ] && pkill -9 -f 'qso_one\.exe'
    [ "$_i" -ge 20 ] && return 0
    _i=$((_i + 1)); sleep 1
  done
}

relaunch() {
  if command -v gtk-launch >/dev/null 2>&1 && gtk-launch qso-one-fixed; then
    log "relaunched via 'QSO One (TZ fixed)'"
    return 0
  fi
  _d="$HOME/.local/share/applications/qso-one-fixed.desktop"
  if [ -f "$_d" ] && command -v gio >/dev/null 2>&1 && gio launch "$_d"; then
    log "relaunched via 'QSO One (TZ fixed)'"
    return 0
  fi
  log "WARNING: could not relaunch — start 'QSO One (TZ fixed)' from the menu"
  return 1
}

apply() {
  _exe=$1
  _prefix=${_exe%%/drive_c/*}
  log "update found: $_exe"
  note "QSO One update downloaded — installing silently..."
  if ! wait_stable "$_exe"; then
    log "installer vanished before finishing; skipping"
    return 0
  fi
  close_qso "$_prefix"
  log "running: WINEPREFIX=$_prefix wine '$_exe' /S"
  WINEPREFIX="$_prefix" wine "$_exe" /S >>"$LOGFILE" 2>&1
  sleep 3   # let NSIS helper processes settle
  log "reapplying Wine fixes"
  if curl -fsSL "$REPO/install.sh" | QSO_WINE_NO_UPDATER=1 sh >>"$LOGFILE" 2>&1; then
    log "fixes reapplied"
  else
    log "WARNING: install.sh re-run failed — run it by hand"
  fi
  relaunch
  note "QSO One updated (${_exe##*/}) and relaunched"
  log "done: ${_exe##*/}"
}

# Baseline: installers already sitting in %TEMP% when the watcher starts
# were downloaded before it ran — usually an already-applied update — so
# they are marked processed and never installed over the current version.
# --now forces the newest of them to be applied first (e.g. right after
# the in-app download finishes).
if [ "${1:-}" = "--now" ]; then
  _f=$(detect)
  if [ -n "$_f" ]; then
    apply "$_f"
    printf '%s\n' "$_f" >>"$PROCESSED"
  else
    log "--now: no downloaded installer found"
  fi
fi
temp_dirs | while IFS= read -r t; do
  find "$t" -maxdepth 1 -name 'QSOOneSetup_v*.exe' 2>/dev/null
done >>"$PROCESSED"
sort -u -o "$PROCESSED" "$PROCESSED" 2>/dev/null || true

log "watching for QSO One update downloads (poll ${POLL}s)..."
while :; do
  _f=$(detect)
  if [ -n "$_f" ]; then
    apply "$_f"
    printf '%s\n' "$_f" >>"$PROCESSED"
  fi
  sleep "$POLL"
done
