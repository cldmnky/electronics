#!/bin/sh
# UMC1820 round-trip latency: TRS/DIN loopback on OUT1->IN1.
#
# GUARD FIRST. This refuses to run while any process owns the UMC PCM devices,
# because the bench rule is never to disturb an audio device another process is
# using (the operator's app or PipeWire may be live). It also never starts a
# measurement without a detected loopback: the presence test is a short quiet
# tone, and silence means "no loopback cable present" -> stop.
#
# Exit codes: 0 measured, 3 device busy, 4 no loopback signal.
set -eu
CARD=${CARD:-1}
TONE_MS=${TONE_MS:-200}
AMPLITUDE=${AMPLITUDE:-0.03}   # -30 dBFS: audible but quiet on studio monitors

busy=$(ssh magnus@studio "fuser /dev/snd/pcmC${CARD}D0p /dev/snd/pcmC${CARD}D0c 2>/dev/null" || true)
if [ -n "$busy" ]; then
  echo "BLOCKED: hw:${CARD},0 is held by pid(s):$busy" >&2
  ssh magnus@studio "fuser -v /dev/snd/pcmC${CARD}D0p /dev/snd/pcmC${CARD}D0c 2>&1" >&2 || true
  exit 3
fi

echo "hw:${CARD},0 is free; checking for a loopback cable with a ${TONE_MS} ms tone"
# 200 ms 1 kHz tone on output pair 1, then the same window recorded from input 1.
python3 - <<PY | ssh magnus@studio "cat > /tmp/midibrain-bench/tone.raw"
import math, struct, sys
n = int(48000 * ${TONE_MS} / 1000)
frames = b"".join(struct.pack("<i", int(${AMPLITUDE} * 2147483647 * math.sin(2*math.pi*1000*i/48000))) for i in range(n))
sys.stdout.buffer.write(frames * 20)  # 20 interleaved channels for the UMC
PY
ssh magnus@studio "
  cd /tmp/midibrain-bench &&
  arecord -D hw:${CARD},0 -c 18 -f S32_LE -r 48000 --period-size=128 --buffer-size=384 -d 1 -t raw rec.raw &
  sleep 0.2 && aplay -D hw:${CARD},0 -c 20 -f S32_LE -r 48000 --period-size=128 --buffer-size=384 -t raw tone.raw
  wait"
scp -q magnus@studio:/tmp/midibrain-bench/rec.raw /tmp/midibrain-bench-rec.raw
if ! python3 "$(cd "$(dirname "$0")/.." && pwd)/tap/loopback_detect.py" /tmp/midibrain-bench-rec.raw; then
  echo "no loopback cable present (no 1 kHz energy in input 1)" >&2
  exit 4
fi
echo "loopback detected; round-trip analysis still has to be added (sample offset + ms at 128-frame buffers)"
