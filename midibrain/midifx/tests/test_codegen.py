#!/usr/bin/env python3
"""Codegen regenerate-and-diff test for midifx/gen/gen_params.py.

The generator must produce byte-identical committed output, and it must refuse
a schema that moves the frozen 0x19 positional order. Both alarms are exercised
on a temporary copy so the committed tree is never touched.
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
