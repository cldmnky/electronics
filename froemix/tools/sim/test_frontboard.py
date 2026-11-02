#!/usr/bin/env python3
"""Run illustrative ngspice front-board regression models; not a hardware spec."""
import bisect
import math
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent
OUT = ROOT / 'out'


def run(deck):
    OUT.mkdir(exist_ok=True)
    (OUT / f'{deck}.dat').unlink(missing_ok=True)  # never reuse stale output
    subprocess.run(['ngspice', '-b', '-o', str(OUT / f'{deck}.log'), f'{deck}.cir'],
                   cwd=ROOT, check=True, stdout=subprocess.DEVNULL)
    with (OUT / f'{deck}.dat').open() as f:
        next(f)
        return [list(map(float, line.split())) for line in f if line.strip()]


def interpolate(rows, t, col):
    times = [r[0] for r in rows]
    i = bisect.bisect_left(times, t)
    if i == 0 or i == len(rows):
        raise ValueError(f'sample {t} outside simulation')
    a, b = rows[i-1], rows[i]
    return a[col] + (b[col] - a[col]) * (t-a[0]) / (b[0]-a[0])


def thd(rows, col):
    # Uniform samples over exactly 10 settled 1 kHz periods; 2nd–10th harmonics.
    samples = [interpolate(rows, .010 + i * .010 / 2048, col) for i in range(2048)]
    def amplitude(h):
        c = sum(v * math.cos(2*math.pi*h*10*i/len(samples)) for i, v in enumerate(samples))
        s = sum(v * math.sin(2*math.pi*h*10*i/len(samples)) for i, v in enumerate(samples))
        return math.hypot(c, s)
    fundamental = amplitude(1)
    return 100 * math.sqrt(sum(amplitude(h)**2 for h in range(2, 11))) / fundamental


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def main():
    audio = run('front_audio')
    values = [thd(audio, i) for i in range(1, 5)]
    print('audio THD (ideal sources, harmonics 2–10): output new/old '
          f'{values[0]:.4f}% / {values[1]:.4f}%; input new/old {values[2]:.4f}% / {values[3]:.4f}%')
    require(values[0] < .02 and values[2] < .02, 'unloaded audio path is unexpectedly nonlinear')
    require(values[1] > 1 and values[3] > 1, 'legacy branch failed to demonstrate nonlinear loading')
    mode = run('front_mode')
    released = interpolate(mode, 0.9e-6, 1)
    pressed = interpolate(mode, 10e-6, 2)
    current_pressed = (3.3 - interpolate(mode, 10e-6, 6)) / 1000
    current_released = (3.3 - interpolate(mode, .9e-6, 5)) / 1000
    sample = interpolate(mode, 10e-6, 4)
    print(f'MODE: released={released:.5f} V, pressed={pressed:.6f} V, '
          f'LED pressed/released={current_pressed*1000:.4f}/{current_released*1000:.6f} mA, '
          f'ADC sample at 9 us after connect={sample:.5f} V')
    require(3.27 < released < 3.31, 'released node outside modeled 1uA leakage range')
    require(pressed < .01, 'pressed node too high')
    require(.0002 < current_pressed < .003 and abs(current_released) < .000001, 'LED current outside illustrative range')
    require(abs(sample - released) < .01, 'sample did not settle within 10mV for illustrative RC')
    cv = run('front_cv_led')
    cv_bare = interpolate(cv, .0008, 1)
    cv_led = interpolate(cv, .0008, 2)
    led_current = (cv_led - interpolate(cv, .0008, 4)) / 2200
    peak = max(row[2] for row in cv if .0001 < row[0] < .0008)
    print(f'CV 10nF: bare/LED={cv_bare:.5f}/{cv_led:.5f} V, '
          f'LED current={led_current*1000:.4f} mA, peak={peak:.5f} V')
    require(abs(cv_led - cv_bare) < .01 and 4.9 < cv_led < 5.1, 'CV LED altered the modeled DC output')
    require(.0001 < led_current < .003, 'CV LED current outside illustrative range')
    require(peak < 5.2, 'CV LED transient exceeds illustrative overshoot bound')
    print('PASS: illustrative front-board SPICE checks (not hardware qualification)')


if __name__ == '__main__':
    try:
        main()
    except (AssertionError, ValueError, subprocess.CalledProcessError, FileNotFoundError) as exc:
        print(f'FAIL: {exc}', file=sys.stderr)
        sys.exit(1)
