#!/usr/bin/env python3
"""Tests for aihook/codex/dispatch.py — write failing tests first."""
from __future__ import annotations

import json
import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import dispatch  # noqa: E402


class DispatchTests(unittest.TestCase):
    def setUp(self) -> None:
        self._tmpdir = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmpdir.name)
        self.status_path = self.tmp / "agent-status.json"
        self.events_path = self.tmp / "events.jsonl"
        self.patches = [
            mock.patch.object(dispatch, "STATUS_PATH", str(self.status_path)),
            mock.patch.object(dispatch, "EVENTS_PATH", str(self.events_path)),
            mock.patch.object(dispatch, "DEBUG_LOG", str(self.tmp / "trace.log")),
            mock.patch("osd_cdc.push_state", return_value="O ok\r\n"),
        ]
        for p in self.patches:
            p.start()

    def tearDown(self) -> None:
        for p in self.patches:
            p.stop()
        self._tmpdir.cleanup()

    def _run(self, payload: dict) -> None:
        raw = json.dumps(payload)
        with mock.patch.object(sys, "stdin", mock.Mock(read=mock.Mock(return_value=raw))):
            dispatch.main()

    def test_user_prompt_sets_developing_and_appends_event(self) -> None:
        self._run(
            {
                "hook_event_name": "UserPromptSubmit",
                "session_id": "s1",
                "cwd": "/tmp/proj",
                "model": "gpt-test",
            }
        )
        status = json.loads(self.status_path.read_text())
        self.assertEqual(status["state"], "developing")
        self.assertTrue(status["active_session"])
        self.assertEqual(status["event"], "UserPromptSubmit")

        lines = self.events_path.read_text().strip().splitlines()
        self.assertEqual(len(lines), 1)
        event = json.loads(lines[0])
        self.assertEqual(event["hook_event_name"], "UserPromptSubmit")
        self.assertEqual(event["session_id"], "s1")
        self.assertIn("received_at", event)

    def test_pre_tool_use_records_tool_detail(self) -> None:
        # Establish active session first
        self._run({"hook_event_name": "UserPromptSubmit", "session_id": "s1"})
        self._run(
            {
                "hook_event_name": "PreToolUse",
                "session_id": "s1",
                "tool_name": "Bash",
                "tool_input": {"command": "ls -la /tmp"},
            }
        )
        status = json.loads(self.status_path.read_text())
        self.assertEqual(status["state"], "developing")
        self.assertEqual(status["last_tool"], "Bash")
        self.assertTrue(status["last_tool_detail"].startswith("ls -la"))

        events = [json.loads(l) for l in self.events_path.read_text().splitlines()]
        self.assertEqual(events[-1]["tool_name"], "Bash")

    def test_gated_event_without_session_still_logged(self) -> None:
        self._run(
            {
                "hook_event_name": "PreToolUse",
                "session_id": "bg",
                "tool_name": "Bash",
                "tool_input": {"command": "echo hi"},
            }
        )
        status = json.loads(self.status_path.read_text())
        self.assertFalse(status.get("active_session", False))
        # state should not become developing when gated
        self.assertNotEqual(status.get("state"), "developing")
        lines = self.events_path.read_text().strip().splitlines()
        self.assertEqual(len(lines), 1)
        event = json.loads(lines[0])
        self.assertTrue(event.get("suppressed"))


if __name__ == "__main__":
    unittest.main()
