# SPDX-License-Identifier: GPL-3.0-or-later
"""Protect the trace lifetime contract around PS5 VideoOut submission."""
from pathlib import Path
import unittest

SOURCE = Path(__file__).resolve().parents[1] / "src/moonlight_stream.cpp"


class FrameTraceOutcomeTest(unittest.TestCase):
    def test_displayed_requires_successful_flip_confirmation(self):
        source = SOURCE.read_text()
        begin = source.index("static int submit_presentation(")
        confirmed = source.index("static int complete_presentation(", begin)
        presenter = source.index("static void *video_present_thread(void *context)\n{", confirmed)
        submit = source[begin:confirmed]
        finish = source[confirmed:presenter]
        self.assertIn("item.trace->submit_us = finished;", submit)
        self.assertNotIn("item.trace->outcome = 1;", submit)
        failure = finish.index("if (result != 0)")
        marked = finish.index("item.trace->outcome = 3;", failure)
        success = finish.index("item.trace->outcome = 1;")
        self.assertLess(failure, marked)
        self.assertLess(marked, success)
        self.assertLess(finish.index("const uint64_t flipped_us", marked), success)
        self.assertIn("current.trace->outcome = 3;", source[presenter:])


if __name__ == "__main__":
    unittest.main()
