#!/usr/bin/env python3
"""Codegen regenerate-and-diff test for midifx/gen/gen_params.py.

The generator must produce byte-identical committed output, and it must refuse
a schema that moves the frozen 0x19 positional order. Both alarms are exercised
on a temporary copy so the committed tree is never touched. The fixture test
proves the schema-driven UI rule: a brand-new pipe type reaches the generated
editor metadata with no app or QML change.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

MIDIFX = Path(__file__).resolve().parent.parent
SCRIPT = MIDIFX / "gen" / "gen_params.py"
FIXTURE = Path(__file__).resolve().parent / "fixtures" / "test-pipe-type.json"


def run(script: Path, *args: str) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        [sys.executable, str(script), *args],
        capture_output=True,
        text=True,
        check=False,
    )


class CodegenTest(unittest.TestCase):
    def test_committed_output_matches_schema(self) -> None:
        result = run(SCRIPT, "--check")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("match pipes.json", result.stdout)

    def test_stale_generated_file_fails_loudly(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp) / "midifx"
            shutil.copytree(MIDIFX, tree)
            header = tree / "gen" / "generated" / "midifx_params.hpp"
            header.write_text("// stale\n", encoding="utf-8")
            result = run(tree / "gen" / "gen_params.py", "--check")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("STALE", result.stderr)
            self.assertIn("midifx_params.hpp", result.stderr)

    def test_stale_ui_metadata_fails_loudly(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp) / "midifx"
            shutil.copytree(MIDIFX, tree)
            header = tree / "gen" / "generated" / "midifx_ui.hpp"
            header.write_text("// stale\n", encoding="utf-8")
            result = run(tree / "gen" / "gen_params.py", "--check")
            self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
            self.assertIn("STALE", result.stderr)
            self.assertIn("midifx_ui.hpp", result.stderr)

    def test_new_pipe_type_needs_no_ui_code(self) -> None:
        """A throwaway schema type must reach the generated editor metadata.

        This is the codegen acceptance proof: the type exists only in the
        fixture plus the schema/wire input the generator receives, and the
        generated tables describe its five editors (toggle, choice and three
        arc knobs with the % / st / note formats). Nothing in pi/app or the QML
        knows the type exists.
        """
        fixture = json.loads(FIXTURE.read_text(encoding="utf-8"))["pipe"]
        schema = json.loads((MIDIFX / "pipes.json").read_text(encoding="utf-8"))
        schema["pipes"].append(fixture)
        with tempfile.TemporaryDirectory() as tmp:
            work = Path(tmp)
            schema_path = work / "pipes.json"
            schema_path.write_text(json.dumps(schema), encoding="utf-8")
            wire = [line for line in (MIDIFX / "wire-order.txt").read_text(encoding="utf-8").splitlines()]
            wire.append("17 " + fixture["key"] + " " + " ".join(param["key"] for param in fixture["params"]))
            wire_path = work / "wire-order.txt"
            wire_path.write_text("\n".join(wire) + "\n", encoding="utf-8")
            out = work / "generated"
            result = run(SCRIPT, "--schema", str(schema_path), "--wire", str(wire_path),
                         "--out", str(out))
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

            ui = (out / "midifx_ui.hpp").read_text(encoding="utf-8")
            self.assertIn("kUiTestRamp[]", ui)
            table = ui.split("inline constexpr ParamUiSpec kUiTestRamp[] = {", 1)[1]
            table = table.split("};", 1)[0]
            rows = [row.strip() for row in table.splitlines() if row.strip()]
            self.assertEqual(len(rows), len(fixture["params"]), table)
            expected = [
                ("ValueFormat::Bool", "EditorKind::Toggle"),
                ("ValueFormat::Choice", "EditorKind::Choice"),
                ("ValueFormat::Percent", "EditorKind::Knob"),
                ("ValueFormat::Semitones", "EditorKind::Knob"),
                ("ValueFormat::Note", "EditorKind::Knob"),
            ]
            for row, (value_format, editor) in zip(rows, expected):
                self.assertIn(value_format, row)
                self.assertIn(editor, row)
            self.assertIn("kUiTestRamp, 5", ui)
            self.assertIn("kPipeUiCount = 17", ui)

            header = (out / "midifx_params.hpp").read_text(encoding="utf-8")
            self.assertIn("kPipeCount = 17", header)
            self.assertIn('"TEST RAMP", "TST", "clocked", PipeRole::Modifier', header)
            self.assertIn("\"TEST NOTE\", ParamKind::Int, 0, 127, 60, \"\", nullptr", header)
            self.assertIn("kChoicesTestRampShape[] = {\"RAMP\", \"STEP\", \"SINE\"}", header)

    def test_missing_pipe_role_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp) / "midifx"
            shutil.copytree(MIDIFX, tree)
            schema_path = tree / "pipes.json"
            schema = json.loads(schema_path.read_text(encoding="utf-8"))
            del schema["pipes"][3]["role"]
            schema_path.write_text(json.dumps(schema), encoding="utf-8")
            result = run(tree / "gen" / "gen_params.py", "--check")
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn("role must be one of", result.stderr)

    def test_unknown_param_format_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp) / "midifx"
            shutil.copytree(MIDIFX, tree)
            schema_path = tree / "pipes.json"
            schema = json.loads(schema_path.read_text(encoding="utf-8"))
            note_range = next(pipe for pipe in schema["pipes"] if pipe["key"] == "noteRange")
            note_range["params"][0]["format"] = "guitar-tab"
            schema_path.write_text(json.dumps(schema), encoding="utf-8")
            result = run(tree / "gen" / "gen_params.py", "--check")
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn("format must be one of", result.stderr)

    def test_reordered_positional_params_are_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp) / "midifx"
            shutil.copytree(MIDIFX, tree)
            schema_path = tree / "pipes.json"
            schema = json.loads(schema_path.read_text(encoding="utf-8"))
            arp = next(pipe for pipe in schema["pipes"] if pipe["key"] == "arp")
            arp["params"][0], arp["params"][1] = arp["params"][1], arp["params"][0]
            schema_path.write_text(json.dumps(schema), encoding="utf-8")
            result = run(tree / "gen" / "gen_params.py", "--check")
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn("frozen wire order", result.stderr)

    def test_channel_scope_out_of_range_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp) / "midifx"
            shutil.copytree(MIDIFX, tree)
            schema_path = tree / "pipes.json"
            schema = json.loads(schema_path.read_text(encoding="utf-8"))
            schema["channelScope"]["default"] = 70000
            schema_path.write_text(json.dumps(schema), encoding="utf-8")
            result = run(tree / "gen" / "gen_params.py", "--check")
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn("channelScope.default", result.stderr)

    def test_default_outside_range_is_refused(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            tree = Path(tmp) / "midifx"
            shutil.copytree(MIDIFX, tree)
            schema_path = tree / "pipes.json"
            schema = json.loads(schema_path.read_text(encoding="utf-8"))
            chance = next(pipe for pipe in schema["pipes"] if pipe["key"] == "chance")
            chance["params"][0]["default"] = 101
            schema_path.write_text(json.dumps(schema), encoding="utf-8")
            result = run(tree / "gen" / "gen_params.py", "--check")
            self.assertEqual(result.returncode, 2, result.stdout + result.stderr)
            self.assertIn("outside", result.stderr)


if __name__ == "__main__":
    unittest.main()
