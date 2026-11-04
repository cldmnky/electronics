#!/usr/bin/env python3
"""Run KiCad ERC on the frozen KiCad project and report the truth.

    python3 froemix/tools/check_erc.py [--project froemix/tmp] [--kicad-cli PATH]
                                       [--report PATH] [--libs DIR[:DIR...]]

Why this script exists: the old `check:erc` package command ran kicad-cli with
its output thrown away, redirected the report to a fixed `/tmp/froemix-erc.rpt`,
and then printed a grep count with `|| echo 0`. A missing kicad-cli, a crashed
ERC run and a stale report from an earlier schematic all produced the same
cheerful "0" and exit status 0. Nothing below can do that:

* the tool is resolved portably (``kicad_cli.resolve_kicad_cli``: explicit path,
  ``KICAD_CLI``, ``PATH``, macOS bundle) and its version is printed;
* ERC runs in a private per-run scratch copy of the project, so the frozen
  ``tmp/`` tree is never modified, two runs cannot share a report, and nothing
  from an earlier run can be mistaken for this one;
* the only library-table edits are resolution fixes for this host: paths that
  point into this repository's own ``tmp/library`` become ``${KIPRJMOD}``, and
  external design libraries are rebased from ``--libs``/``FROEMIX_KICAD_LIBS``
  when they exist. Every dropped entry is named in the output, so a library this
  host does not have is visible rather than silent;
* project ERC exclusions (``froemix.kicad_pro``) are honoured and their count is
  reported, so an exclusion that stops matching shows up as an unexcluded
  finding;
* a missing, empty or unparsable report is a failure (exit 3), never a pass.

Exit codes: 0 = 0 unexcluded findings, 1 = unexcluded findings, 3 = ERC could
not be verified (no tool, crash, no report). There is no skip, force or waive
flag. ``FROEMIX_SKIP_ERC=1`` is not read here; the prototype gate lists it and
the production releases refuse it.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from kicad_cli import KicadCliError, kicad_cli_version, resolve_kicad_cli  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_PROJECT = os.path.join(ROOT, "tmp")

#: An unexcluded finding: the `; error` / `; warning` line with no "(excluded)".
UNEXCLUDED = re.compile(r"^\s+;\s+(error|warning)\s*$")
EXCLUDED = re.compile(r"^\s+;\s+(error|warning)\s+\(excluded\)\s*$")
VIOLATION_HEAD = re.compile(r"^\[(?P<check>[a-z0-9_]+)\]:\s*(?P<description>.*)$")
POSITION = re.compile(r"^\s+@\((?P<x>[-0-9.]+) (?P<unit>[a-z]+), (?P<y>[-0-9.]+) (?P<unit2>[a-z]+)\)")
LIB_ENTRY = re.compile(
    r'\(lib \(name "(?P<name>[^"]+)"\)\(type "(?P<type>[^"]+)"\)\(uri "(?P<uri>[^"]*)"\)'
)
PROJECT_FILES = (
    "froemix.kicad_pro",
    "fp-lib-table",
    "sym-lib-table",
)
SKIP_LIBRARY_SUFFIXES = (".wrl", ".step", ".stp", ".3dshapes")


class ErcError(RuntimeError):
    """ERC could not be verified."""


# --------------------------------------------------------------------------
# report parsing (pure)
# --------------------------------------------------------------------------

class Finding:
    __slots__ = ("check", "description", "severity", "excluded", "where")

    def __init__(self, check, description, severity, excluded, where):
        self.check = check
        self.description = description
        self.severity = severity
        self.excluded = excluded
        self.where = where

    def line(self):
        state = "excluded" if self.excluded else "UNEXCLUDED"
        at = f" {self.where}" if self.where else ""
        return f"{state} {self.severity} {self.check}: {self.description}{at}"


# A KiCad ERC report block is: a "[check]: description" head, then a severity
# line ("; error", "; warning", optionally "(excluded)"), then an "@(x, y)"
# position. The position follows the severity line, so the parser looks one
# line ahead. Every finding is derivable from the text alone, and a block whose
# head is missing is still counted (check name empty) rather than dropped.
def _iter_findings(text: str):
    lines = text.splitlines()
    check = description = ""
    for index, raw in enumerate(lines):
        head = VIOLATION_HEAD.match(raw)
        if head:
            check, description = head.group("check"), head.group("description").strip()
            continue
        if not (UNEXCLUDED.match(raw) or EXCLUDED.match(raw)):
            continue
        where = ""
        if index + 1 < len(lines):
            pos = POSITION.match(lines[index + 1])
            if pos:
                where = (f"@({pos.group('x')} {pos.group('unit')}, "
                         f"{pos.group('y')} {pos.group('unit2')})")
        yield (check, description, ("error" if "error" in raw else "warning"),
               ("(excluded)" in raw), where)


def findings(text: str) -> list[Finding]:
    return [Finding(*row) for row in _iter_findings(text)]


def count(findings_: list[Finding]):
    unexcluded = [f for f in findings_ if not f.excluded]
    return {
        "total": len(findings_),
        "excluded": len(findings_) - len(unexcluded),
        "unexcluded": len(unexcluded),
        "errors": sum(1 for f in unexcluded if f.severity == "error"),
        "warnings": sum(1 for f in unexcluded if f.severity == "warning"),
        "checks": sorted({f.check for f in unexcluded}),
    }


# --------------------------------------------------------------------------
# library-table resolution (pure, tested)
# --------------------------------------------------------------------------

def _rebase_external(uri: str, roots: list[str]) -> str | None:
    """Return an existing path under one of `roots` that ends with `uri`'s tail."""
    tail = uri.replace("\\", "/").strip("/")
    if not tail:
        return None
    while tail:
        for root in roots:
            candidate = os.path.join(root, tail)
            if os.path.exists(candidate):
                return candidate
        head, _, rest = tail.partition("/")
        if not rest:
            break
        tail = rest
    return None


def normalise_lib_table(name: str, text: str, project_dir: str, library_dir: str,
                        external_roots: list[str]) -> tuple[str, dict]:
    """Rewrite library URIs so they resolve on this host; name every drop.

    Resolution, per entry:
      * already resolves (absolute path exists, or ${KIPRJMOD} entry exists)
        -> kept unchanged;
      * points into this repository's KiCad library -> ${KIPRJMOD}/library/...;
      * found under one of the external roots -> rebased to that absolute path;
      * otherwise dropped and reported as unresolved.

    Nothing design-related changes: the tables only say where libraries live.
    """
    kept, rebased_repo, rebased_external, unresolved = [], [], [], []
    lines_out = []
    for line in text.splitlines():
        entry = LIB_ENTRY.search(line)
        if not entry:
            lines_out.append(line)
            continue
        uri = entry.group("uri")
        expanded = uri.replace("${KIPRJMOD}", project_dir)
        if os.path.exists(expanded):
            kept.append(entry.group("name"))
            lines_out.append(line)
            continue
        marker = "/library/"
        if marker in uri:
            tail = uri.split(marker, 1)[1]
            candidate = os.path.join(library_dir, tail)
            if os.path.exists(candidate):
                rebased_repo.append(entry.group("name"))
                lines_out.append(line.replace(uri, "${KIPRJMOD}/library/" + tail))
                continue
        external = _rebase_external(uri, external_roots)
        if external:
            rebased_external.append(entry.group("name"))
            lines_out.append(line.replace(uri, external))
            continue
        unresolved.append((entry.group("name"), uri))
    report = {
        "table": name,
        "kept": kept,
        "rebased_repo": rebased_repo,
        "rebased_external": rebased_external,
        "unresolved": unresolved,
    }
    return "\n".join(lines_out) + "\n", report


# --------------------------------------------------------------------------
# scratch project
# --------------------------------------------------------------------------

def check_project(project_dir: str) -> None:
    if not os.path.isdir(project_dir):
        raise ErcError(f"KiCad project directory not found: {project_dir}")
    root_sch = os.path.join(project_dir, "froemix.kicad_sch")
    if not os.path.isfile(root_sch):
        raise ErcError(f"no froemix.kicad_sch in {project_dir}")
    if not os.path.isfile(os.path.join(project_dir, "froemix.kicad_pro")):
        raise ErcError(f"no froemix.kicad_pro in {project_dir} (ERC exclusions live there)")


def copy_project(project_dir: str, scratch: str) -> None:
    """Copy the schematic set and the repo-local libraries into `scratch`."""
    for entry in sorted(os.listdir(project_dir)):
        src = os.path.join(project_dir, entry)
        if entry.endswith((".kicad_sch", ".kicad_dru", ".kicad_prl")) or entry in PROJECT_FILES:
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(scratch, entry))
    library = os.path.join(project_dir, "library")
    if os.path.isdir(library):
        def ignore(directory, names):
            dropped = set()
            for name in names:
                if name.endswith(SKIP_LIBRARY_SUFFIXES):
                    dropped.add(name)
            return dropped

        shutil.copytree(library, os.path.join(scratch, "library"), ignore=ignore, symlinks=True)
    if not os.path.isfile(os.path.join(scratch, "froemix.kicad_sch")):
        raise ErcError(f"could not copy the schematic set from {project_dir}")


def run_erc(kicad_cli: str, project_dir: str, report_path: str) -> tuple[int, str]:
    """Run ERC, returning (exit code, combined output). Fails if no report."""
    if os.path.exists(report_path):
        os.unlink(report_path)  # never read a report from an earlier run
    run = subprocess.run(
        [kicad_cli, "sch", "erc", "--severity-exclusions", "--format", "report",
         "--units", "mm", "--output", report_path, os.path.join(project_dir, "froemix.kicad_sch")],
        capture_output=True,
        text=True,
    )
    output = ((run.stdout or "") + (run.stderr or "")).strip()
    if not os.path.isfile(report_path):
        raise ErcError(f"kicad-cli wrote no ERC report (exit {run.returncode})\n{output}")
    if os.path.getsize(report_path) == 0:
        raise ErcError(f"kicad-cli wrote an empty ERC report (exit {run.returncode})\n{output}")
    return run.returncode, output


# --------------------------------------------------------------------------
# main
# --------------------------------------------------------------------------

def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="KiCad ERC that cannot pass silently")
    ap.add_argument("--project", default=DEFAULT_PROJECT, help="KiCad project dir (default: froemix/tmp)")
    ap.add_argument("--kicad-cli", default=None, help="explicit path (default: KICAD_CLI, PATH, macOS bundle)")
    ap.add_argument("--report", default=None, help="also keep the ERC report at this path")
    ap.add_argument("--libs", default=os.environ.get("FROEMIX_KICAD_LIBS", ""),
                    help="colon-separated roots of external KiCad libraries (default: FROEMIX_KICAD_LIBS)")
    ap.add_argument("--max-listed", type=int, default=20, help="unexcluded findings printed (default: 20)")
    args = ap.parse_args(argv)

    project_dir = os.path.abspath(args.project)
    external_roots = [os.path.abspath(p) for p in args.libs.split(os.pathsep) if p.strip()]

    try:
        kicad_cli = resolve_kicad_cli(args.kicad_cli)
        version = kicad_cli_version(kicad_cli)
        print(f"kicad-cli: {version} ({kicad_cli})")
        check_project(project_dir)
        print(f"project: {project_dir}")

        tmp_root = os.environ.get("TMPDIR") or tempfile.gettempdir()
        os.makedirs(tmp_root, exist_ok=True)
        scratch = tempfile.mkdtemp(prefix="froemix-erc-", dir=tmp_root)
        try:
            copy_project(project_dir, scratch)
            library_dir = os.path.join(project_dir, "library")
            for table in ("fp-lib-table", "sym-lib-table"):
                path = os.path.join(scratch, table)
                if not os.path.isfile(path):
                    continue
                text = open(path, encoding="utf-8").read()
                new_text, report = normalise_lib_table(
                    table, text, scratch, library_dir, external_roots)
                open(path, "w", encoding="utf-8").write(new_text)
                print(f"library table {report['table']}: "
                      f"{len(report['kept'])} kept, "
                      f"{len(report['rebased_repo'])} repo-local, "
                      f"{len(report['rebased_external'])} external, "
                      f"{len(report['unresolved'])} unresolved")
                if report["rebased_repo"]:
                    print("  repo-local (${KIPRJMOD}/library): "
                          + ", ".join(report["rebased_repo"]))
                if report["rebased_external"]:
                    print("  external (--libs): " + ", ".join(report["rebased_external"]))
                if report["unresolved"]:
                    # Naming every dropped library is the point: a library this
                    # host does not have must be visible, not silently gone.
                    print("  unresolved (dropped from the table, not on this host): "
                          + ", ".join(name for name, _ in report["unresolved"]))
                    locations = sorted({os.path.dirname(uri) for _, uri in report["unresolved"]})
                    print("    library locations: " + " ; ".join(locations))

            report_in_scratch = os.path.join(scratch, "froemix-erc.rpt")
            if args.report and os.path.exists(os.path.abspath(args.report)):
                # A failed run must not leave a previous report looking like
                # this run's result.
                os.unlink(os.path.abspath(args.report))
            exit_code, output = run_erc(kicad_cli, scratch, report_in_scratch)
            text = open(report_in_scratch, encoding="utf-8").read()
            if args.report:
                destination = os.path.abspath(args.report)
                os.makedirs(os.path.dirname(destination), exist_ok=True)
                if os.path.exists(destination):
                    os.unlink(destination)
                shutil.copyfile(report_in_scratch, destination)
                print(f"report: {destination}")
            else:
                print(f"report: {report_in_scratch}")

            parsed = findings(text)
            totals = count(parsed)
            print(f"ERC unexcluded: {totals['unexcluded']} "
                  f"(errors {totals['errors']}, warnings {totals['warnings']}); "
                  f"excluded: {totals['excluded']}")
            if totals["checks"]:
                print(f"ERC unexcluded checks: {', '.join(totals['checks'])}")
            if exit_code not in (0, 5) and "Found" not in output:
                raise ErcError(f"kicad-cli erc exited {exit_code}\n{output}")
            if totals["unexcluded"] == 0:
                print("ERC PASS: 0 unexcluded findings")
                return 0
            unexcluded = [f for f in parsed if not f.excluded]
            for finding in unexcluded[: args.max_listed]:
                print(f"  {finding.line()}")
            if len(unexcluded) > args.max_listed:
                print(f"  ... {len(unexcluded) - args.max_listed} more in the report")
            print("ERC FAIL: unexcluded findings remain (exclusions: froemix.kicad_pro)")
            return 1
        finally:
            if os.environ.get("FROEMIX_KEEP_ERC_SCRATCH") == "1":
                print(f"scratch kept: {scratch}")
            else:
                shutil.rmtree(scratch, ignore_errors=True)
    except (ErcError, KicadCliError) as exc:
        print(f"ERC CANNOT BE VERIFIED: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main())
