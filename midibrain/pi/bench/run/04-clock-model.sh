#!/bin/sh
# Build the Pi scheduling + UsbClock model harness and run all three modes.
# usage: sh run/04-clock-model.sh [seconds]   (default 60 per mode)
set -eu
SECONDS_TO_RUN=${1:-60}
HOST=magnus@studio
WORK=/tmp/midibrain-bench/model
HERE=$(cd "$(dirname "$0")/.." && pwd)
rsync -az --delete "$HERE/model" "$HOST:/tmp/midibrain-bench/"
rsync -az "$HERE/../midi/clock.cpp" "$HERE/../midi/clock.hpp" "$HOST:$WORK/"
ssh "$HOST" "cd $WORK && g++ -O2 -std=c++17 -Wall -o clock_model clock_model.cpp clock.cpp &&
  ./clock_model sched $SECONDS_TO_RUN && ./clock_model freerun $SECONDS_TO_RUN &&
  ./clock_model pico $SECONDS_TO_RUN"
