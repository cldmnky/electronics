#!/usr/bin/env python3
"""Tests for check_erc.py. No KiCad needed for most of them; the native ERC
integration tests skip when no genuine kicad-cli is available.

Three groups:

* parser/validator tests on synthetic report text (strict header, "Report
  includes", sheet section, closing counts);
* CLI tests with a fake kicad-cli that records its arguments, writes a canned
  report and returns a chosen exit code - including the regression test for the
  severity flags (asking only for ``--severity-exclusions`` reports the excluded
  findings and nothing else, which used to look like a clean pass);
* native integration tests against the real project with genuine kicad-cli: the
  unmutated project must show no unexcluded electrical finding, and a copy with
  one PWR_FLAG removed must show a NEW unexcluded ``power_pin_not_driven`` error
  that the checker reports and rejects.
"""

import hashlib
import os
import shutil
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

import check_erc as e  # noqa: E402
from kicad_cli import KicadCliError, resolve_kicad_cli  # noqa: E402

PROJECT = HERE.parent / "tmp"


def entry(check, severity="error", excluded=False, pos="10.16 mm, 20.32 mm", text=None):
    return {
        "check": check,
        "severity": severity,
        "excluded": excluded,
        "pos": pos,
        "text": text or f"{check} description",
    }


def make_report(entries, includes="Errors, Warnings, Exclusions", summary=None,
                drop_summary=False):
    """A complete, self-consistent KiCad ERC report text (or a broken one)."""
    lines = [
        "ERC report (2026-01-01T00:00:00, Encoding UTF8)",
        f"Report includes: {includes}",
        "",
        "***** Sheet /",
    ]
    errors = warnings = 0
    for item in entries:
        lines.append(f"[{item['check']}]: {item['text']}")
        state = "; error" if item["severity"] == "error" else "; warning"
        if item["excluded"]:
            state += " (excluded)"
        lines.append(f"    {state}")
        if item["pos"]:
            lines.append(f"    @({item['pos']}): Symbol DaisySeed1 Pin 16 [AUDIO_IN_1, Input, Line]")
        if item["severity"] == "error":
            errors += 1
        else:
            warnings += 1
    lines.append("")
    lines.append("***** Sheet /Audio/")
    if not drop_summary:
        counts = summary or {"messages": errors + warnings, "errors": errors, "warnings": warnings}
        lines.append("")
        lines.append(f" ** ERC messages: {counts['messages']}  Errors {counts['errors']}  "
                     f"Warnings {counts['warnings']}")
    lines.append("")
    return "\n".join(lines)


EXCLUDED_ONLY = make_report([
    entry("pin_not_driven", excluded=True),
    entry("lib_symbol_issues", "warning", excluded=True),
])
ONE_UNEXCLUDED_WARNING = make_report([entry("unconnected_wire_endpoint", "warning")])
ONE_UNEXCLUDED_ERROR = make_report([entry("power_pin_not_driven")])
NO_FINDINGS = make_report([])

FAKE_TOOL = """#!/bin/sh
# fake kicad-cli: records its argv, writes a canned report, exits as told
if [ "$1" = "version" ]; then echo 10.0.7; exit 0; fi
[ -n "$FAKE_ERC_ARGV" ] && printf '%s\\n' "$@" > "$FAKE_ERC_ARGV"
out=""
prev=""
for a in "$@"; do
  case "$prev" in
    --output|-o) out="$a" ;;
  esac
  prev="$a"
done
if [ -n "$FAKE_ERC_REPORT_FILE" ] && [ -f "$FAKE_ERC_REPORT_FILE" ] && [ -n "$out" ]; then
  cp "$FAKE_ERC_REPORT_FILE" "$out"
fi
[ -n "$FAKE_ERC_STDOUT" ] && echo "$FAKE_ERC_STDOUT"
exit "${FAKE_ERC_EXIT:-5}"
"""


class ParserTests(unittest.TestCase):
    def test_excluded_and_unexcluded_are_counted_separately(self):
        report = e.parse_report(EXCLUDED_ONLY)
        totals = report.summary()
        self.assertEqual(totals["total"], 2)
        self.assertEqual(totals["excluded"], 2)
        self.assertEqual(totals["unexcluded"], 0)

    def test_one_unexcluded_warning_is_counted(self):
        totals = e.parse_report(ONE_UNEXCLUDED_WARNING).summary()
        self.assertEqual((totals["unexcluded"], totals["warnings"], totals["errors"]), (1, 1, 0))
        self.assertEqual(totals["checks"], ["unconnected_wire_endpoint"])

    def test_position_and_head_are_attached_to_the_finding(self):
        finding = e.parse_report(ONE_UNEXCLUDED_ERROR).unexcluded[0]
        self.assertEqual(finding.check, "power_pin_not_driven")
        self.assertEqual(finding.severity, "error")
        self.assertIn("@(10.16 mm, 20.32 mm)", finding.where)
        self.assertIn("UNEXCLUDED error power_pin_not_driven", finding.line())

    def test_an_empty_report_passes_with_the_closing_zero_counts(self):
        self.assertEqual(e.parse_report(NO_FINDINGS).summary()["total"], 0)

    def test_garbage_is_not_zero_findings(self):
        with self.assertRaises(e.ReportFormatError) as caught:
            e.findings("garbage")
        self.assertIn("report header", str(caught.exception))

    def test_empty_text_is_rejected(self):
        with self.assertRaises(e.ReportFormatError):
            e.parse_report("")

    def test_truncated_report_is_rejected(self):
        truncated = make_report([entry("power_pin_not_driven")], drop_summary=True)
        with self.assertRaises(e.ReportFormatError) as caught:
            e.parse_report(truncated)
        self.assertIn("truncated", str(caught.exception))

    def test_report_without_the_exclusions_severity_is_rejected(self):
        # This is the shape a `--severity-error --severity-warning` run writes;
        # it cannot be considered a complete check.
        text = make_report([entry("power_pin_not_driven")], includes="Errors, Warnings")
        with self.assertRaises(e.ReportFormatError) as caught:
            e.parse_report(text)
        self.assertIn("Exclusions", str(caught.exception))

    def test_report_without_a_sheet_section_is_rejected(self):
        text = make_report([entry("power_pin_not_driven")]).replace("***** Sheet ", "Sheet ")
        with self.assertRaises(e.ReportFormatError):
            e.parse_report(text)

    def test_counts_that_disagree_with_the_findings_are_rejected(self):
        text = make_report([entry("power_pin_not_driven")],
                           summary={"messages": 3, "errors": 3, "warnings": 0})
        with self.assertRaises(e.ReportFormatError) as caught:
            e.parse_report(text)
        self.assertIn("counts 3 messages", str(caught.exception))

    def test_a_report_that_omits_a_violation_but_counts_it_is_rejected(self):
        text = make_report([]).replace(
            " ** ERC messages: 0  Errors 0  Warnings 0",
            " ** ERC messages: 1  Errors 1  Warnings 0",
        )
        with self.assertRaises(e.ReportFormatError):
            e.parse_report(text)


class Fixture(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.tool = self.dir / "kicad-cli"
        self.tool.write_text(FAKE_TOOL)
        self.tool.chmod(self.tool.stat().st_mode | stat.S_IXUSR)
        self.project = self.dir / "proj"
        (self.project / "library" / "jclpcb.pretty").mkdir(parents=True)
        (self.project / "froemix.kicad_sch").write_text(
            '(kicad_sch (lib_id "power:GND"))\n(property "Footprint" "Module:Electrosmith_Daisy_Seed")\n')
        (self.project / "froemix.kicad_pro").write_text('{"erc": {}}\n')
        (self.project / "fp-lib-table").write_text("(fp_lib_table\n  (version 7)\n)\n")
        (self.project / "sym-lib-table").write_text("(sym_lib_table\n  (version 7)\n)\n")
        self.report_file = self.dir / "report.txt"
        self.report_file.write_text(EXCLUDED_ONLY)
        self.scratch_root = self.dir / "tmpdir"
        self.scratch_root.mkdir()
        self.argv = self.dir / "argv.txt"
        self.kicad_share = self.dir / "share"
        (self.kicad_share / "symbols").mkdir(parents=True)
        (self.kicad_share / "footprints").mkdir(parents=True)
        (self.kicad_share / "symbols" / "power.kicad_sym").write_text("(kicad_symbol_lib)\n")
        (self.kicad_share / "footprints" / "Module.pretty").mkdir()

    def run_check(self, *args, env=None, report_file=None):
        base = {
            "TMPDIR": str(self.scratch_root),
            "KICAD_CLI": str(self.tool),
            "FAKE_ERC_REPORT_FILE": str(report_file or self.report_file),
            "FAKE_ERC_ARGV": str(self.argv),
            "FAKE_ERC_EXIT": "5",
            "KICAD_SHARE": None,
        }
        base.update(env or {})
        import io
        from contextlib import redirect_stderr, redirect_stdout

        patched = {key: value for key, value in base.items() if value is not None}
        removed = [key for key, value in base.items() if value is None]
        with patch.dict(os.environ, patched, clear=False):
            for key in removed:
                os.environ.pop(key, None)
            out, err = io.StringIO(), io.StringIO()
            with redirect_stdout(out), redirect_stderr(err):
                code = e.main(["--project", str(self.project), "--kicad-share",
                               str(self.kicad_share), *args])
        return code, out.getvalue(), err.getvalue()


class GateTests(Fixture):
    def test_the_checker_requests_all_three_severities(self):
        # Regression test for the flag bug: asking only for
        # --severity-exclusions reports the excluded findings and nothing else,
        # which read as a clean pass. All three flags must be on the command.
        self.run_check()
        argv = self.argv.read_text().split()
        for flag in ("--severity-error", "--severity-warning", "--severity-exclusions"):
            self.assertIn(flag, argv)
        self.assertIn("--exit-code-violations", argv)

    def test_zero_unexcluded_passes(self):
        code, out, err = self.run_check()
        self.assertEqual(code, 0, out + err)
        self.assertIn("ERC PASS: 0 unexcluded findings", out)
        self.assertIn("asked for severities: --severity-error --severity-warning --severity-exclusions", out)
        self.assertIn("excluded: 2", out)

    def test_one_unexcluded_warning_fails(self):
        self.report_file.write_text(ONE_UNEXCLUDED_WARNING)
        code, out, _ = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("ERC FAIL", out)
        self.assertIn("unconnected_wire_endpoint", out)

    def test_one_unexcluded_error_fails(self):
        self.report_file.write_text(ONE_UNEXCLUDED_ERROR)
        code, out, _ = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("UNEXCLUDED error power_pin_not_driven", out)

    def test_garbage_report_is_exit_3(self):
        garbage = self.dir / "garbage.txt"
        garbage.write_text("this is not an ERC report\n")
        code, _, err = self.run_check(report_file=garbage)
        self.assertEqual(code, 3)
        self.assertIn("CANNOT BE VERIFIED", err)
        self.assertIn("report header", err)

    def test_truncated_report_is_exit_3(self):
        truncated = self.dir / "truncated.txt"
        truncated.write_text(make_report([entry("power_pin_not_driven")], drop_summary=True))
        code, _, err = self.run_check(report_file=truncated)
        self.assertEqual(code, 3)
        self.assertIn("truncated", err)

    def test_unexpected_exit_is_rejected_even_with_found_in_the_output(self):
        # A crash that prints "Found 0 violations" must never pass; the exit
        # code is checked independently of any message text.
        self.report_file.write_text(NO_FINDINGS)
        code, _, err = self.run_check(env={"FAKE_ERC_EXIT": "1", "FAKE_ERC_STDOUT": "Found 0 violations"})
        self.assertEqual(code, 3)
        self.assertIn("exited 1", err)

    def test_unexpected_exit_is_rejected_even_with_a_valid_report(self):
        code, _, err = self.run_check(env={"FAKE_ERC_EXIT": "2"})
        self.assertEqual(code, 3)
        self.assertIn("expected 0 or 5", err)

    def test_exit_code_5_with_a_valid_report_is_accepted(self):
        code, out, _ = self.run_check(env={"FAKE_ERC_EXIT": "5"})
        self.assertEqual(code, 0, out)

    def test_no_report_is_a_failure_not_a_pass(self):
        code, _, err = self.run_check(env={"FAKE_ERC_REPORT_FILE": None})
        self.assertEqual(code, 3)
        self.assertIn("no ERC report", err)

    def test_empty_report_is_a_failure(self):
        empty = self.dir / "empty.txt"
        empty.write_text("")
        code, _, err = self.run_check(env={"FAKE_ERC_REPORT_FILE": str(empty)})
        self.assertEqual(code, 3)
        self.assertIn("empty ERC report", err)

    def test_stale_report_at_the_output_path_is_replaced(self):
        stale = self.dir / "keep.rpt"
        stale.write_text(ONE_UNEXCLUDED_ERROR)
        self.report_file.write_text(EXCLUDED_ONLY)
        code, out, _ = self.run_check("--report", str(stale))
        self.assertEqual(code, 0, out)
        self.assertEqual(stale.read_text(), EXCLUDED_ONLY)

    def test_stale_report_is_removed_when_the_run_produces_none(self):
        stale = self.dir / "keep.rpt"
        stale.write_text(ONE_UNEXCLUDED_ERROR)
        code, _, err = self.run_check("--report", str(stale), env={"FAKE_ERC_REPORT_FILE": None})
        self.assertEqual(code, 3)
        self.assertFalse(stale.exists(), "a stale report must not survive a failed run")

    def test_missing_tool_is_exit_3_with_a_clear_message(self):
        code, _, err = self.run_check(env={"KICAD_CLI": str(self.dir / "absent")})
        self.assertEqual(code, 3)
        self.assertIn("KICAD_CLI", err)

    def test_project_without_a_schematic_is_exit_3(self):
        (self.project / "froemix.kicad_sch").unlink()
        code, _, err = self.run_check()
        self.assertEqual(code, 3)
        self.assertIn("no froemix.kicad_sch", err)

    def test_project_without_a_project_file_is_exit_3(self):
        (self.project / "froemix.kicad_pro").unlink()
        code, _, err = self.run_check()
        self.assertEqual(code, 3)
        self.assertIn("froemix.kicad_pro", err)

    def test_original_project_files_are_never_modified(self):
        def digest(path):
            return hashlib.sha256(Path(path).read_bytes()).hexdigest()

        before = {p: digest(p) for p in self.project.rglob("*") if p.is_file()}
        self.assertEqual(self.run_check()[0], 0)
        after = {p: digest(p) for p in self.project.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_scratch_directory_is_private_and_cleaned_up(self):
        self.run_check()
        self.assertEqual(list(self.scratch_root.rglob("*")), [])

    def test_two_runs_do_not_share_a_report(self):
        first = self.dir / "first.rpt"
        second = self.dir / "second.rpt"
        self.assertEqual(self.run_check("--report", str(first))[0], 0)
        self.report_file.write_text(ONE_UNEXCLUDED_ERROR)
        self.assertEqual(self.run_check("--report", str(second))[0], 1)
        self.assertEqual(first.read_text(), EXCLUDED_ONLY)
        self.assertEqual(second.read_text(), ONE_UNEXCLUDED_ERROR)

    def test_private_library_tables_are_appended_without_touching_the_schematic(self):
        code, out, _ = self.run_check()
        self.assertEqual(code, 0, out)
        # power (symbol) and Module (footprint) are referenced and available
        self.assertIn("appended from this host", out)
        self.assertIn("symbols/power.kicad_sym", out)
        self.assertIn("footprints/Module.pretty", out)
        # the schematic is byte-identical afterwards (asserted above); the
        # scratch copy is gone, so nothing was written outside it


class LibraryTableTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.project = self.root / "proj"
        self.library = self.project / "library"
        (self.library / "jclpcb.pretty").mkdir(parents=True)
        (self.library / "jclpcb.kicad_sym").write_text("(kicad_symbol_lib)\n")
        self.share = self.root / "share"
        (self.share / "symbols").mkdir(parents=True)
        (self.share / "footprints").mkdir(parents=True)
        (self.share / "symbols" / "power.kicad_sym").write_text("(kicad_symbol_lib)\n")
        (self.share / "footprints" / "Module.pretty").mkdir()

    def table(self, *uris):
        lines = ["(fp_lib_table", "  (version 7)"]
        for name, uri in uris:
            lines.append(f'  (lib (name "{name}")(type "KiCad")(uri "{uri}")(options "")(descr ""))')
        lines.append(")")
        return "\n".join(lines) + "\n"

    def test_referenced_libraries_covers_symbols_and_footprints(self):
        symbols, footprints = e.referenced_libraries({
            "a.kicad_sch": '(lib_id "MCU_Module:Electrosmith_Daisy_Seed_Rev4")\n'
                           '(property "Footprint" "Module:Electrosmith_Daisy_Seed")\n',
            "b.kicad_sch": '(lib_id "jclpcb:N4141")\n',
        })
        self.assertEqual(symbols, {"MCU_Module", "jclpcb"})
        self.assertEqual(footprints, {"Module"})

    def test_repo_local_library_is_rebased_to_project(self):
        text = self.table(("jclpcb", "/Users/someone/hardware/library/jclpcb.pretty"))
        new, report = e.normalise_lib_table("fp-lib-table", text, str(self.project),
                                            str(self.library), [])
        self.assertEqual(report["rebased_repo"], ["jclpcb"])
        self.assertIn("${KIPRJMOD}/library/jclpcb.pretty", new)

    def test_external_root_rebases_by_path_tail(self):
        external = self.root / "ext" / "4ms-kicad-lib" / "symbols"
        external.mkdir(parents=True)
        (external / "4ms_Connector.kicad_sym").write_text("(kicad_symbol_lib)\n")
        text = self.table(("4ms_Connector",
                           "/Users/someone/mnkydsy/hardware/4ms-kicad-lib/symbols/4ms_Connector.kicad_sym"))
        new, report = e.normalise_lib_table("sym-lib-table", text, str(self.project),
                                            str(self.library), [str(self.root / "ext")])
        self.assertEqual(report["rebased_external"], ["4ms_Connector"])
        self.assertIn(str(external / "4ms_Connector.kicad_sym"), new)

    def test_unresolved_library_is_dropped_and_named(self):
        text = self.table(("4ms_Jack", "/Users/someone/hardware/4ms-kicad-lib/footprints/4ms_Jack.pretty"))
        new, report = e.normalise_lib_table("fp-lib-table", text, str(self.project),
                                            str(self.library), [])
        self.assertEqual([name for name, _ in report["unresolved"]], ["4ms_Jack"])
        self.assertNotIn("4ms_Jack", new)

    def test_existing_absolute_path_is_kept_untouched(self):
        real = self.root / "real.pretty"
        real.mkdir()
        text = self.table(("mine", str(real)))
        new, report = e.normalise_lib_table("fp-lib-table", text, str(self.project),
                                            str(self.library), [])
        self.assertEqual(report["kept"], ["mine"])
        self.assertIn(str(real), new)

    def test_append_finds_official_libraries_and_keeps_the_table_valid(self):
        text = self.table(("jclpcb", str(self.library / "jclpcb.pretty")))
        new, report = e.append_resolved_libraries(
            "sym-lib-table", text, {"power", "not_installed"}, str(self.share), [])
        self.assertEqual([name for name, _ in report["appended"]], ["power"])
        self.assertEqual(report["missing"], ["not_installed"])
        self.assertIn('(lib (name "power")(type "KiCad")(uri "', new)
        self.assertEqual(new.count("(lib "), 2)
        self.assertTrue(new.rstrip().endswith(")"))

    def test_append_uses_external_roots(self):
        external = self.root / "ext"
        (external / "4ms-kicad-lib" / "symbols").mkdir(parents=True)
        (external / "4ms-kicad-lib" / "symbols" / "4ms_Connector.kicad_sym").write_text("(x)\n")
        text = self.table()
        new, report = e.append_resolved_libraries(
            "sym-lib-table", text, {"4ms_Connector"}, None, [str(external)])
        self.assertEqual(len(report["appended"]), 1)
        self.assertIn("4ms_Connector", new)

    def test_find_kicad_share_accepts_an_explicit_root(self):
        self.assertEqual(e.find_kicad_share("/nonexistent/kicad-cli", str(self.share)), str(self.share))

    def test_find_kicad_share_returns_none_without_libraries(self):
        empty = self.root / "empty"
        empty.mkdir()
        with patch.dict(os.environ, {"FROEMIX_KICAD_SHARE": ""}, clear=False):
            self.assertIsNone(e.find_kicad_share("/nonexistent/kicad-cli", str(empty)))


def native_kicad_cli():
    try:
        return resolve_kicad_cli(os.environ.get("KICAD_CLI") or None)
    except KicadCliError:
        return None


NATIVE_CLI = native_kicad_cli()


@unittest.skipUnless(NATIVE_CLI, "no genuine kicad-cli on this host (set KICAD_CLI); "
                                 "native ERC integration tests skipped")
class NativeErcTests(unittest.TestCase):
    """Real ERC against the real project. Synthetic CLI tests cannot catch a
    wrong severity flag: only the genuine tool shows what is actually reported."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def run_native(self, project, *args):
        import io
        from contextlib import redirect_stderr, redirect_stdout

        out, err = io.StringIO(), io.StringIO()
        with patch.dict(os.environ, {"TMPDIR": str(self.dir)}, clear=False):
            with redirect_stdout(out), redirect_stderr(err):
                code = e.main(["--project", str(project), "--kicad-cli", NATIVE_CLI, *args])
        return code, out.getvalue(), err.getvalue()

    def test_unmutated_project_has_no_unexcluded_electrical_finding(self):
        code, out, err = self.run_native(PROJECT)
        self.assertIn("asked for severities: --severity-error --severity-warning --severity-exclusions", out)
        self.assertIn("report includes: Errors, Warnings, Exclusions", out)
        self.assertNotIn("UNEXCLUDED error pin_not_driven", out, out)
        if code != 0:
            # Only an unresolved library environment may remain; it must be
            # named as such, never silently dropped.
            self.assertIn("library configuration this host could not resolve", out)
        if code == 0:
            self.assertIn("ERC PASS", out)
        self.assertNotIn("CANNOT BE VERIFIED", err)

    def test_new_electrical_error_is_reported_and_rejected(self):
        project = self.dir / "proj"
        shutil.copytree(PROJECT, project)
        # Remove one PWR_FLAG symbol: the net it drove loses its driver, which
        # is a real power_pin_not_driven ERC error, not a configuration change.
        schematic = project / "power.kicad_sch"
        text = schematic.read_text()
        start = text.index('(symbol\n\t\t(lib_id "power:PWR_FLAG")')
        depth = 0
        index = start
        while True:
            char = text[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    end = index + 1
                    break
            index += 1
        schematic.write_text(text[:start] + text[end:])

        baseline_code, baseline_out, _ = self.run_native(PROJECT)
        code, out, err = self.run_native(project)
        self.assertNotEqual(code, 0, out + err)
        self.assertIn("UNEXCLUDED error power_pin_not_driven", out)
        self.assertIn("ERC FAIL", out)
        self.assertNotIn("CANNOT BE VERIFIED", err)
        # the mutation really added something the baseline did not have
        self.assertNotIn("power_pin_not_driven", baseline_out)
        self.assertGreater(out.count("UNEXCLUDED"), baseline_out.count("UNEXCLUDED"))

    def test_mutated_copy_is_detected_even_with_a_passing_baseline(self):
        # Guard against a checker that only lights up when the baseline is red:
        # whatever the baseline says, the mutated copy must contain the new
        # error as UNEXCLUDED and the exit code must be nonzero.
        project = self.dir / "proj2"
        shutil.copytree(PROJECT, project)
        schematic = project / "froemix.kicad_sch"
        text = schematic.read_text()
        start = text.index('(symbol\n\t\t(lib_id "power:PWR_FLAG")')
        depth = 0
        index = start
        while True:
            char = text[index]
            if char == "(":
                depth += 1
            elif char == ")":
                depth -= 1
                if depth == 0:
                    end = index + 1
                    break
            index += 1
        schematic.write_text(text[:start] + text[end:])
        code, out, _ = self.run_native(project)
        self.assertEqual(code, 1, out)
        self.assertIn("power_pin_not_driven", out)
        self.assertIn("ERC unexcluded:", out)
        self.assertNotIn("ERC unexcluded: 0", out)


if __name__ == "__main__":
    unittest.main()
