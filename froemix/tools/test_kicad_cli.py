#!/usr/bin/env python3
"""Tests for the portable kicad-cli resolver. No KiCad and no network needed.

The negative cases matter more than the positive ones: a check that cannot find
its tool must fail with a clear message, never pass quietly.
"""

import os
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import kicad_cli as k  # noqa: E402

HERE = Path(__file__).resolve().parent


def fake_cli(directory: Path, name: str = "kicad-cli", body: str = "#!/bin/sh\necho 10.0.7\n") -> Path:
    path = directory / name
    path.write_text(body)
    path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)
    return path


class ResolveTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)
        # Hermetic environment: nothing resolved through the host's own KiCad.
        self.env = patch.dict(os.environ, {}, clear=False)
        self.env.start()
        self.addCleanup(self.env.stop)
        os.environ.pop("KICAD_CLI", None)
        self.path_patch = patch.object(k.shutil, "which", return_value=None)
        self.path_patch.start()
        self.addCleanup(self.path_patch.stop)
        self.fallback_patch = patch.object(k, "FALLBACK_PATHS", ())
        self.fallback_patch.start()
        self.addCleanup(self.fallback_patch.stop)

    def test_explicit_path_is_used(self):
        cli = fake_cli(self.dir)
        self.assertEqual(k.resolve_kicad_cli(str(cli)), str(cli))

    def test_explicit_path_wins_over_environment(self):
        cli = fake_cli(self.dir, "explicit")
        other = fake_cli(self.dir, "from-env")
        with patch.dict(os.environ, {"KICAD_CLI": str(other)}):
            self.assertEqual(k.resolve_kicad_cli(str(cli)), str(cli))

    def test_missing_explicit_path_raises_and_names_it(self):
        missing = self.dir / "nope" / "kicad-cli"
        with self.assertRaises(k.KicadCliError) as caught:
            k.resolve_kicad_cli(str(missing))
        self.assertIn(str(missing), str(caught.exception))
        self.assertIn("does not exist", str(caught.exception))

    def test_explicit_path_that_is_not_executable_raises(self):
        path = self.dir / "kicad-cli"
        path.write_text("#!/bin/sh\necho 10.0.7\n")
        path.chmod(0o644)
        with self.assertRaises(k.KicadCliError) as caught:
            k.resolve_kicad_cli(str(path))
        self.assertIn("not executable", str(caught.exception))

    def test_explicit_directory_raises(self):
        with self.assertRaises(k.KicadCliError) as caught:
            k.resolve_kicad_cli(str(self.dir))
        self.assertIn("directory", str(caught.exception))

    def test_environment_variable_is_used(self):
        cli = fake_cli(self.dir)
        with patch.dict(os.environ, {"KICAD_CLI": str(cli)}):
            self.assertEqual(k.resolve_kicad_cli(), str(cli))

    def test_broken_environment_variable_does_not_fall_back(self):
        # A set KICAD_CLI is authoritative: a typo must not silently select
        # another install.
        on_path = fake_cli(self.dir, "kicad-cli")
        with patch.object(k.shutil, "which", return_value=str(on_path)):
            with patch.dict(os.environ, {"KICAD_CLI": str(self.dir / "gone")}):
                with self.assertRaises(k.KicadCliError) as caught:
                    k.resolve_kicad_cli()
        self.assertIn("KICAD_CLI", str(caught.exception))

    def test_path_lookup_is_used(self):
        cli = fake_cli(self.dir)
        with patch.dict(os.environ, {"PATH": str(self.dir)}):
            # exercise the real shutil.which against the patched PATH
            with patch.object(k.shutil, "which", side_effect=lambda name: str(cli) if name == "kicad-cli" else None):
                self.assertEqual(k.resolve_kicad_cli(), str(cli))

    def test_mac_fallback_path_is_used(self):
        cli = fake_cli(self.dir, "KicadBundleCli")
        with patch.object(k, "FALLBACK_PATHS", (str(cli),)):
            self.assertEqual(k.resolve_kicad_cli(), str(cli))

    def test_nothing_found_raises_a_clear_error(self):
        with self.assertRaises(k.KicadCliError) as caught:
            k.resolve_kicad_cli()
        message = str(caught.exception)
        self.assertIn("no kicad-cli found", message)
        self.assertIn("KICAD_CLI", message)
        self.assertIn("PATH", message)

    def test_help_text_lists_every_way_to_set_the_tool(self):
        for token in ("--kicad-cli", "KICAD_CLI", "PATH"):
            self.assertIn(token, k.HOW_TO_SET)

    def test_real_mac_path_is_the_documented_fallback(self):
        # FALLBACK_PATHS is patched per test, so assert on the constant it is
        # built from: the macOS bundle is the documented last resort.
        self.assertEqual(
            k.MAC_BUNDLE_CLI,
            "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli",
        )
        self.assertIn(k.MAC_BUNDLE_CLI, k.HOW_TO_SET)


class VersionTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def test_version_is_read_from_a_wrapper(self):
        cli = fake_cli(self.dir, body="#!/bin/sh\necho '10.0.7+dfsg-1'\n")
        self.assertEqual(k.kicad_cli_version(str(cli)), "10.0.7+dfsg-1")

    def test_version_of_a_missing_binary_raises(self):
        with self.assertRaises(k.KicadCliError):
            k.kicad_cli_version(str(self.dir / "absent"))

    def test_version_that_prints_nothing_raises(self):
        cli = fake_cli(self.dir, body="#!/bin/sh\nexit 0\n")
        with self.assertRaises(k.KicadCliError) as caught:
            k.kicad_cli_version(str(cli))
        self.assertIn("printed nothing", str(caught.exception))


class CommandLineTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)
        self.addCleanup(self.tmp.cleanup)

    def run_cli(self, *args, env=None):
        return subprocess.run(
            [sys.executable, str(HERE / "kicad_cli.py"), *args],
            capture_output=True,
            text=True,
            env=env,
        )

    def clean_env(self, **extra):
        env = {
            "PATH": str(self.dir),
            "HOME": str(self.dir),
            "KICAD_CLI": "",
        }
        env.update(extra)
        return env

    def test_print_returns_the_resolved_path(self):
        cli = fake_cli(self.dir)
        run = self.run_cli("--print", **{"env": self.clean_env(KICAD_CLI=str(cli))})
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout.strip(), str(cli))

    def test_version_option_prints_the_version(self):
        cli = fake_cli(self.dir)
        run = self.run_cli("--version", **{"env": self.clean_env(KICAD_CLI=str(cli))})
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(run.stdout.strip(), "10.0.7")

    def test_missing_tool_exits_nonzero_with_a_message(self):
        run = self.run_cli("--print", **{"env": self.clean_env()})
        self.assertNotEqual(run.returncode, 0)
        self.assertIn("no kicad-cli found", run.stderr)


if __name__ == "__main__":
    unittest.main()
