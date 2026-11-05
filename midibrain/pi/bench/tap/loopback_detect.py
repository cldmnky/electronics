#!/usr/bin/env python3
"""Detect the bench tone in a raw UMC1820 capture (channel 1 of 18, S32_LE).

The tone is a 1 kHz sine; the loopback test is "is there 1 kHz energy on input 1
after playing a quiet 1 kHz burst on output 1". A Goertzel filter keeps this
dependency-free. Exit 0 = tone present, 1 = silence (no loopback cable).

usage: loopback_detect.py <rec.raw> [rate] [channels] [freq]
"""
import math
import struct
import sys

RATE = int(sys.argv[2]) if len(sys.argv) > 2 else 48000
CHANNELS = int(sys.argv[3]) if len(sys.argv) > 3 else 18
FREQ = float(sys.argv[4]) if len(sys.argv) > 4 else 1000.0
THRESHOLD_DBFS = -50.0


def main(path):
    with open(path, "rb") as handle:
        data = handle.read()
    frames = len(data) // (4 * CHANNELS)
    if frames < RATE // 100:
        print(f"capture too short: {frames} frames")
        return 1
    # channel 1 of every frame
    xs = [struct.unpack_from("<i", data, frame * 4 * CHANNELS)[0] / 2147483648.0
          for frame in range(frames)]
    k = 2.0 * 3.141592653589793 * FREQ / RATE
    coeff = 2.0 * math.cos(k)
    s1 = s2 = 0.0
    for x in xs:
        s0 = x + coeff * s1 - s2
        s2, s1 = s1, s0
    power = s1 * s1 + s2 * s2 - coeff * s1 * s2
    magnitude = (power ** 0.5) / max(frames, 1)
    dbfs = 20.0 * math.log10(max(magnitude, 1e-12))
    print(f"frames={frames} 1kHz_ch1_magnitude={magnitude:.3e} ({dbfs:.1f} dBFS)")
    return 0 if dbfs > THRESHOLD_DBFS else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
