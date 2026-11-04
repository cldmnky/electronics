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
        # the props between name and resistance are not fixed, so match across
        audio.write_text(re.sub(r'name="R4"(.{0,120}?)resistance="100k"',
                                r'name="R4"\1resistance="110k"', text, count=1, flags=re.S))
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

    def test_small_source_value_drift_is_detected(self):
        """0.4 % drift is inside the check_values window but is still drift."""
        audio = self.sections / "audio.tsx"
        text = audio.read_text()
        audio.write_text(re.sub(r'name="R4"(.{0,120}?)resistance="100k"',
                                r'name="R4"\1resistance="100400"', text, count=1, flags=re.S))
        problems = source_link.check_all(base_sections=self.sections)
        self.assertTrue(any("R4" in p and "100400" in p for p in problems), problems)
        self.assertTrue(any("exactly" in p for p in problems), problems)

    def test_small_deck_value_drift_is_detected(self):
        """The deck side is held to the same exact-nominal rule."""
        deck = copy_deck("audio_out.cir", self.tmp,
                         edit=lambda t: t.replace("R13 U3A_IN_N NET_C8_2 33k",
                                                  "R13 U3A_IN_N NET_C8_2 33100"))
        boards = source_link.load_boards()
        problems = source_link.check_deck(deck, source_link.DECK_LINKS["audio_out.cir"],
                                          "audio_out.cir", boards)
        self.assertTrue(any("R13" in p and "33100" in p for p in problems), problems)

    def test_small_instance_parameter_drift_is_detected(self):
        deck = copy_deck("cv_inout.cir", self.tmp,
                         edit=lambda t: t.replace("RF1=49.9k", "RF1=49.95k"))
        boards = source_link.load_boards()
        problems = source_link.check_deck_params(deck, source_link.DECK_PARAMS["cv_inout.cir"],
                                                boards)
        self.assertTrue(any("RF1" in p for p in problems), problems)

    def test_midi_source_value_is_detected(self):
        midi = self.sections / "midi.tsx"
        text = midi.read_text()
        midi.write_text(re.sub(r'name="R58"(.{0,200}?)resistance="180"',
                               r'name="R58"\1resistance="200"', text, count=1, flags=re.S))
        problems = source_link.check_all(base_sections=self.sections)
        self.assertTrue(any("R58" in p and "200" in p for p in problems), problems)

    def test_midi_source_connection_is_detected(self):
        midi = self.sections / "midi.tsx"
        text = midi.read_text()
        midi.write_text(text.replace('"drain": "net.NET_Q2_C"',
                                     '"drain": "net.NET_Q2_B"', 1))
        problems = source_link.check_all(base_sections=self.sections)
        self.assertTrue(any("XQ2" in p and "connection" in p for p in problems), problems)

    def test_midi_receiver_pin_net_is_detected(self):
        midi = self.sections / "midi.tsx"
        text = midi.read_text()
        midi.write_text(text.replace('"VO": "net.USART1_RX"', '"VO": "net.USART1_TX"', 1))
        problems = source_link.check_all(base_sections=self.sections)
        self.assertTrue(any("USART1_RX" in p for p in problems), problems)

    def test_midi_deck_value_is_detected(self):
        deck = copy_deck("midi_out.cir", self.tmp,
                         edit=lambda t: t.replace("R59  J_MIDI_OUT_RING NET_Q2_C 33",
                                                  "R59  J_MIDI_OUT_RING NET_Q2_C 39"))
        boards = source_link.load_boards()
        problems = source_link.check_deck(deck, source_link.DECK_LINKS["midi_out.cir"],
                                          "midi_out.cir", boards)
        self.assertTrue(any("R59" in p and "39" in p for p in problems), problems)

    def test_nominal_values_match_to_parsing_precision(self):
        self.assertTrue(source_link.same_value(30100.0, 30.1e3))
        self.assertTrue(source_link.same_value(2.2e3, 2.2 * 10 ** 3))
        self.assertFalse(source_link.same_value(33000.0, 33100.0))
        self.assertFalse(source_link.same_value(100000.0, 100400.0))
        self.assertFalse(source_link.same_value(4.99e4, 4.995e4))

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

    def test_midi_deck_assumption_numbers_are_carried(self):
        deck = copy_deck("midi_out.cir", self.tmp,
                         edit=lambda t: t.replace("645p", "800p"))
        problems = models.check_midi_assumptions(deck)
        self.assertTrue(any("AO3401A_CISS_F" in p for p in problems), problems)

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
        runs = list(work.glob("audio_in-*/out/audio_in_ac.dat"))
        self.assertEqual(len(runs), 1, f"expected one fresh run directory, got {runs}")
        after = {path.name: (path.stat().st_size, path.stat().st_mtime_ns)
                 for path in OUT.glob("*")}
        self.assertEqual(before, after, "the run must not touch the committed out/")

    @unittest.skipUnless(NGSPICE, "ngspice not installed")
    def test_existing_workdir_is_never_deleted(self):
        """--workdir may hold anything: a fresh child is made, nothing removed."""
        root = self.tmp / "wroot"
        (root / "keepme").mkdir(parents=True)
        sentinel = root / "keepme" / "SENTINEL"
        sentinel.write_text("keep\n")
        decoy = root / "audio_in"           # same name as the deck, but a file
        decoy.write_text("not a directory\n")
        code = run_checks.main(["--deck", "audio_in", "--workdir", str(root)])
        self.assertEqual(code, 0)
        self.assertEqual(sentinel.read_text(), "keep\n", "a caller's tree was changed")
        self.assertEqual(decoy.read_text(), "not a directory\n")
        children = [path for path in root.iterdir()
                    if path.is_dir() and path.name.startswith("audio_in-")]
        self.assertEqual(len(children), 1, children)
        self.assertTrue((children[0] / "out" / "audio_in_ac.dat").is_file())
        # a second run must not delete the first run's directory either
        self.assertEqual(run_checks.main(["--deck", "audio_in", "--workdir", str(root)]), 0)
        again = [path for path in root.iterdir()
                 if path.is_dir() and path.name.startswith("audio_in-")]
        self.assertEqual(len(again), 2, "the previous run directory was removed")
        self.assertTrue((children[0] / "out" / "audio_in_ac.dat").is_file())

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
