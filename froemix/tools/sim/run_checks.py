#!/usr/bin/env python3
"""Run every froemix analog deck and assert its limits. Exit nonzero on failure.

    cd froemix/tools/sim && bash run.sh          # every deck, every check
    python3 run_checks.py --deck audio_in        # one deck
    python3 run_checks.py --list                 # deck names
    python3 run_checks.py --source-only          # linkage + model tables, no ngspice

What a run does, in order:

  1. source linkage (``source_link.py``): every linked deck element still
     matches its VALUE and its CONNECTIONS in tscircuit/sections/*.tsx (and the
     euro board for the front decks).
  2. model provenance (``models.py``): every model parameter in a deck is in
     the documented table, and every deck value is unambiguous SPICE.
  3. the decks themselves, each copied into a fresh private directory under
     ``$TMPDIR/froemix-sim/`` and run with its own log, so no result can come
     from a stale or committed file. ngspice stderr is kept beside the log.
  4. the assertion tables (``assertions.py``).

Exit codes: 0 every check passed, 1 at least one check failed (including a
missing/NaN/missing-.meas result or an ngspice error line), 2 the environment
is incomplete (no ngspice, no source tree). Nothing is written inside the
repository: the committed ``out/`` directory is history, not a result.
"""

from __future__ import annotations

import argparse
import shutil
import sys
from pathlib import Path

import assertions
import models
import simlib
import source_link

HERE = Path(__file__).resolve().parent


def _print_header(title: str) -> None:
    print(f"\n== {title}")


def deck_files(deck_dir: Path) -> list[Path]:
    return sorted(deck_dir.glob("*.cir"))


def run(deck_names: list[str], ngspice: str | None, workdir: Path | None,
        source_only: bool) -> int:
    """0 on success, 1 on a failed check, 2 on an environment problem."""
    deck_dir = HERE
    known = list(assertions.DECK_CHECKS)
    unknown = [name for name in deck_names if name not in known]
    if unknown:
        print(f"unknown deck(s): {', '.join(unknown)} (known: {', '.join(known)})",
              file=sys.stderr)
        return 2

    _print_header("source linkage and model provenance")
    problems = source_link.check_all(deck_dir=deck_dir)
    model_problems: list[str] = []
    for deck in deck_files(deck_dir):
        model_problems.extend(models.check_models(deck))
        model_problems.extend(models.check_mux_assumptions(deck))
        model_problems.extend(simlib.check_deck_values(deck))
    if problems or model_problems:
        for problem in problems:
            print(f"  FAIL {problem}")
        for problem in model_problems:
            print(f"  FAIL {problem}")
        print(f"\nsource/model checks failed ({len(problems) + len(model_problems)} "
              "problem(s)); the simulations were not started")
        return 1
    linked = sum(len(links) for links in source_link.DECK_LINKS.values())
    nets = sum(len(entries) for entries in source_link.DECK_NETS.values())
    print(f"  PASS {linked} linked deck elements, {nets} net links, "
          f"{len(models.PROVENANCE)} model parameter sets")

    if source_only:
        print("\nsource/model checks only (--source-only); the decks were not run")
        return 0

    try:
        exe = simlib.find_ngspice(ngspice)
    except simlib.SimError as exc:
        print(f"\nFAIL {exc}", file=sys.stderr)
        return 2
    print(f"  PASS ngspice: {exe}")

    selected = deck_names or known
    failures: list[assertions.Check] = []
    passed = 0
    for deck in selected:
        work = workdir / deck if workdir else simlib.private_workdir(deck)
        if work.exists():
            shutil.rmtree(work)
        try:
            result = simlib.run_deck(HERE / f"{deck}.cir", work, ngspice=exe)
            checks = assertions.DECK_CHECKS[deck](result)
        except simlib.SimError as exc:
            print(f"\n== {deck}: FAIL -- {exc}")
            failures.append(assertions.Check(deck, "simulation or output", str(exc),
                                             "a usable result", False))
            continue
        _print_header(f"{deck}  ({work})")
        print(f"  log: {result.log.name}   stderr: {result.stderr.name}")
        for check in checks:
            print(check.line())
        failures.extend(check for check in checks if not check.ok)
        passed += sum(1 for check in checks if check.ok)

    print()
    if failures:
        print(f"FAIL: {len(failures)} check(s) failed, {passed} passed")
        for failure in failures:
            print(f"  {failure.deck}: {failure.name} = {failure.measured}, "
                  f"limit {failure.limit} ({failure.source})")
        return 1
    print(f"PASS: {passed} checks over {len(selected)} decks "
          "(behavioural models, not measured hardware)")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description="Run the froemix analog decks and assert every limit.")
    parser.add_argument("--deck", action="append", default=[],
                        help="only this deck (repeatable)")
    parser.add_argument("--ngspice", default=None, help="native ngspice binary")
    parser.add_argument("--workdir", type=Path, default=None,
                        help="run the decks here instead of a private $TMPDIR dir")
    parser.add_argument("--source-only", action="store_true",
                        help="linkage and model tables only, no simulation")
    parser.add_argument("--list", action="store_true", help="print the deck names")
    parser.add_argument("--models", action="store_true",
                        help="print the model provenance table and exit")
    args = parser.parse_args(argv)
    if args.list:
        for deck in assertions.DECK_CHECKS:
            print(deck)
        return 0
    if args.models:
        print(models.table_text())
        return 0
    try:
        return run(args.deck, args.ngspice, args.workdir, args.source_only)
    except source_link.LinkError as exc:
        print(f"FAIL: source linkage could not run: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
