#!/usr/bin/env python3
"""Tests for check_erc.py: no KiCad install and no network required.

The point of these tests is the failure side: a missing report, an empty report,
a stale report, a crash and an unexcluded finding must each produce a nonzero
exit with a message that says what happened. A checker that can pass without
having run is worse than no checker.
"""

import hashlib
import os
import stat
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import check_erc as e  # noqa: E402

EXCLUDED_ONLY = """ERC report (2026-01-01T00:00:00, Encoding UTF8)
Report includes: Errors, Warnings, Exclusions

***** Sheet /
[pin_not_driven]: Input pin not driven by any Output pins
    ; error (excluded)
    @(132.08 mm, 46.99 mm): Symbol DaisySeed1 Pin 16 [AUDIO_IN_1, Input, Line]
    AUDIO_IN_1 driven through R9 1k from U2 opamp output
[lib_symbol_issues]: Library symbol issue
    ; warning (excluded)
    @(148.59 mm, 27.94 mm): Symbol C1 [CL31A106KBHNNNE]

 ** ERC messages: 2  Errors 1  Warnings 1
"""

ONE_UNEXCLUDED_WARNING = """ERC report (2026-01-01T00:00:00, Encoding UTF8)

***** Sheet /
[unconnected_wire_endpoint]: Wire endpoint is not connected
    ; warning
    @(10.16 mm, 20.32 mm): Wire
    A wire end is dangling
 ** ERC messages: 1  Errors 0  Warnings 1
"""

ONE_UNEXCLUDED_ERROR = """ERC report (2026-01-01T00:00:00, Encoding UTF8)

***** Sheet /
[power_pin_not_driven]: A power input pin is not driven
    ; error
    @(20.32 mm, 30.48 mm): Symbol U9
 ** ERC messages: 1  Errors 1  Warnings 0
"""

FAKE_TOOL = """#!/bin/sh
# fake kicad-cli: version, plus sch erc writing a canned report
if [ "$1" = "version" ]; then echo 10.0.7; exit 0; fi
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
if [ -n "$FAKE_ERC_STDOUT" ]; then echo "$FAKE_ERC_STDOUT"; fi
exit "${FAKE_ERC_EXIT:-5}"
"""

ALL_EXCLUDED = EXCLUDED_ONLY


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
        (self.project / "froemix.kicad_sch").write_text("(kicad_sch)\n")
        (self.project / "froemix.kicad_pro").write_text('{"erc": {}}\n')
        (self.project / "audio.kicad_sch").write_text("(kicad_sch audio)\n")
        (self.project / "fp-lib-table").write_text("(fp_lib_table\n)\n")
        (self.project / "sym-lib-table").write_text("(sym_lib_table\n)\n")
        self.report_file = self.dir / "report.txt"
        self.report_file.write_text(ALL_EXCLUDED)
        self.scratch_root = self.dir / "tmpdir"
        self.scratch_root.mkdir()

    def run_check(self, *args, env=None, report_file=None):
        report_file = report_file or self.report_file
        base = {
            "TMPDIR": str(self.scratch_root),
            "KICAD_CLI": str(self.tool),
            "FAKE_ERC_REPORT_FILE": str(report_file),
        }
        base.update(env or {})
        patched = {k: v for k, v in base.items() if v is not None}
        removed = [k for k, v in base.items() if v is None]
        with patch.dict(os.environ, patched, clear=False):
            for key in removed:
                os.environ.pop(key, None)
            import io
            from contextlib import redirect_stderr, redirect_stdout

            out, err = io.StringIO(), io.StringIO()
            with redirect_stdout(out), redirect_stderr(err):
                code = e.main(["--project", str(self.project), *args])
        return code, out.getvalue(), err.getvalue()


class ReportParsingTests(unittest.TestCase):
    def test_counts_excluded_and_unexcluded_separately(self):
        parsed = e.findings(EXCLUDED_ONLY)
        totals = e.count(parsed)
        self.assertEqual(totals["total"], 2)
        self.assertEqual(totals["excluded"], 2)
        self.assertEqual(totals["unexcluded"], 0)

    def test_one_unexcluded_warning_is_counted(self):
        totals = e.count(e.findings(ONE_UNEXCLUDED_WARNING))
        self.assertEqual((totals["unexcluded"], totals["warnings"], totals["errors"]), (1, 1, 0))
        self.assertEqual(totals["checks"], ["unconnected_wire_endpoint"])

    def test_position_and_head_are_attached_to_the_finding(self):
        finding = e.findings(ONE_UNEXCLUDED_ERROR)[0]
        self.assertEqual(finding.check, "power_pin_not_driven")
        self.assertEqual(finding.severity, "error")
        self.assertIn("@(20.32 mm, 30.48 mm)", finding.where)
        self.assertIn("UNEXCLUDED error power_pin_not_driven", finding.line())

    def test_a_report_without_findings_counts_zero(self):
        self.assertEqual(e.count(e.findings("nothing here\n"))["total"], 0)


class GateTests(Fixture):
    def test_zero_unexcluded_passes(self):
        code, out, err = self.run_check()
        self.assertEqual(code, 0, out + err)
        self.assertIn("ERC PASS: 0 unexcluded findings", out)
        self.assertIn("unexcluded: 0", out)
        self.assertIn("excluded: 2", out)

    def test_one_unexcluded_warning_fails(self):
        self.report_file.write_text(ONE_UNEXCLUDED_WARNING)
        code, out, err = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("ERC FAIL", out)
        self.assertIn("unconnected_wire_endpoint", out)

    def test_one_unexcluded_error_fails(self):
        self.report_file.write_text(ONE_UNEXCLUDED_ERROR)
        code, out, _ = self.run_check()
        self.assertEqual(code, 1)
        self.assertIn("UNEXCLUDED error power_pin_not_driven", out)

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

    def test_crash_without_output_is_a_failure(self):
        code, _, err = self.run_check(
            env={"FAKE_ERC_REPORT_FILE": None, "FAKE_ERC_EXIT": "1", "FAKE_ERC_STDOUT": "boom"},
        )
        self.assertEqual(code, 3)
        self.assertIn("no ERC report", err)

    def test_stale_report_at_the_output_path_is_replaced(self):
        # A previous run's report must never be read as this run's result.
        stale = self.dir / "keep.rpt"
        stale.write_text(ONE_UNEXCLUDED_ERROR)
        self.report_file.write_text(ALL_EXCLUDED)
        code, out, _ = self.run_check("--report", str(stale))
        self.assertEqual(code, 0, out)
        self.assertEqual(stale.read_text(), ALL_EXCLUDED)
        self.assertIn("ERC PASS", out)

    def test_stale_report_is_removed_when_the_run_produces_none(self):
        stale = self.dir / "keep.rpt"
        stale.write_text(ONE_UNEXCLUDED_ERROR)
        code, _, err = self.run_check("--report", str(stale), env={"FAKE_ERC_REPORT_FILE": None})
        self.assertEqual(code, 3)
        self.assertIn("no ERC report", err)
        self.assertFalse(stale.exists(), "a stale report must not survive a failed run")

    def test_exit_code_5_with_a_parsable_report_is_not_a_crash(self):
        # kicad-cli returns 5 when violations exist; the report is the authority.
        code, out, _ = self.run_check(env={"FAKE_ERC_EXIT": "5"})
        self.assertEqual(code, 0, out)

    def test_missing_tool_is_exit_3_with_a_clear_message(self):
        code, _, err = self.run_check(env={"KICAD_CLI": str(self.dir / "absent")})
        self.assertEqual(code, 3)
        self.assertIn("KICAD_CLI", err)
        self.assertIn("ERC CANNOT BE VERIFIED", err)

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
        code, _, _ = self.run_check()
        self.assertEqual(code, 0)
        after = {p: digest(p) for p in self.project.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_scratch_directory_is_private_and_cleaned_up(self):
        self.run_check()
        leftovers = [p for p in self.scratch_root.iterdir() if p.is_dir()]
        self.assertEqual(leftovers, [])
        self.assertEqual(
            [p for p in self.scratch_root.rglob("*") if p.is_file()],
            [],
            "the per-run scratch tree must not survive a run",
        )

    def test_two_runs_do_not_share_a_report(self):
        first = self.dir / "first.rpt"
        second = self.dir / "second.rpt"
        self.report_file.write_text(ALL_EXCLUDED)
        self.assertEqual(self.run_check("--report", str(first))[0], 0)
        self.report_file.write_text(ONE_UNEXCLUDED_ERROR)
        self.assertEqual(self.run_check("--report", str(second))[0], 1)
        self.assertEqual(first.read_text(), ALL_EXCLUDED)
        self.assertEqual(second.read_text(), ONE_UNEXCLUDED_ERROR)


class LibraryTableTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        self.project = self.root / "proj"
        self.library = self.project / "library"
        (self.library / "jclpcb.pretty").mkdir(parents=True)
        (self.library / "jclpcb.kicad_sym").write_text("(kicad_symbol_lib)\n")

    def table(self, *uris):
        lines = ["(fp_lib_table", "  (version 7)"]
        for name, uri in uris:
            lines.append(f'  (lib (name "{name}")(type "KiCad")(uri "{uri}")(options "")(descr ""))')
        lines.append(")")
        return "\n".join(lines) + "\n"

    def test_repo_local_library_is_rebased_to_project(self):
        text = self.table(("jclpcb", "/Users/someone/hardware/library/jclpcb.pretty"))
        new, report = e.normalise_lib_table("fp-lib-table", text, str(self.project),
                                            str(self.library), [])
        self.assertEqual(report["rebased_repo"], ["jclpcb"])
        self.assertEqual(report["unresolved"], [])
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

    def test_entries_beyond_lib_lines_are_preserved(self):
        text = self.table(("jclpcb", "/Users/someone/hardware/library/jclpcb.pretty")) + "; comment\n"
        new, _ = e.normalise_lib_table("fp-lib-table", text, str(self.project),
                                       str(self.library), [])
        self.assertIn("; comment", new)
        self.assertTrue(new.startswith("(fp_lib_table"))


if __name__ == "__main__":
    unittest.main()
