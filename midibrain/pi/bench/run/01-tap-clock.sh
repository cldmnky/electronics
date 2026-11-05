#!/bin/sh
# Capture MIDI events with the read-only sequencer tap and analyse clock jitter.
# usage: sh run/01-tap-clock.sh [seconds]   (default 600, hard cap for one call)
set -eu
SECONDS_TO_RUN=${1:-600}
HOST=magnus@studio
WORK=/tmp/midibrain-bench/tap
HERE=$(cd "$(dirname "$0")/.." && pwd)
rsync -az --delete "$HERE/tap" "$HOST:/tmp/midibrain-bench/"
ssh "$HOST" "cd $WORK && gcc -O2 -Wall -o midi_seqtap midi_seqtap.c -lasound &&
  ./midi_seqtap $SECONDS_TO_RUN capture.csv > capture.stderr 2>&1"
TAG="logs/$(date +%Y%m%d-%H%M%S)-tap-${SECONDS_TO_RUN}s"
scp -q "$HOST:$WORK/capture.csv" "$TAG.csv"
scp -q "$HOST:$WORK/capture.stderr" "$TAG-topology.log"
python3 "$HERE/tap/clock_jitter.py" "$TAG.csv" --json "$TAG.json" > "$TAG-summary.txt"
gzip -9 -f "$TAG.csv"   # analysis reads the plain CSV; only the gzip is kept
echo "wrote $TAG-summary.txt"
