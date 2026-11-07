#!/usr/bin/env python3
"""Front-board analog checks -- entry point kept, implementation in run_checks.py.

The three front-board decks (audio loading, CV jack LED, MODE button) are part
of the asserted suite now, so this wrapper just selects them. It no longer runs
ngspice itself and it no longer writes anything into ``out/``: the committed
``out/`` files are history, and every number a check uses comes from that run's
own private directory.

    python3 test_frontboard.py           # front decks + source/model checks
    bash run.sh                          # the whole suite
"""

from __future__ import annotations

import sys

import run_checks

FRONT_DECKS = ["front_audio", "front_cv_led", "front_mode"]


def main() -> int:
    arguments: list[str] = []
    for deck in FRONT_DECKS:
        arguments += ["--deck", deck]
    return run_checks.main(arguments)


if __name__ == "__main__":
    sys.exit(main())
