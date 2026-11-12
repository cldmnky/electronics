#!/usr/bin/env python3
"""Compare the firmware pin map with the board source.

    python3 froemix/tools/check_firmware_pins.py [--repo-root PATH]

The firmware's `src/pins.hpp` table is the only place a Daisy Seed pin is bound
to a board net, and it is hand-written. This script makes that claim checkable:
it re-derives the binding from the board's own sources and fails on any
difference - a renamed net, a moved pin, an added/removed connection.

Sources of truth, all in this repository:

  froemix/tscircuit/lib/pins.ts        Seed pad number -> signal label
                                       (DAISY_SEED_PINS: "pin16": "AUDIO_IN_1")
  froemix/tscircuit/sections/main.tsx  DaisySeed1 connections: signal label -> net
  froemix/tmp/library/DaisyKiCad-main/Daisy-Boards.kicad_sym
                                       Seed pad number -> libDaisy D-number
                                       (the official Electrosmith symbol)

Exit codes: 0 = match, 1 = mismatch, 2 = a source could not be parsed (which is
a failure too - a silently skipped check is worse than a red one).
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

# The Seed's 40-pin header: pads 1..15 are D0..D14, pads 16..21 are the codec /
# analogue / 3.3 V pins, pads 22..37 are D15..D30, pads 38..40 are power.
# The checker does NOT hardcode this - it reads the Electrosmith symbol below -
# but this is why the firmware table's D-numbers look non-contiguous.
EXPECTED_PAD_COUNT = 40


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:  # pragma: no cover - environment problem
        print(f"error: cannot read {path}: {exc}", file=sys.stderr)
        raise SystemExit(2)


def parse_symbol_pins(root: Path) -> dict[int, str]:
    """pad number -> symbol pin name ("D0".."D30", "AUDIO_IN_1", ...)."""
    path = root / "tmp/library/DaisyKiCad-main/Daisy-Boards.kicad_sym"
    text = read_text(path)

    start = text.find('(symbol "Daisy_Seed"')
    if start < 0:
        print(f"error: Daisy_Seed symbol not found in {path}", file=sys.stderr)
        raise SystemExit(2)
    # The block ends at the next top-level symbol definition.
    end = text.find('\n\t(symbol "', start + 1)
    block = text[start : end if end > 0 else len(text)]

    pairs = re.findall(r'\(name\s+"([^"]+)"[\s\S]*?\(number\s+"([^"]+)"', block)
    pins: dict[int, str] = {}
    for name, number in pairs:
        if number.isdigit():
            pins[int(number)] = name

    if len(pins) != EXPECTED_PAD_COUNT:
        print(
            f"error: parsed {len(pins)} pads from {path.name}, expected "
            f"{EXPECTED_PAD_COUNT}",
            file=sys.stderr,
        )
        raise SystemExit(2)
    return pins


def parse_pins_ts(root: Path) -> dict[int, str]:
    """pad number -> signal label, from lib/pins.ts (DAISY_SEED_PINS).

    These labels are what main.tsx uses as the Seed component's pin names, so
    they, not the symbol's D-numbers, are the key to the netlist.
    """
    path = root / "tscircuit/lib/pins.ts"
    text = read_text(path)

    match = re.search(
        r"export const DAISY_SEED_PINS = \{(?P<body>.*?)\n\} as const",
        text,
        re.DOTALL,
    )
    if not match:
        print(f"error: DAISY_SEED_PINS not found in {path}", file=sys.stderr)
        raise SystemExit(2)

    labels: dict[int, str] = {}
    for pin, label in re.findall(r'pin(\d+):\s*"([^"]+)"', match.group("body")):
        pad = int(pin)
        if pad in labels:
            print(f"error: {path.name} maps pad {pad} twice", file=sys.stderr)
            raise SystemExit(2)
        labels[pad] = label
    if len(labels) != EXPECTED_PAD_COUNT:
        print(
            f"error: parsed {len(labels)} labels from {path.name}, expected "
            f"{EXPECTED_PAD_COUNT}",
            file=sys.stderr,
        )
        raise SystemExit(2)
    return labels


def parse_board_nets(root: Path) -> dict[str, str]:
    """signal label -> net, from the DaisySeed1 element in sections/main.tsx."""
    path = root / "tscircuit/sections/main.tsx"
    text = read_text(path)

    start = text.find('name="DaisySeed1"')
    if start < 0:
        print(f"error: DaisySeed1 not found in {path}", file=sys.stderr)
        raise SystemExit(2)
    block_start = text.find("connections={{", start)
    block_end = text.find("}}", block_start)
    if block_start < 0 or block_end < 0:
        print(f"error: DaisySeed1 connections not found in {path}", file=sys.stderr)
        raise SystemExit(2)

    nets: dict[str, str] = {}
    for label, net in re.findall(
        r'"([A-Za-z0-9_]+)":\s*"net\.([A-Za-z0-9_]+)"', text[block_start:block_end]
    ):
        nets[label] = net
    if not nets:
        print(f"error: no connections parsed from {path}", file=sys.stderr)
        raise SystemExit(2)
    return nets


def parse_firmware_pins(root: Path) -> dict[int, tuple[str, str]]:
    """pad -> (net, D-number-or-NA), from firmware/src/pins.hpp."""
    path = root / "firmware/src/pins.hpp"
    text = read_text(path)

    match = re.search(r"#define FROEMIX_SEED_PINS\(X\)(?P<body>[\s\S]*?)\n// clang-format on", text)
    if not match:
        print(f"error: FROEMIX_SEED_PINS table not found in {path}", file=sys.stderr)
        raise SystemExit(2)

    rows = re.findall(
        r"X\(\s*(\d+),\s*([A-Za-z0-9_]+),\s*([A-Za-z0-9_]+),", match.group("body")
    )
    table: dict[int, tuple[str, str]] = {}
    for pad, net, dn in rows:
        table[int(pad)] = (net, dn)

    missing = set(range(1, EXPECTED_PAD_COUNT + 1)) - set(table)
    if missing:
        print(
            "error: pins.hpp does not cover pad(s) "
            + ", ".join(str(p) for p in sorted(missing)),
            file=sys.stderr,
        )
        raise SystemExit(2)
    return table


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo-root",
        type=Path,
        default=Path(__file__).resolve().parents[1],
        help="froemix/ directory (default: the parent of this script's directory)",
    )
    args = parser.parse_args()
    root: Path = args.repo_root

    symbol = parse_symbol_pins(root)
    labels = parse_pins_ts(root)
    board = parse_board_nets(root)
    firmware = parse_firmware_pins(root)

    problems: list[str] = []

    for pad in range(1, EXPECTED_PAD_COUNT + 1):
        label = labels[pad]
        net = board.get(label)
        if net is None:
            problems.append(f"pad {pad}: no connection for {label} in main.tsx")
            continue
        fw_net, fw_dn = firmware[pad]
        if fw_net != net:
            problems.append(f"pad {pad}: board net {net}, firmware net {fw_net}")

        # The symbol's pin name is the libDaisy D-number the firmware must use
        # (pads 16..21, 38..40 are not GPIOs and carry no D-number).
        symbol_name = symbol[pad]
        is_gpio = symbol_name.startswith("D") and symbol_name[1:].isdigit()
        expected_dn = symbol_name if is_gpio else "NA"
        if fw_dn != expected_dn:
            problems.append(
                f"pad {pad}: symbol pin {symbol_name}, firmware names it {fw_dn}"
            )

    if problems:
        print("firmware pin map does NOT match the board:", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1

    print(
        f"firmware pin map matches the board: {EXPECTED_PAD_COUNT} pads "
        f"({len(set(board.values()))} nets, D-numbers from the Electrosmith symbol)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
