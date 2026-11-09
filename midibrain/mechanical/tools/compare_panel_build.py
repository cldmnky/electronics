#!/usr/bin/env python3
"""Compare two builds of the panel (and the other boards): nothing may drift.

This is the "did that change move anything?" tool. Give it a snapshot taken
BEFORE a source change and one taken AFTER, and it refuses any difference it
cannot explain. It reads only files; it never writes, never builds and never
touches `fab/`.

Two kinds of evidence, both covered:

  * ``--gerbers``  two extracted Gerber directories (the panel's inspection
                   Gerbers). Every file is compared line by line after removing
                   ONLY the exporter's timestamp lines, which differ between two
                   runs of unchanged sources. A geometry change cannot hide:
                   the coordinates, apertures, drill hits and silk are all in
                   these lines.
  * ``--json``     two ``circuit.json`` files (panel, front or rear board). The
                   element list is compared in ORDER, element by element and
                   field by field. Two pre-existing non-deterministic fields are
                   exempt - ``source_filesystem_md5_hash`` (a hash of the
                   sources) and the random ``*_warning_id`` suffix - and warning
                   elements are compared as a set because the builder emits them
                   in a varying order. Nothing else is exempt.

Usage:

    # before the change
    bunx tsci export panel.circuit.tsx -f gerbers -o /tmp/before/panel.zip
    (cd /tmp/before && unzip -q panel.zip -d gerbers)
    cp tscircuit/dist/panel/circuit.json /tmp/before/
    # after the change
    bunx tsci export panel.circuit.tsx -f gerbers -o /tmp/after/panel.zip
    (cd /tmp/after && unzip -q panel.zip -d gerbers)
    cp tscircuit/dist/panel/circuit.json /tmp/after/

    python3 mechanical/tools/compare_panel_build.py \\
      --gerbers /tmp/before/gerbers /tmp/after/gerbers \\
      --json /tmp/before/circuit.json /tmp/after/circuit.json

Exit status: 0 when everything matches, 1 on any difference, 2 on a usage error.
"""
from __future__ import annotations

import argparse
import json
import re
import sys
from pathlib import Path

# The exporter stamps the creation time into every file. These four patterns are
# the only lines allowed to differ between two runs of unchanged sources; each
# was observed by building twice and diffing (see docs/mechanical-cad.md).
TIMESTAMP_PATTERNS = (
    re.compile(r"^%TF\.CreationDate,.*$"),
    re.compile(r"^G04 Created by tscircuit \(builder\) date .*$"),
    re.compile(r"^; DRILL file \{.*\} date .*$"),
    re.compile(r"^; #@! TF\.CreationDate,.*$"),
)

# Fields that are non-deterministic or source-identity only, never geometry.
EXEMPT_FIELDS = ("source_filesystem_md5_hash",)
WARNING_SUFFIX = "_warning_id"


def normalise_lines(text: str) -> list[str]:
    return [line for line in text.splitlines()
            if not any(p.match(line) for p in TIMESTAMP_PATTERNS)]


def compare_gerbers(before: Path, after: Path) -> list[str]:
    problems: list[str] = []
    files_before = sorted(p.name for p in before.iterdir() if p.is_file())
    files_after = sorted(p.name for p in after.iterdir() if p.is_file())
    if files_before != files_after:
        problems.append(f"file set differs: {files_before} vs {files_after}")
        return problems
    if not files_before:
        problems.append(f"{before} holds no files")
        return problems
    for name in files_before:
        a = normalise_lines((before / name).read_text(errors="replace"))
        b = normalise_lines((after / name).read_text(errors="replace"))
        if a != b:
            first = next((i for i, (x, y) in enumerate(zip(a, b)) if x != y),
                         min(len(a), len(b)))
            problems.append(
                f"{name} differs at normalised line {first + 1}: "
                f"{a[first] if first < len(a) else '<missing>'!s} -> "
                f"{b[first] if first < len(b) else '<missing>'!s}")
    return problems


def _clean(element: dict) -> dict:
    clean = {k: v for k, v in element.items()
             if k not in EXEMPT_FIELDS and not k.endswith(WARNING_SUFFIX)}
    return clean


def _is_warning(element: dict) -> bool:
    return str(element.get("type", "")).endswith("_warning")


def compare_json(before: Path, after: Path) -> list[str]:
    problems: list[str] = []
    a = json.loads(before.read_text())
    b = json.loads(after.read_text())
    geometry_a = [_clean(e) for e in a if not _is_warning(e)]
    geometry_b = [_clean(e) for e in b if not _is_warning(e)]
    if len(geometry_a) != len(geometry_b):
        problems.append(f"geometry element count {len(geometry_a)} -> {len(geometry_b)}")
    for i, (x, y) in enumerate(zip(geometry_a, geometry_b)):
        if x == y:
            continue
        fields = sorted(k for k in set(x) | set(y) if x.get(k) != y.get(k))
        problems.append(
            f"element {i} ({x.get('type')} -> {y.get('type')}) differs in "
            f"{', '.join(fields)}")
        for key in fields:
            problems.append(f"    {key}: {json.dumps(x.get(key))[:160]} -> "
                            f"{json.dumps(y.get(key))[:160]}")
        if len(problems) > 40:
            problems.append("... stopping after 40 differences")
            break
    warnings_a = sorted(json.dumps(_clean(e), sort_keys=True) for e in a if _is_warning(e))
    warnings_b = sorted(json.dumps(_clean(e), sort_keys=True) for e in b if _is_warning(e))
    if warnings_a != warnings_b:
        problems.append(f"warning set differs: {len(warnings_a)} -> {len(warnings_b)}")
    return problems


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Compare two builds; nothing may drift.")
    parser.add_argument("--gerbers", nargs=2, metavar=("BEFORE", "AFTER"),
                        help="two extracted Gerber directories")
    parser.add_argument("--json", nargs=2, metavar=("BEFORE", "AFTER"),
                        help="two circuit.json files")
    args = parser.parse_args(argv)
    if not args.gerbers and not args.json:
        parser.error("give --gerbers and/or --json")

    problems: list[str] = []
    if args.gerbers:
        before, after = (Path(p) for p in args.gerbers)
        for path in (before, after):
            if not path.is_dir():
                print(f"FAIL {path} is not a directory", file=sys.stderr)
                return 2
        gerber_problems = compare_gerbers(before, after)
        print(f"gerbers: {before} vs {after}: "
              + ("identical after timestamp normalisation"
                 if not gerber_problems else f"{len(gerber_problems)} difference(s)"))
        problems += gerber_problems
    if args.json:
        before, after = (Path(p) for p in args.json)
        for path in (before, after):
            if not path.is_file():
                print(f"FAIL {path} is not a file", file=sys.stderr)
                return 2
        json_problems = compare_json(before, after)
        print(f"json:    {before} vs {after}: "
              + ("geometry identical in order, warnings set-equal"
                 if not json_problems else f"{len(json_problems)} difference(s)"))
        problems += json_problems

    if problems:
        print("\nDIFFERENT: reject the change until the cause is understood.", file=sys.stderr)
        for problem in problems:
            print(f"  {problem}", file=sys.stderr)
        return 1
    print("\nIDENTICAL: no geometry, silk, drill or element-order change.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
