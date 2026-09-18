#!/usr/bin/env python3
from __future__ import annotations

import unittest
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import osd_cdc  # noqa: E402


class OsdCdcTests(unittest.TestCase):
    def test_command_for_state(self) -> None:
        self.assertEqual(osd_cdc.command_for_state("idle"), "O!")
        self.assertEqual(osd_cdc.command_for_state(None), "O!")
        self.assertEqual(osd_cdc.command_for_state("developing"), "O #2AD4FF WORKING")
        self.assertEqual(osd_cdc.command_for_state("thinking"), "O #FFB020 THINK...")
        self.assertEqual(osd_cdc.command_for_state("confirming"), "O #FF4D6A ASK?")
        self.assertEqual(osd_cdc.command_for_state("completed"), "O #B794F6 INPUT")
        self.assertEqual(osd_cdc.command_for_state("weird"), "O #FF4D6A ERR")


if __name__ == "__main__":
    unittest.main()
