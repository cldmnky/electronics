#!/bin/sh
# Automated MIDI-clock acceptance matrix against the real Pico + app.
#
# usage: sh run/06-clock-acceptance.sh [quick|full]
#   quick 180 s per tempo  (20/60/120/180/300 bpm) -> ~15 min
#   full   2 h per tempo                          -> ~10 h (owner's session)
#
# The app must be stopped: --bench-clock opens the real SPI link and the audio
# device. The driver writes no project or take, prints timestamped
# "[bench] MARK ... mono_us=..." lines for logic-analyser alignment and a
# results table shaped like the clock_sim matrix.
#
# SSH/nohup-safe (long full run):
#   ssh magnus@studio 'cd ~/src/midibrain/pi/bench && nohup sh run/06-clock-acceptance.sh full >/dev/null 2>&1 &'
# Then read the log path printed at start (default /tmp/midibrain-bench/).
set -eu
PROFILE=${1:-quick}
case "$PROFILE" in quick|full) ;; *) echo "usage: $0 [quick|full]" >&2; exit 2 ;; esac
BIN=${MNKYBOX_BIN:-$HOME/bin/mnkybox}
DIR=${MNKYBOX_BENCH_DIR:-/tmp/midibrain-bench}
STAMP=$(date +%Y%m%d-%H%M%S)
LOG=$DIR/clock-acceptance-$PROFILE-$STAMP.log
mkdir -p "$DIR"
if [ ! -x "$BIN" ]; then
  echo "mnkybox binary not found: $BIN (set MNKYBOX_BIN)" >&2
  exit 2
fi
if pgrep -x mnkybox >/dev/null 2>&1; then
  echo "mnkybox is already running; stop it first (SPI link/audio are exclusive)" >&2
  exit 3
fi
NICE=
if command -v nice >/dev/null 2>&1; then NICE="nice -n 10"; fi
if command -v ionice >/dev/null 2>&1; then NICE="$NICE ionice -c2 -n7"; fi
echo "clock-acceptance: profile=$PROFILE log=$LOG"
echo "clock-acceptance: markers are '[bench] MARK ... mono_us=<steady_clock_us> ...'"
# shellcheck disable=SC2086
exec $NICE "$BIN" --bench-clock "$PROFILE" >>"$LOG" 2>&1
