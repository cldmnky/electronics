#!/usr/bin/env python3
"""Find a genuine `kicad-cli` binary, or fail with a clear message.

Why this module exists: the check scripts used to hard-code the macOS path
`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`, so on Linux (and on
any other host without that exact bundle) `check:erc` and `check:netlist`
either crashed or silently produced nothing while the baseline "passed". This
helper resolves the tool portably and refuses to invent one.

Resolution order (first hit wins):

1. an explicit path passed by the caller (`--kicad-cli PATH`),
2. the environment variable `KICAD_CLI`,
3. `kicad-cli` on `PATH`,
4. the macOS application-bundle path.

An explicit path or a set `KICAD_CLI` is authoritative: when it does not point
at an executable regular file this module raises instead of quietly using a
different tool. Only steps 3 and 4 are fallbacks.

A small executable wrapper script that runs a real KiCad install is accepted
(the binary only has to be executable and print a version); nothing here can
tell a wrapper from the real binary, and no script in this tree may substitute
a hand-rolled exporter for the genuine tool.

Usage:

    from kicad_cli import resolve_kicad_cli
    kicad_cli = resolve_kicad_cli()          # or resolve_kicad_cli(requested)

Shell use:

    python3 tools/kicad_cli.py --print       # resolved absolute path
    python3 tools/kicad_cli.py --version     # tool version, e.g. 10.0.7
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys

#: The macOS application bundle, the one host path we still know by name.
MAC_BUNDLE_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"

#: Candidate paths tried after the explicit path, the environment and PATH.
FALLBACK_PATHS: tuple[str, ...] = (MAC_BUNDLE_CLI,)

HOW_TO_SET = (
    "Set one of: --kicad-cli PATH, the environment variable KICAD_CLI, "
    "kicad-cli on PATH, or a KiCad application bundle at "
    + ", ".join(FALLBACK_PATHS)
    + "."
)


class KicadCliError(RuntimeError):
    """No usable kicad-cli (or the explicitly requested one is unusable)."""


def _reject(path: str, why: str) -> "KicadCliError":
    return KicadCliError(f"{why}: {path}\n{HOW_TO_SET}")


def _check(path: str, source: str) -> str:
    """Validate one candidate; raise KicadCliError explaining what is wrong."""
    path = os.path.abspath(os.path.expanduser(path))
    if not os.path.exists(path):
        raise _reject(path, f"{source} does not exist")
    if os.path.isdir(path):
        raise _reject(path, f"{source} is a directory, not a kicad-cli binary")
    if not os.access(path, os.X_OK):
        raise _reject(path, f"{source} is not executable")
    return path


def resolve_kicad_cli(requested: str | None = None) -> str:
    """Return the absolute path of a genuine kicad-cli.

    `requested` is the explicit `--kicad-cli` value. It, and a set `KICAD_CLI`,
    are authoritative: a bad value raises instead of falling through to another
    install, so a caller that names a tool gets that tool or an error.
    """
    if requested is not None and str(requested).strip() != "":
        return _check(str(requested).strip(), "requested --kicad-cli")

    from_env = os.environ.get("KICAD_CLI", "").strip()
    if from_env:
        return _check(from_env, "KICAD_CLI")

    on_path = shutil.which("kicad-cli")
    if on_path:
        return _check(on_path, "kicad-cli found on PATH")

    for candidate in FALLBACK_PATHS:
        if os.path.exists(candidate):
            return _check(candidate, "KiCad application bundle")

    raise KicadCliError(
        "no kicad-cli found: not on PATH and no KiCad application bundle\n"
        + HOW_TO_SET
        + "\nWithout it the ERC and netlist checks cannot run; no script in this "
        "tree substitutes a hand-written netlist exporter for the real tool."
    )


def kicad_cli_version(kicad_cli: str, timeout: float = 120.0) -> str:
    """Return `kicad-cli version` as a stripped single line.

    Raises KicadCliError when the tool cannot be run, so a check that prints the
    version cannot silently print nothing.
    """
    try:
        run = subprocess.run(
            [kicad_cli, "version"],
            capture_output=True,
            text=True,
            timeout=timeout,
        )
    except OSError as exc:
        raise KicadCliError(f"could not run {kicad_cli} version: {exc}") from exc
    except subprocess.TimeoutExpired as exc:
        raise KicadCliError(f"{kicad_cli} version timed out after {timeout}s") from exc
    output = (run.stdout or "").strip() or (run.stderr or "").strip()
    if run.returncode != 0 and not output:
        raise KicadCliError(f"{kicad_cli} version exited {run.returncode} with no output")
    first = output.splitlines()[0].strip() if output else ""
    if not first:
        raise KicadCliError(f"{kicad_cli} version printed nothing")
    return first


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="resolve a genuine kicad-cli")
    ap.add_argument("--kicad-cli", default=None, help="explicit path (default: KICAD_CLI, PATH, macOS bundle)")
    ap.add_argument("--print", dest="print_path", action="store_true", help="print the resolved path")
    ap.add_argument("--version", action="store_true", help="print the resolved tool's version")
    args = ap.parse_args(argv)
    try:
        path = resolve_kicad_cli(args.kicad_cli)
        if args.version:
            print(kicad_cli_version(path))
        elif args.print_path:
            print(path)
        else:
            print(path)
    except KicadCliError as exc:
        print(f"kicad-cli: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
