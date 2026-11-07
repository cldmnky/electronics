#!/usr/bin/env python3
"""Run KiCad ERC on the frozen KiCad project and report the truth.

    python3 froemix/tools/check_erc.py [--project froemix/tmp] [--kicad-cli PATH]
                                       [--report PATH] [--libs DIR[:DIR...]]
                                       [--kicad-share DIR]

Why this script exists: the old `check:erc` package command ran kicad-cli with
its output thrown away, redirected the report to a fixed `/tmp/froemix-erc.rpt`
and printed a grep count with `|| echo 0`. A missing kicad-cli, a crashed run,
a stale report and a truncated report all produced the same cheerful "0" and
exit status 0.

What this script guarantees:

* the tool is resolved portably (`kicad_cli.resolve_kicad_cli`: explicit path,
  `KICAD_CLI`, `PATH`, macOS bundle) and its version is printed;
* ERC is asked for ALL THREE severities — ``--severity-error``,
  ``--severity-warning`` and ``--severity-exclusions``. Asking only for
  ``--severity-exclusions`` reports the excluded findings and *nothing else*,
  which reads as a clean pass while the ordinary errors are simply absent;
* ERC runs in a private per-run scratch copy of the project, so the frozen
  ``tmp/`` tree is never modified, two runs cannot share a report, and nothing
  from an earlier run can be mistaken for this one;
* the report is validated before it is believed: header, the "Report includes"
  line (all three severities), at least one sheet section, the closing
  ``** ERC messages: N  Errors E  Warnings W`` line, and the parsed findings
  must match those counts. Anything else is "cannot verify", never "pass";
* the exit code is checked independently of the report: only 0 and 5 (5 is
  ``--exit-code-violations`` finding violations) are acceptable, and no
  message text can excuse another code;
* library *configuration* is repaired for this host without touching schematic
  bytes: URIs into this repository become ``${KIPRJMOD}``, and libraries the
  schematic references are looked up in the KiCad installation's own
  ``symbols/`` and ``footprints/`` and in ``--libs``/``FROEMIX_KICAD_LIBS``,
  then appended to the private copy of the library tables. Every library that
  cannot be found is printed with its URI and stays visible in the ERC report
  as an unexcluded finding: an unresolved environment is never hidden;
* project ERC exclusions (``froemix.kicad_pro``) are honoured and the excluded
  count is reported, so an exclusion that stops matching shows up as an
  unexcluded finding.

Exit codes: 0 = 0 unexcluded findings, 1 = unexcluded findings, 3 = ERC could
not be verified (no tool, crash, invalid/truncated/empty report). There is no
skip, force or waive flag. ``FROEMIX_SKIP_ERC=1`` is not read here; the
prototype gate lists it and the production releases refuse it.
"""

from __future__ import annotations

import argparse
import glob
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

#: Where external design libraries normally live on this host. The Froemix
#: schematic references the 4ms Eurorack library, which is not part of this
#: repository; put the genuine checkout here, or name it in
#: FROEMIX_KICAD_LIBS/--libs. Nothing is downloaded at check time.
DEFAULT_EXTERNAL_ROOT = os.path.join(
    os.path.expanduser("~"), ".local", "share", "froemix", "kicad-libs")

#: The three severities this script requests. Missing one is a false pass.
SEVERITY_FLAGS = ("--severity-error", "--severity-warning", "--severity-exclusions")
REQUIRED_INCLUDES = ("Errors", "Warnings", "Exclusions")

UNEXCLUDED = re.compile(r"^\s+;\s+(error|warning)\s*$")
EXCLUDED = re.compile(r"^\s+;\s+(error|warning)\s+\(excluded\)\s*$")
VIOLATION_HEAD = re.compile(r"^\[(?P<check>[a-z0-9_]+)\]:\s*(?P<description>.*)$")
POSITION = re.compile(r"^\s+@\((?P<x>[-0-9.]+) (?P<unit>[a-z]+), (?P<y>[-0-9.]+) (?P<unit2>[a-z]+)\)")
REPORT_HEADER = re.compile(r"^ERC report \((?P<timestamp>.+), Encoding (?P<encoding>[A-Za-z0-9_-]+)\)$")
REPORT_INCLUDES = re.compile(r"^Report includes: (?P<items>.+)$")
REPORT_SUMMARY = re.compile(
    r"^\s*\*\* ERC messages:\s*(?P<messages>\d+)\s+Errors\s+(?P<errors>\d+)\s+Warnings\s+(?P<warnings>\d+)\s*$"
)
SHEET_MARKER = re.compile(r"^\*\*\*\*\* Sheet ")
LIB_ENTRY = re.compile(
    r'\(lib \(name "(?P<name>[^"]+)"\)\(type "(?P<type>[^"]+)"\)\(uri "(?P<uri>[^"]*)"\)'
)
LIB_ID = re.compile(r'\(lib_id "(?P<library>[^":]+):(?P<part>[^"]+)"\)')
FOOTPRINT_PROPERTY = re.compile(r'\(property "Footprint" "(?P<library>[^":]+):')
PROJECT_FILES = (
    "froemix.kicad_pro",
    "fp-lib-table",
    "sym-lib-table",
)
SKIP_LIBRARY_SUFFIXES = (".wrl", ".step", ".stp", ".3dshapes")

#: Findings that come from a library this host cannot resolve rather than from
#: the design. They are printed as such and still counted and still fail.
ENVIRONMENT_CHECKS = frozenset({"lib_symbol_issues", "footprint_link_issues"})


class ErcError(RuntimeError):
    """ERC could not be verified (no tool, crash, invalid report)."""


class ReportFormatError(ErcError):
    """The ERC report is not a complete, self-consistent KiCad report."""


# --------------------------------------------------------------------------
# report parsing (pure, strict)
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


class Report:
    """A validated KiCad ERC report."""

    def __init__(self, findings, includes, messages, errors, warnings, timestamp):
        self.findings = findings
        self.includes = includes
        self.messages = messages
        self.errors = errors
        self.warnings = warnings
        self.timestamp = timestamp

    @property
    def unexcluded(self):
        return [finding for finding in self.findings if not finding.excluded]

    @property
    def excluded(self):
        return [finding for finding in self.findings if finding.excluded]

    def summary(self):
        return {
            "total": len(self.findings),
            "excluded": len(self.excluded),
            "unexcluded": len(self.unexcluded),
            "errors": sum(1 for f in self.unexcluded if f.severity == "error"),
            "warnings": sum(1 for f in self.unexcluded if f.severity == "warning"),
            "checks": sorted({f.check for f in self.unexcluded}),
        }


# A KiCad ERC report block is: a "[check]: description" head, then a severity
# line ("; error", "; warning", optionally "(excluded)"), then an "@(x, y)"
# position. The position follows the severity line, so the parser looks one
# line ahead. Every finding is derivable from the text alone.
def _iter_findings(lines):
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


def parse_report(text: str) -> Report:
    """Parse and validate a KiCad `sch erc --format report` text.

    Raises ReportFormatError for anything that is not a complete,
    self-consistent report: wrong or missing header, a "Report includes" line
    that does not cover all three severities, no sheet section, no closing
    count line, or counts that disagree with the findings. A truncated or
    foreign file therefore can never be read as "0 findings".
    """
    lines = text.splitlines()
    if not lines:
        raise ReportFormatError("ERC report is empty")

    header = REPORT_HEADER.match(lines[0])
    if not header:
        raise ReportFormatError(
            f"ERC report does not start with a KiCad report header: {lines[0][:120]!r}")

    includes = None
    summary = None
    sheet_sections = 0
    for raw in lines:
        if SHEET_MARKER.match(raw):
            sheet_sections += 1
        match = REPORT_INCLUDES.match(raw)
        if match:
            includes = [item.strip() for item in match.group("items").split(",")]
        match = REPORT_SUMMARY.match(raw)
        if match:
            summary = {key: int(match.group(key)) for key in ("messages", "errors", "warnings")}

    if includes is None:
        raise ReportFormatError("ERC report has no 'Report includes:' line")
    missing = [item for item in REQUIRED_INCLUDES if item not in includes]
    if missing:
        raise ReportFormatError(
            "ERC report does not include " + ", ".join(missing)
            + " - it was not asked for all three severities, so it cannot be trusted")
    if sheet_sections == 0:
        raise ReportFormatError("ERC report has no '***** Sheet ' section")
    if summary is None:
        raise ReportFormatError(
            "ERC report has no closing '** ERC messages: N  Errors E  Warnings W' line "
            "(truncated report)")

    parsed = [Finding(*row) for row in _iter_findings(lines)]
    errors = sum(1 for finding in parsed if finding.severity == "error")
    warnings = sum(1 for finding in parsed if finding.severity == "warning")
    if summary["messages"] != len(parsed):
        raise ReportFormatError(
            f"ERC report counts {summary['messages']} messages but {len(parsed)} findings are present")
    if summary["errors"] != errors:
        raise ReportFormatError(
            f"ERC report counts {summary['errors']} errors but {errors} findings are present")
    if summary["warnings"] != warnings:
        raise ReportFormatError(
            f"ERC report counts {summary['warnings']} warnings but {warnings} findings are present")

    return Report(
        findings=parsed,
        includes=includes,
        messages=summary["messages"],
        errors=summary["errors"],
        warnings=summary["warnings"],
        timestamp=header.group("timestamp"),
    )


def findings(text: str):
    """Unexcluded and excluded findings of a validated report (raises if invalid)."""
    return parse_report(text).findings


def count(report_findings):
    report = Report(report_findings, [], 0, 0, 0, "")
    return report.summary()


# --------------------------------------------------------------------------
# library resolution (pure helpers, tested)
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


def referenced_libraries(schematics: dict[str, str]) -> tuple[set[str], set[str]]:
    """Library nicknames the schematic files reference.

    Symbol libraries come from `(lib_id "lib:part")`, footprint libraries from
    `(property "Footprint" "lib:footprint")`. Both are needed: a schematic can
    reference a symbol library that is configured and a footprint library that
    is not, and ERC warns about the second.
    """
    symbols, footprints = set(), set()
    for text in schematics.values():
        for match in LIB_ID.finditer(text):
            symbols.add(match.group("library"))
        for match in FOOTPRINT_PROPERTY.finditer(text):
            footprints.add(match.group("library"))
    return symbols, footprints


def _find_library(kind: str, name: str, kicad_share: str | None,
                  external_roots: list[str]) -> str | None:
    """Locate one library directory/file by nickname."""
    suffix = ".kicad_sym" if kind == "symbol" else ".pretty"
    subdir = "symbols" if kind == "symbol" else "footprints"
    if kicad_share:
        candidate = os.path.join(kicad_share, subdir, name + suffix)
        if os.path.exists(candidate):
            return candidate
    for root in external_roots:
        patterns = (
            os.path.join(root, subdir, name + suffix),
            os.path.join(root, "*", subdir, name + suffix),
            os.path.join(root, "*", name + suffix),
        )
        for pattern in patterns:
            matches = sorted(glob.glob(pattern))
            if matches:
                return matches[0]
    return None


def append_resolved_libraries(name: str, text: str, wanted: set[str],
                              kicad_share: str | None,
                              external_roots: list[str]) -> tuple[str, dict]:
    """Append table entries for referenced libraries this host actually has.

    Only names the schematic references are considered, and only entries that
    exist are appended: this repairs the private copy of the table, it does not
    invent a library or change a schematic byte. Names that cannot be found are
    returned so the caller can print them; they stay unresolved in ERC.
    """
    kind = "symbol" if "sym" in name else "footprint"
    present = {entry.group("name") for entry in LIB_ENTRY.finditer(text)}
    appended, missing = [], []
    for library in sorted(wanted - present):
        resolved = _find_library(kind, library, kicad_share, external_roots)
        if resolved is None:
            missing.append(library)
        else:
            appended.append((library, resolved))
    if not appended:
        return text if text.endswith("\n") else text + "\n", {"appended": [], "missing": missing}
    lines = text.splitlines()
    # the table closes with the last line that is exactly ")"
    closing = max(index for index, line in enumerate(lines) if line.strip() == ")")
    entries = [f'  (lib (name "{library}")(type "KiCad")(uri "{uri}")(options "")(descr ""))'
               for library, uri in appended]
    lines = lines[:closing] + entries + [")"] + lines[closing + 1:]
    return "\n".join(lines) + "\n", {"appended": appended, "missing": missing}


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
            return {name for name in names if name.endswith(SKIP_LIBRARY_SUFFIXES)}

        shutil.copytree(library, os.path.join(scratch, "library"), ignore=ignore, symlinks=True)
    if not os.path.isfile(os.path.join(scratch, "froemix.kicad_sch")):
        raise ErcError(f"could not copy the schematic set from {project_dir}")


def find_kicad_share(kicad_cli: str, requested: str | None) -> str | None:
    """Locate the KiCad library root (the install's own symbols/ + footprints/)."""
    candidates: list[str] = []
    for value in (requested, os.environ.get("FROEMIX_KICAD_SHARE")):
        if value:
            candidates.append(os.path.abspath(value))
    real = os.path.realpath(kicad_cli)
    for base in (os.path.dirname(kicad_cli), os.path.dirname(real)):
        candidates.append(os.path.join(base, "..", "share", "kicad"))
        candidates.append(os.path.join(base, "..", "..", "share", "kicad"))
        candidates.append(os.path.join(base, "..", "..", "..", "share", "kicad"))
        candidates.append(os.path.join(base, "root", "usr", "share", "kicad"))
        candidates.append(os.path.join(base, "..", "root", "usr", "share", "kicad"))
    candidates.extend((
        "/usr/share/kicad",
        "/usr/local/share/kicad",
        "/Applications/KiCad/KiCad.app/Contents/SharedSupport",
    ))
    for candidate in candidates:
        path = os.path.abspath(candidate)
        if os.path.isdir(os.path.join(path, "symbols")) and os.path.isdir(os.path.join(path, "footprints")):
            return path
    return None


def run_erc(kicad_cli: str, project_dir: str, report_path: str) -> tuple[int, str]:
    """Run ERC with all three severities; return (exit code, combined output)."""
    if os.path.exists(report_path):
        os.unlink(report_path)  # never read a report from an earlier run
    run = subprocess.run(
        [kicad_cli, "sch", "erc", *SEVERITY_FLAGS, "--format", "report",
         "--units", "mm", "--exit-code-violations",
         "--output", report_path, os.path.join(project_dir, "froemix.kicad_sch")],
        capture_output=True,
        text=True,
    )
    output = ((run.stdout or "") + (run.stderr or "")).strip()
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
    ap.add_argument("--kicad-share", default=None,
                    help="KiCad library root (default: FROEMIX_KICAD_SHARE, then auto-detect)")
    ap.add_argument("--max-listed", type=int, default=20, help="unexcluded findings printed (default: 20)")
    args = ap.parse_args(argv)

    project_dir = os.path.abspath(args.project)
    external_roots = [os.path.abspath(p) for p in args.libs.split(os.pathsep) if p.strip()]
    if args.libs == os.environ.get("FROEMIX_KICAD_LIBS", "") and os.path.isdir(DEFAULT_EXTERNAL_ROOT):
        # a conventional place for libraries that do not belong to this repo
        external_roots.append(DEFAULT_EXTERNAL_ROOT)

    try:
        kicad_cli = resolve_kicad_cli(args.kicad_cli)
        version = kicad_cli_version(kicad_cli)
        print(f"kicad-cli: {version} ({kicad_cli})")
        check_project(project_dir)
        print(f"project: {project_dir}")
        kicad_share = find_kicad_share(kicad_cli, args.kicad_share)
        print(f"kicad libraries: {kicad_share or 'not found (only repo-local and --libs entries resolve)'}")

        tmp_root = os.environ.get("TMPDIR") or tempfile.gettempdir()
        os.makedirs(tmp_root, exist_ok=True)
        scratch = tempfile.mkdtemp(prefix="froemix-erc-", dir=tmp_root)
        try:
            copy_project(project_dir, scratch)
            schematics = {
                path: open(os.path.join(scratch, path), encoding="utf-8").read()
                for path in os.listdir(scratch) if path.endswith(".kicad_sch")
            }
            wanted_symbols, wanted_footprints = referenced_libraries(schematics)
            library_dir = os.path.join(project_dir, "library")
            for table in ("fp-lib-table", "sym-lib-table"):
                path = os.path.join(scratch, table)
                if not os.path.isfile(path):
                    continue
                text = open(path, encoding="utf-8").read()
                wanted = wanted_symbols if "sym" in table else wanted_footprints
                text, report = normalise_lib_table(table, text, scratch, library_dir, external_roots)
                text, extra = append_resolved_libraries(table, text, wanted, kicad_share, external_roots)
                open(path, "w", encoding="utf-8").write(text)
                print(f"library table {report['table']}: "
                      f"{len(report['kept'])} kept, "
                      f"{len(report['rebased_repo'])} repo-local, "
                      f"{len(report['rebased_external'])} external, "
                      f"{len(extra['appended'])} appended, "
                      f"{len(report['unresolved'])} unresolved")
                if report["rebased_repo"]:
                    print("  repo-local (${KIPRJMOD}/library): "
                          + ", ".join(report["rebased_repo"]))
                if report["rebased_external"]:
                    print("  external (--libs): " + ", ".join(report["rebased_external"]))
                for library, uri in extra["appended"]:
                    print(f"  appended from this host: {library} -> {uri}")
                if extra["missing"]:
                    print("  NOT ON THIS HOST (referenced, not resolvable): "
                          + ", ".join(extra["missing"]))
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
            # The exit code is checked independently of anything printed: only
            # 0 and 5 (violations found) are expected, and no message text can
            # excuse another code.
            if exit_code not in (0, 5):
                raise ErcError(f"kicad-cli sch erc exited {exit_code}, expected 0 or 5\n{output}")
            if not os.path.isfile(report_in_scratch):
                raise ErcError(f"kicad-cli wrote no ERC report (exit {exit_code})\n{output}")
            if os.path.getsize(report_in_scratch) == 0:
                raise ErcError(f"kicad-cli wrote an empty ERC report (exit {exit_code})\n{output}")

            text = open(report_in_scratch, encoding="utf-8").read()
            if args.report:
                destination = os.path.abspath(args.report)
                os.makedirs(os.path.dirname(destination), exist_ok=True)
                shutil.copyfile(report_in_scratch, destination)
                print(f"report: {destination}")
            else:
                print(f"report: {report_in_scratch}")

            report = parse_report(text)
            totals = report.summary()
            print(f"asked for severities: {' '.join(SEVERITY_FLAGS)}")
            print(f"report includes: {', '.join(report.includes)}")
            print(f"ERC unexcluded: {totals['unexcluded']} "
                  f"(errors {totals['errors']}, warnings {totals['warnings']}); "
                  f"excluded: {totals['excluded']}")
            if totals["checks"]:
                print(f"ERC unexcluded checks: {', '.join(totals['checks'])}")
            if totals["unexcluded"] == 0:
                print("ERC PASS: 0 unexcluded findings")
                return 0
            unexcluded = report.unexcluded
            for finding in unexcluded[: args.max_listed]:
                print(f"  {finding.line()}")
            if len(unexcluded) > args.max_listed:
                print(f"  ... {len(unexcluded) - args.max_listed} more in the report")
            environment = [finding for finding in unexcluded if finding.check in ENVIRONMENT_CHECKS]
            if environment:
                print(f"  of these, {len(environment)} come from library configuration this host "
                      "could not resolve (environment, printed above), not from the design")
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
