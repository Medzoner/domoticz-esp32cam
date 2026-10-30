import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location(
    "check_playback_stack", Path(__file__).resolve().parents[2] / "scripts/check_playback_stack.py")
stack = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(stack)


def report(play=192, download=320, kind="static"):
    return (f"test.cpp:1:1:bool RecordingPlaybackServer::play(Client&)\t{play}\t{kind}\n"
            f"test.cpp:2:1:bool RecordingPlaybackServer::download(Client&)\t{download}\tstatic\n")


class PlaybackStackTests(unittest.TestCase):
    def test_small_static_frames_pass(self):
        self.assertEqual(len(stack.check(report())), 2)

    def test_old_stack_local_buffer_fails_for_both_routes(self):
        for play, download in ((4288, 320), (192, 4416)):
            with self.subTest(play=play, download=download), self.assertRaises(ValueError):
                stack.check(report(play, download))

    def test_missing_or_malformed_reports_fail_closed(self):
        for text in ("", "malformed", report().splitlines()[0]):
            with self.subTest(text=text), self.assertRaises(ValueError):
                stack.check(text)

    def test_dynamic_frames_are_not_treated_as_bounded(self):
        with self.assertRaises(ValueError):
            stack.check(report(kind="dynamic"))

    def test_other_frames_in_translation_unit_are_also_checked(self):
        with self.assertRaises(ValueError):
            stack.check(report() + "test.cpp:3:1:void helper()\t4096\tstatic\n")
