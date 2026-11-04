#!/usr/bin/env python3
"""Run one froemix analog deck and read its output strictly.

Every deck is copied into a private run directory, so a result can never be
read from a stale file: the run directory is the only place ``run_deck`` looks.
Reading is strict on purpose -- a missing ``wrdata`` file, an empty file, a
ragged row, a NaN/Inf value, a missing ``.meas`` name or an ngspice error line
raises ``SimError``. The checks in ``run_checks.py`` turn that into a failure,
so the tools cannot print a table and exit 0 on bad or absent data.

What this module does NOT do: it does not turn a simulation into a
measurement of the built board. The decks are transcriptions of the tscircuit
source (``source_link.py`` proves that), and the device models are behavioural
(``models.py`` records their provenance). See RESULTS.md.

Configuration: ``ngspice`` must be on PATH or given with ``--ngspice``. A
native ngspice prints harmless host-library warnings on stderr (for example
``libtinfo.so.6: no version information available``); they are kept in the run
directory next to the deck but never treated as a design error.
"""

from __future__ import annotations

import bisect
import math
import os
import re
import shutil
import subprocess
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

HERE = Path(__file__).resolve().parent

# One deck = one circuit; the name is the file stem.
DECKS = (
    "audio_in",
    "audio_out",
    "cv_inout",
    "ref_bias",
    "sync_io",
    "mux_adc",
    "front_audio",
    "front_cv_led",
    "front_mode",
    "midi_in",
    "midi_out",
)


class SimError(RuntimeError):
    """A simulation or its output cannot be trusted; callers must fail."""


# --------------------------------------------------------------------------
# running ngspice
# --------------------------------------------------------------------------

NGSPICE_ENV = "NGSPICE"


def find_ngspice(explicit: str | None = None) -> str:
    """Path to a native ngspice, or raise SimError with an install hint."""
    candidate = explicit or os.environ.get(NGSPICE_ENV)
    if candidate:
        path = shutil.which(candidate) or (candidate if os.path.isfile(candidate) else None)
        if not path:
            raise SimError(f"ngspice not found at {candidate!r} ({NGSPICE_ENV})")
        return path
    path = shutil.which("ngspice")
    if not path:
        raise SimError(
            "ngspice not found on PATH -- the analog decks cannot be verified. "
            "Install it (Debian/Ubuntu: `apt install ngspice`, macOS: "
            "`brew install ngspice`) or pass --ngspice PATH. This check fails "
            "instead of skipping, because an absent simulator must never look "
            "like a passed simulation."
        )
    return path


@dataclass
class DeckRun:
    """One deck executed in its own directory."""

    deck: str
    workdir: Path
    log: Path
    stderr: Path
    returncode: int
    measures: dict[str, float] = field(default_factory=dict)

    def dat(self, name: str, layout: str = "pairs") -> tuple[list[float], list[list[float]]]:
        return read_wrdata(self.workdir / "out" / f"{name}.dat", layout=layout)

    def measure(self, name: str) -> float:
        if name not in self.measures:
            raise SimError(
                f"{self.deck}: the log has no .meas result {name!r} "
                f"(found: {', '.join(sorted(self.measures)) or 'none'}) -- "
                "a required measurement is missing"
            )
        value = self.measures[name]
        if not math.isfinite(value):
            raise SimError(f"{self.deck}: .meas {name!r} is not a finite number: {value!r}")
        return value


def _title_guard(path: Path) -> None:
    """SPICE treats the first line as the title; an element there is lost."""
    first = ""
    with path.open() as handle:
        for line in handle:
            if line.strip():
                first = line
                break
    if not first.startswith("*"):
        raise SimError(
            f"{path.name}: the first line must be a comment (it is the SPICE "
            f"title, so anything else is silently dropped): {first.rstrip()!r}"
        )


def log_errors(text: str) -> list[str]:
    """ngspice error lines (warnings are not errors)."""
    problems = []
    for line in text.splitlines():
        stripped = line.strip()
        if stripped.startswith("Error") or "Fatal error" in stripped:
            problems.append(stripped)
        elif re.search(r"measure .* failed|failed!$", stripped):
            problems.append(stripped)
    return problems


def read_measures(text: str) -> dict[str, float]:
    """``.meas`` results from an ngspice log: name -> value."""
    found: dict[str, float] = {}
    pattern = re.compile(r"^\s{0,4}([A-Za-z_][\w.]*)\s+=\s+(\S+)")
    for line in text.splitlines():
        match = pattern.match(line)
        if not match:
            continue
        try:
            found[match.group(1)] = float(match.group(2))
        except ValueError:
            continue
    return found


def run_deck(deck: str | Path, workdir: str | Path, ngspice: str | None = None,
             timeout: float = 900.0) -> DeckRun:
    """Copy ``deck`` into ``workdir``, run it there and return the outputs."""
    deck_path = Path(deck)
    if not deck_path.is_absolute():
        deck_path = HERE / deck_path
    if not deck_path.is_file():
        raise SimError(f"deck not found: {deck_path}")
    _title_guard(deck_path)

    work = Path(workdir)
    (work / "out").mkdir(parents=True, exist_ok=True)
    local = work / deck_path.name
    shutil.copyfile(deck_path, local)

    exe = find_ngspice(ngspice)
    log = work / f"{deck_path.stem}.log"
    stderr = work / f"{deck_path.stem}.stderr"
    try:
        proc = subprocess.run(
            [exe, "-b", "-o", str(log), local.name],
            cwd=work, capture_output=True, text=True, timeout=timeout,
        )
    except subprocess.TimeoutExpired as exc:
        raise SimError(f"{deck_path.stem}: ngspice timed out after {timeout:g}s") from exc
    stderr.write_text(proc.stderr or "")
    if not log.is_file():
        raise SimError(
            f"{deck_path.stem}: ngspice wrote no log (exit {proc.returncode}); "
            f"stderr: {(proc.stderr or '').strip()[-400:]}"
        )
    text = log.read_text(errors="replace")
    errors = log_errors(text)
    if errors:
        raise SimError(f"{deck_path.stem}: ngspice reported {len(errors)} error line(s): "
                       + " | ".join(errors[:4]))
    if proc.returncode != 0:
        tail = " | ".join(text.splitlines()[-3:])
        raise SimError(
            f"{deck_path.stem}: ngspice exited {proc.returncode} (a deck must end its "
            f".control block with `quit`): {tail}"
        )
    if "Circuit:" not in text:
        raise SimError(f"{deck_path.stem}: the log does not show a parsed circuit")
    return DeckRun(deck=deck_path.stem, workdir=work, log=log, stderr=stderr,
                   returncode=proc.returncode, measures=read_measures(text))


def private_workdir(tag: str = "run", root: str | Path | None = None) -> Path:
    """Fresh, uniquely named directory for one deck's run.

    Default: below ``$TMPDIR/froemix-sim/``. With ``root``: a new unique child
    of that root (created if it does not exist). Either way the directory is
    NEW and nothing that already exists is removed -- a caller-supplied
    ``--workdir`` may hold anything, so the runner must never delete it. The
    run directory is kept afterwards so a failure can be triaged (log, stderr
    and raw data are all inside).
    """
    if root is None:
        base = Path(os.environ.get("TMPDIR", tempfile.gettempdir())) / "froemix-sim"
        base.mkdir(parents=True, exist_ok=True)
        return Path(tempfile.mkdtemp(prefix=f"{tag}-", dir=base))
    base = Path(root)
    if base.exists() and not base.is_dir():
        raise SimError(f"--workdir {base} exists and is not a directory")
    base.mkdir(parents=True, exist_ok=True)
    return Path(tempfile.mkdtemp(prefix=f"{tag}-", dir=base))


# --------------------------------------------------------------------------
# SPICE numbers
# --------------------------------------------------------------------------

SPICE_SUFFIX = {
    "t": 1e12, "g": 1e9, "meg": 1e6, "k": 1e3, "m": 1e-3,
    "u": 1e-6, "µ": 1e-6, "n": 1e-9, "p": 1e-12, "f": 1e-15, "": 1.0,
}
# letters ngspice ignores after the number (a unit designator)
UNIT_LETTERS = ("ohm", "ohms", "f", "h", "r", "v", "a", "s")


def spice_number(text: str, where: str = "") -> float:
    """SPICE value text -> SI float, or raise SimError.

    Understands the SPICE suffixes (``meg`` is 1e6, ``m`` is 1e-3) and ignores
    a trailing unit designator. ``1M`` is REJECTED: in SPICE it means one
    milliohm, while the schematic means one megohm, so an ``M`` literal is a
    silent value error -- write ``1Meg``.
    """
    raw = text.strip().strip("{}").strip()
    match = re.match(r"^([-+]?\d+(?:\.\d+)?(?:[eE][-+]?\d+)?)([A-Za-zµ]*)$", raw)
    if not match:
        raise SimError(f"{where}: cannot read {text!r} as a SPICE value")
    number, suffix = match.group(1), match.group(2)
    lowered = suffix.lower()
    head = lowered[:-1] if lowered.endswith(UNIT_LETTERS) else lowered
    if head in ("m",):
        if suffix.startswith("M"):
            raise SimError(
                f"{where}: value {text!r} is ambiguous -- SPICE reads a bare M as "
                "milli (1e-3), the schematic means mega. Write Meg."
            )
        head = "m"
    if head == "meg":
        pass
    elif head not in SPICE_SUFFIX:
        raise SimError(f"{where}: unknown SPICE suffix in {text!r}")
    return float(number) * SPICE_SUFFIX[head]


def check_deck_values(path: str | Path) -> list[str]:
    """Reject silently misread values in a deck (the bare-M trap)."""
    problems: list[str] = []
    for number, line in enumerate(Path(path).read_text().splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith(("*", ".")) or stripped.startswith("+"):
            continue
        first = stripped.split()[0].upper()
        if first[0] not in "RCL":
            continue
        fields = stripped.split()
        value = None
        for field in reversed(fields[1:]):
            if re.match(r"^[-+]?\d", field):
                value = field
                break
        if value is None:
            continue
        try:
            spice_number(value, f"{Path(path).name}:{number}")
        except SimError as exc:
            problems.append(str(exc))
    return problems


# --------------------------------------------------------------------------
# reading wrdata files
# --------------------------------------------------------------------------

def read_wrdata(path: str | Path, layout: str = "pairs") -> tuple[list[float], list[list[float]]]:
    """Return (x, [vector, ...]) from a ``wrdata`` file, or raise SimError.

    ``layout="pairs"``: every vector repeats the scale column
    (``x v0 x v1 ...``), which is what the base decks write.
    ``layout="vectors"``: one shared scale column followed by the vectors
    (``set wr_singlescale`` plus an optional ``wr_vecnames`` header).

    Rejects a missing/empty file, a header-only file, non-numeric tokens,
    ragged rows and any NaN/Inf value.
    """
    path = Path(path)
    if not path.is_file():
        raise SimError(f"missing simulation output: {path.name} ({path}) -- was the "
                       "wrdata line removed from the deck?")
    if path.stat().st_size == 0:
        raise SimError(f"empty simulation output: {path.name}")
    if layout not in ("pairs", "vectors"):
        raise SimError(f"unknown wrdata layout {layout!r}")

    rows: list[list[float]] = []
    width = None
    for number, line in enumerate(path.read_text().splitlines(), start=1):
        fields = line.split()
        if not fields:
            continue
        try:
            values = [float(field) for field in fields]
        except ValueError as exc:
            # only a header line may be non-numeric, and only as line 1
            if number == 1 and rows == [] and not re.match(r"^\s*[-+]?\d", line):
                continue
            raise SimError(f"{path.name}: line {number} is not numeric: {line.strip()!r}") from exc
        if width is None:
            width = len(values)
        elif len(values) != width:
            raise SimError(f"{path.name}: row {number} has {len(values)} columns, "
                           f"the first row has {width}")
        for value in values:
            if not math.isfinite(value):
                raise SimError(f"{path.name}: row {number} holds a non-finite value "
                               f"({value!r}); a failed simulation is not a result")
        rows.append(values)

    if not rows:
        raise SimError(f"{path.name}: no data rows (header only)")
    if width is None or width < 2:
        raise SimError(f"{path.name}: {width} column(s), need at least a scale and one vector")

    if layout == "pairs":
        if width % 2:
            raise SimError(f"{path.name}: {width} columns is not an even scale/value pair count")
        count = width // 2
        x = [row[0] for row in rows]
        for index in range(1, count):
            if any(row[2 * index] != row[0] for row in rows):
                raise SimError(f"{path.name}: vector {index} does not share the first scale column")
        vectors = [[row[2 * index + 1] for row in rows] for index in range(count)]
    else:
        x = [row[0] for row in rows]
        vectors = [[row[index] for row in rows] for index in range(1, width)]

    for index in range(1, len(x)):
        if x[index] < x[index - 1]:
            raise SimError(f"{path.name}: the scale column is not monotonic at row {index + 1}")
    return x, vectors


# --------------------------------------------------------------------------
# measurements
# --------------------------------------------------------------------------

def index_at(x: list[float], wanted: float, what: str, log: bool = False) -> int:
    if not x:
        raise SimError(f"{what}: no data")
    if log:
        return min(range(len(x)), key=lambda i: abs(math.log(x[i] / wanted)) if x[i] > 0 else math.inf)
    return min(range(len(x)), key=lambda i: abs(x[i] - wanted))


def value_at(x: list[float], values: list[float], wanted: float, what: str,
             log: bool = False) -> float:
    index = index_at(x, wanted, what, log=log)
    if log:
        # a logarithmic sweep never lands exactly on the request; interpolate
        if index == 0 or index == len(x) - 1:
            return values[index]
        x0, x1 = math.log10(x[index - 1]), math.log10(x[index])
        t = (math.log10(wanted) - x0) / (x1 - x0) if x1 != x0 else 0.0
        return values[index - 1] + t * (values[index] - values[index - 1])
    return values[index]


def minus3db(x: list[float], gain_db: list[float], what: str) -> float:
    """Frequency where the response is 3 dB below its low-frequency plateau."""
    if len(x) < 2:
        raise SimError(f"{what}: need at least two sweep points")
    target = gain_db[0] - 3.0
    for index in range(1, len(x)):
        if gain_db[index] <= target:
            log_f0, log_f1 = math.log10(x[index - 1]), math.log10(x[index])
            g0, g1 = gain_db[index - 1], gain_db[index]
            if g1 == g0:
                return x[index]
            return 10 ** (log_f0 + (target - g0) / (g1 - g0) * (log_f1 - log_f0))
    raise SimError(f"{what}: the sweep never falls 3 dB below {target:g} dB")


def db(gain: float) -> float:
    if gain == 0:
        return -math.inf
    return 20 * math.log10(abs(gain))


def step_metrics(t: list[float], y: list[float], step_time: float = 1e-6) -> dict:
    """Overshoot / 1 % settling / tail ripple of a step from 0 at ``step_time``."""
    if not t:
        raise SimError("step metrics: no data")
    tail = [value for time, value in zip(t, y) if time >= t[-1] - 0.1 * (t[-1] - step_time)]
    final = sum(tail) / len(tail)
    peak = max(y)
    overshoot = (peak - final) / abs(final) * 100.0 if final else math.inf
    low, high = 0.99 * final, 1.01 * final
    outside = [time for time, value in zip(t, y) if time > step_time and not (low <= value <= high)]
    if not outside:
        settled = 0.0
    elif outside[-1] > t[-1] - 0.05 * (t[-1] - step_time):
        settled = math.inf
    else:
        settled = outside[-1]
    ring = [value for time, value in zip(t, y) if time >= t[-1] - 0.25 * (t[-1] - step_time)]
    ripple = (max(ring) - min(ring)) if ring else 0.0
    return {"final": final, "overshoot": overshoot, "settle": settled,
            "ripple": ripple, "ripple_pct": ripple / abs(final) * 100.0 if final else math.inf}


def interpolate(t: list[float], values: list[float], at: float, what: str) -> float:
    index = bisect.bisect_left(t, at)
    if index == 0 or index >= len(t):
        raise SimError(f"{what}: sample {at:g}s is outside the simulation "
                       f"({t[0]:g}..{t[-1]:g}s)")
    t0, t1 = t[index - 1], t[index]
    v0, v1 = values[index - 1], values[index]
    return v0 if t1 == t0 else v0 + (v1 - v0) * (at - t0) / (t1 - t0)


def thd(t: list[float], values: list[float], fundamental: float, start: float,
        stop: float, samples: int = 2048, harmonics: int = 10) -> float:
    """Total harmonic distortion (%) over settled periods of a sine."""
    period = 1.0 / fundamental
    span = stop - start
    whole = int(span / period)
    if whole < 2:
        raise SimError(f"thd: {span:g}s is less than two periods of {fundamental:g}Hz")
    picked = [interpolate(t, values, start + i * whole * period / samples, "thd")
              for i in range(samples)]

    def amplitude(order: int) -> float:
        angle = 2 * math.pi * order * whole / samples
        cosine = sum(v * math.cos(angle * i) for i, v in enumerate(picked))
        sine = sum(v * math.sin(angle * i) for i, v in enumerate(picked))
        return math.hypot(cosine, sine)

    first = amplitude(1)
    if first == 0:
        raise SimError("thd: zero fundamental amplitude")
    return 100.0 * math.sqrt(sum(amplitude(order) ** 2
                                 for order in range(2, harmonics + 1))) / first
