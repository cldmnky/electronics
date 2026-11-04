#!/usr/bin/env python3
"""Tests for the sim tools themselves: drift and bad data must be DETECTED.

    cd froemix/tools/sim && python3 -m unittest -v test_sim

Three groups:

  * source linkage: an altered source VALUE, an altered source CONNECTION, an
    altered deck value, an unexplained deck element and a changed deck net are
    each detected (they are what keeps the decks honest);
  * model provenance: a model limit that is not in the table is detected, and
    an ambiguous SPICE value (``1M`` = one milliohm) is rejected;
  * the runner and the reader: a missing ngspice, a missing ``wrdata`` file, a
    NaN, a ragged row, a missing ``.meas`` result and a non-comment first line
    each fail instead of passing quietly, and a failing assertion makes the
    runner exit nonzero.

The tests write only to temporary directories; the committed ``out/``
directory is history and must never be touched by them (one test asserts it).
Directories below ``$TMPDIR`` are removed afterwards.
"""

from __future__ import annotations

import os
import re
import shutil
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import assertions
import models
import run_checks
import simlib
import source_link

HERE = Path(__file__).resolve().parent
BASE_SECTIONS = source_link.BASE_SECTIONS
OUT = HERE / "out"


def ngspice() -> str | None:
    try:
        return simlib.find_ngspice()
    except simlib.SimError:
        return None


NGSPICE = ngspice()


def copy_deck(name: str, directory: Path, edit=None) -> Path:
    """Copy one deck into ``directory`` and optionally edit its text."""
    target = directory / name
    text = (HERE / name).read_text()
    if edit:
        text = edit(text)
    target.write_text(text)
    return target


class LinkageTests(unittest.TestCase):
    """A changed source must break the deck that models it."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="sim-test-"))
        self.sections = self.tmp / "sections"
        shutil.copytree(BASE_SECTIONS, self.sections)

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_altered_source_value_is_detected(self):
        audio = self.sections / "audio.tsx"
        text = audio.read_text()
        self.assertIn('resistance="100k"', text)
        audio.write_text(re.sub(r'name="R4"(\s+)resistance="100k"',
                                r'name="R4"\1resistance="110k"', text, count=1))
        problems = source_link.check_all(base_sections=self.sections)
        self.assertTrue(any("R4" in p and "110k" in p for p in problems), problems)

    def test_altered_source_connection_is_detected(self):
        audio = self.sections / "audio.tsx"
        text = audio.read_text()
        audio.write_text(text.replace('"pin2": "net.J_AUDIO_OUT_L"',
                                      '"pin2": "net.AUDIO_OUT_L"', 1))
        problems = source_link.check_all(base_sections=self.sections)
        self.assertTrue(any("R19" in p and "connection" in p for p in problems), problems)

    def test_altered_opamp_connection_is_detected(self):
        audio = self.sections / "cv.tsx"
        text = audio.read_text()
        audio.write_text(text.replace('"IN1P": "net.CV_OUT_1"',
                                      '"IN1P": "net.CV_OUT_2"', 1))
        problems = source_link.check_all(base_sections=self.sections)
        self.assertTrue(any("XOUT_NOM" in p or "XIN_MCP" in p for p in problems), problems)

    def test_altered_mux_pin_net_is_detected(self):
        main = self.sections / "main.tsx"
        text = main.read_text()
        main.write_text(text.replace('"X0": "net.M_ADC_0"', '"X0": "net.M_ADC_1"', 1))
        problems = source_link.check_all(base_sections=self.sections)
        self.assertTrue(any("M_ADC_0" in p for p in problems), problems)

    def test_deck_value_change_is_detected(self):
        deck = copy_deck("audio_in.cir", self.tmp,
                         edit=lambda t: t.replace("R8  U2B_IN_N NET_C7_2 10k",
                                                  "R8  U2B_IN_N NET_C7_2 12k"))
        links = source_link.DECK_LINKS["audio_in.cir"]
        boards = source_link.load_boards()
        problems = source_link.check_deck(deck, links, "audio_in.cir", boards)
        self.assertTrue(any("R8" in p for p in problems), problems)

    def test_deck_node_change_is_detected(self):
        deck = copy_deck("audio_in.cir", self.tmp,
                         edit=lambda t: t.replace("R9  NET_C7_2 AUDIO_IN_L 1k",
                                                  "R9  NET_C7_2 AUDIO_RET 1k"))
        boards = source_link.load_boards()
        problems = source_link.check_deck(deck, source_link.DECK_LINKS["audio_in.cir"],
                                          "audio_in.cir", boards)
        self.assertTrue(any("R9" in p and "connection" in p for p in problems), problems)

    def test_unexplained_deck_element_is_detected(self):
        deck = copy_deck("audio_in.cir", self.tmp,
                         edit=lambda t: t.replace("R9  NET_C7_2 AUDIO_IN_L 1k",
                                                  "R9  NET_C7_2 AUDIO_IN_L 1k\n"
                                                  "R99 NET_C7_2 0 10k"))
        boards = source_link.load_boards()
        problems = source_link.check_deck(deck, source_link.DECK_LINKS["audio_in.cir"],
                                          "audio_in.cir", boards)
        self.assertTrue(any("R99" in p and "neither linked" in p for p in problems),
                        problems)

    def test_current_source_passes(self):
        self.assertEqual(source_link.check_all(), [])


class ModelProvenanceTests(unittest.TestCase):
    """A model parameter outside the table is a failure, not a warning."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="sim-model-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_unknown_opamp_limit_is_detected(self):
        deck = copy_deck("audio_in.cir", self.tmp,
                         edit=lambda t: t.replace("A0=2e5 GBW=3e6 HEAD=1.5",
                                                  "A0=2e5 GBW=3e7 HEAD=1.5", 1))
        problems = models.check_models(deck)
        self.assertTrue(problems, "a 10x GBW change must not pass as provenance")
        self.assertIn("provenance", problems[0])

    def test_unknown_diode_parameter_is_detected(self):
        deck = copy_deck("front_audio.cir", self.tmp,
                         edit=lambda t: t.replace(".model D4148 D(IS=2.52n N=1.752)",
                                                  ".model D4148 D(IS=2.52n N=2.5)"))
        problems = models.check_models(deck)
        self.assertTrue(any("D4148" in p for p in problems), problems)

    def test_current_decks_pass_provenance(self):
        for deck in sorted(HERE.glob("*.cir")):
            with self.subTest(deck=deck.name):
                self.assertEqual(models.check_models(deck), [])
                self.assertEqual(models.check_mux_assumptions(deck), [])
                self.assertEqual(simlib.check_deck_values(deck), [])

    def test_bare_megohm_suffix_is_rejected(self):
        deck = copy_deck("sync_io.cir", self.tmp,
                         edit=lambda t: t.replace("R23 NET_Q1_1 0 1Meg",
                                                  "R23 NET_Q1_1 0 1M"))
        problems = simlib.check_deck_values(deck)
        self.assertTrue(any("ambiguous" in p for p in problems), problems)

    def test_spice_suffixes(self):
        self.assertEqual(simlib.spice_number("1Meg"), 1e6)
        self.assertEqual(simlib.spice_number("2.52n"), 2.52e-9)
        self.assertEqual(simlib.spice_number("10.3k"), 10300.0)
        self.assertAlmostEqual(simlib.spice_number("100nF"), 100e-9)
        with self.assertRaises(simlib.SimError):
            simlib.spice_number("1M")


class ReaderTests(unittest.TestCase):
    """Bad or absent output must raise, never read as a number."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="sim-read-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_missing_file(self):
        with self.assertRaises(simlib.SimError) as caught:
            simlib.read_wrdata(self.tmp / "nope.dat")
        self.assertIn("missing simulation output", str(caught.exception))

    def test_empty_and_header_only(self):
        path = self.tmp / "empty.dat"
        path.write_text("")
        with self.assertRaises(simlib.SimError):
            simlib.read_wrdata(path)
        path.write_text(" time  v(out)\n")
        with self.assertRaises(simlib.SimError):
            simlib.read_wrdata(path, layout="vectors")

    def test_nan_and_ragged_rows(self):
        path = self.tmp / "bad.dat"
        path.write_text("1.0 2.0 1.0 3.0\nnan 2.0 nan 3.0\n")
        with self.assertRaises(simlib.SimError) as caught:
            simlib.read_wrdata(path)
        self.assertIn("non-finite", str(caught.exception))
        path.write_text("1.0 2.0 1.0 3.0\n2.0 2.0 2.0\n")
        with self.assertRaises(simlib.SimError) as caught:
            simlib.read_wrdata(path)
        self.assertIn("columns", str(caught.exception))

    def test_mismatched_pair_scale(self):
        path = self.tmp / "scale.dat"
        path.write_text("1.0 2.0 9.0 3.0\n")
        with self.assertRaises(simlib.SimError) as caught:
            simlib.read_wrdata(path)
        self.assertIn("scale column", str(caught.exception))

    def test_valid_pairs_and_vectors(self):
        pairs = self.tmp / "pairs.dat"
        pairs.write_text("1.0 2.0 1.0 3.0\n2.0 4.0 2.0 6.0\n")
        x, columns = simlib.read_wrdata(pairs)
        self.assertEqual(x, [1.0, 2.0])
        self.assertEqual(columns, [[2.0, 4.0], [3.0, 6.0]])
        vectors = self.tmp / "vectors.dat"
        vectors.write_text(" time v(a) v(b)\n0.0 1.0 2.0\n1.0 3.0 4.0\n")
        x, columns = simlib.read_wrdata(vectors, layout="vectors")
        self.assertEqual(columns, [[1.0, 3.0], [2.0, 4.0]])

    def test_missing_measurement(self):
        run = simlib.DeckRun(deck="x", workdir=self.tmp, log=self.tmp / "x.log",
                             stderr=self.tmp / "x.stderr", returncode=0,
                             measures={"gain": 1.0})
        self.assertEqual(run.measure("gain"), 1.0)
        with self.assertRaises(simlib.SimError) as caught:
            run.measure("threshold")
        self.assertIn("no .meas result", str(caught.exception))

    def test_measure_parsing(self):
        text = ("meas ac gain_1k find vdb(out) at=1000\n"
                "gain_1k             =  -2.000939e+01\n"
                "vmax                =  5.9e-01 at=  9.99e-06\n"
                "Error: measure  gain_2k  find(AT) : out of interval\n")
        measures = simlib.read_measures(text)
        self.assertAlmostEqual(measures["gain_1k"], -20.00939)
        self.assertAlmostEqual(measures["vmax"], 0.59)
        self.assertEqual(simlib.log_errors(text),
                         ["Error: measure  gain_2k  find(AT) : out of interval"])

    def test_nan_measurement_is_rejected(self):
        run = simlib.DeckRun(deck="x", workdir=self.tmp, log=self.tmp / "x.log",
                             stderr=self.tmp / "x.stderr", returncode=0,
                             measures={"gain": float("nan")})
        with self.assertRaises(simlib.SimError):
            run.measure("gain")

    def test_first_line_must_be_a_comment(self):
        deck = self.tmp / "titled.cir"
        deck.write_text("V1 a 0 DC 1\nR1 a 0 1k\n.end\n")
        with self.assertRaises(simlib.SimError) as caught:
            simlib.run_deck(deck, self.tmp / "work", ngspice="/bin/false")
        self.assertIn("comment", str(caught.exception))


class RunnerTests(unittest.TestCase):
    """The runner must exit nonzero: no ngspice, bad data, failed assertion."""

    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="sim-run-"))

    def tearDown(self):
        shutil.rmtree(self.tmp, ignore_errors=True)

    def test_missing_ngspice_fails(self):
        code = run_checks.main(["--deck", "audio_in",
                                "--ngspice", str(self.tmp / "no-such-ngspice"),
                                "--workdir", str(self.tmp / "work")])
        self.assertEqual(code, 2, "a missing simulator must not look like success")

    def test_missing_ngspice_on_path(self):
        with mock.patch.dict(os.environ, {"PATH": str(self.tmp)}):
            with self.assertRaises(simlib.SimError) as caught:
                simlib.find_ngspice()
        self.assertIn("ngspice not found", str(caught.exception))

    def test_source_only_mode_needs_no_simulator(self):
        self.assertEqual(run_checks.main(["--source-only"]), 0)

    @unittest.skipUnless(NGSPICE, "ngspice not installed")
    def test_real_run_passes_and_writes_nothing_in_the_repo(self):
        before = {path.name: (path.stat().st_size, path.stat().st_mtime_ns)
                  for path in OUT.glob("*")}
        work = self.tmp / "work"
        code = run_checks.main(["--deck", "audio_in", "--workdir", str(work)])
        self.assertEqual(code, 0)
        self.assertTrue((work / "audio_in" / "out" / "audio_in_ac.dat").is_file())
        after = {path.name: (path.stat().st_size, path.stat().st_mtime_ns)
                 for path in OUT.glob("*")}
        self.assertEqual(before, after, "the run must not touch the committed out/")

    @unittest.skipUnless(NGSPICE, "ngspice not installed")
    def test_failing_assertion_returns_nonzero(self):
        def failing(run):
            return [assertions.Check("audio_in", "forced", "1", "2", False, "test")]

        with mock.patch.dict(assertions.DECK_CHECKS, {"audio_in": failing}):
            code = run_checks.main(["--deck", "audio_in", "--workdir", str(self.tmp / "w")])
        self.assertEqual(code, 1)

    @unittest.skipUnless(NGSPICE, "ngspice not installed")
    def test_broken_deck_output_fails(self):
        """A deck whose wrdata line is gone must fail, not print old numbers."""
        deck = copy_deck("audio_in.cir", self.tmp,
                         edit=lambda t: t.replace(
                             "wrdata out/audio_in_ac.dat vdb(AUDIO_IN_L) "
                             "vdb(NET_C2_2)", ""))
        result = simlib.run_deck(deck, self.tmp / "work", ngspice=NGSPICE)
        with self.assertRaises(simlib.SimError) as caught:
            assertions.audio_in(result)
        self.assertIn("missing simulation output", str(caught.exception))


class CheckTests(unittest.TestCase):
    def test_band_semantics(self):
        good = assertions.band("d", "x", 1.0, 0.9, 1.1, " V", "test")
        bad = assertions.band("d", "x", 1.2, 0.9, 1.1, " V", "test")
        self.assertTrue(good.ok)
        self.assertFalse(bad.ok)
        self.assertIn("FAIL", bad.line())
        self.assertIn("PASS", good.line())


if __name__ == "__main__":
    unittest.main(verbosity=2)
