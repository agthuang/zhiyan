#!/usr/bin/env python3
from __future__ import annotations
"""AI Hook Codex dispatcher — status for CodexBar + event stream for the live web UI."""

import fcntl
import json
import os
import sys
import tempfile
import time
from datetime import datetime, timedelta, timezone
from pathlib import Path

_HERE = Path(__file__).resolve().parent
STATUS_PATH = os.path.expanduser("~/.codex/agent-status.json")
EVENTS_PATH = str(_HERE / "data" / "events.jsonl")
DEBUG_LOG = str(_HERE / "data" / "dispatch_trace.log")

EVENT_STATE_MAP = {
    "SessionStart": "idle",
    "UserPromptSubmit": "developing",
    "SubagentStart": "developing",
    "PreToolUse": "developing",
    "PostToolUse": "developing",  # leave ASK? after permission / tool done
    "PermissionRequest": "confirming",
    "PreCompact": "thinking",
    "PostCompact": "thinking",
    "Stop": "completed",
}

USER_INTERACTION_TOOLS = {
    "request_user_input",
    "AskUser",
}

TOOL_EVENTS = {"PreToolUse", "PostToolUse"}
ACTIVE_SESSION_GATED = {
    "PreToolUse",
    "PostToolUse",
    "PermissionRequest",
    "SubagentStart",
    "PreCompact",
    "PostCompact",
}


def trace(msg: str) -> None:
    try:
        Path(DEBUG_LOG).parent.mkdir(parents=True, exist_ok=True)
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        with open(DEBUG_LOG, "a", encoding="utf-8") as f:
            f.write(f"[{ts}] {msg}\n")
    except Exception:
        pass


def extract_tool_detail(event_name: str, data: dict) -> tuple[str | None, str | None]:
    tool_name = data.get("tool_name")
    tool_input = data.get("tool_input") or {}

    if tool_name:
        trace(f"EVENT={event_name} TOOL={tool_name}")

    if not tool_name:
        return None, None

    if tool_name == "apply_patch":
        cmd = tool_input.get("command", "")
        detail = cmd.split("\n")[0][:80] if cmd else "apply_patch"
        return tool_name, detail

    if tool_name == "Bash":
        cmd = tool_input.get("command", "")
        detail = cmd[:60] + ("..." if len(cmd) > 60 else "")
        return tool_name, detail

    return tool_name, tool_name


def read_existing_status() -> dict:
    try:
        with open(STATUS_PATH, "r", encoding="utf-8") as f:
            return json.load(f)
    except (FileNotFoundError, json.JSONDecodeError, OSError):
        return {}


def write_status_atomic(status: dict) -> None:
    dir_name = os.path.dirname(STATUS_PATH) or "."
    os.makedirs(dir_name, exist_ok=True)
    fd, tmp_path = tempfile.mkstemp(dir=dir_name, suffix=".tmp")
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            fcntl.flock(f, fcntl.LOCK_EX)
            json.dump(status, f, ensure_ascii=False, indent=2)
            f.flush()
            os.fsync(f.fileno())
            fcntl.flock(f, fcntl.LOCK_UN)
        os.replace(tmp_path, STATUS_PATH)
    except Exception:
        try:
            os.unlink(tmp_path)
        except OSError:
            pass
        raise


def append_event(record: dict) -> None:
    path = Path(EVENTS_PATH)
    path.parent.mkdir(parents=True, exist_ok=True)
    line = json.dumps(record, ensure_ascii=False) + "\n"
    with open(path, "a", encoding="utf-8") as f:
        fcntl.flock(f, fcntl.LOCK_EX)
        f.write(line)
        f.flush()
        fcntl.flock(f, fcntl.LOCK_UN)


def build_event_record(data: dict, *, suppressed: bool, state: str | None) -> dict:
    tz = timezone(timedelta(hours=8))
    record = {
        "received_at": datetime.now(tz).isoformat(),
        "hook_event_name": data.get("hook_event_name", ""),
        "session_id": data.get("session_id"),
        "turn_id": data.get("turn_id"),
        "cwd": data.get("cwd"),
        "model": data.get("model"),
        "tool_name": data.get("tool_name"),
        "permission_mode": data.get("permission_mode"),
        "state": state,
        "suppressed": suppressed,
    }
    tool_input = data.get("tool_input")
    if isinstance(tool_input, dict) and tool_input:
        # Keep a short preview only — avoid dumping huge payloads into the log.
        preview = {}
        for key, value in list(tool_input.items())[:8]:
            text = value if isinstance(value, str) else json.dumps(value, ensure_ascii=False)
            preview[key] = text[:200] + ("…" if len(text) > 200 else "")
        record["tool_input_preview"] = preview
    return record


def main() -> None:
    try:
        raw = sys.stdin.read()
        data = json.loads(raw) if raw.strip() else {}
    except json.JSONDecodeError:
        sys.exit(0)

    event_name = data.get("hook_event_name", "")
    new_state = EVENT_STATE_MAP.get(event_name)

    trace(
        f"HOOK={event_name} state={new_state} tool={data.get('tool_name', '')} "
        f"mode={data.get('permission_mode', '')}"
    )

    status = read_existing_status()
    active_session = status.get("active_session", False)

    if event_name == "SessionStart":
        active_session = False
    elif event_name == "UserPromptSubmit":
        active_session = True
    elif event_name == "Stop":
        active_session = False

    status["active_session"] = active_session
    event_suppressed = False

    if event_name in ACTIVE_SESSION_GATED and not active_session:
        trace(f"BACKGROUND: {event_name} suppressed (no active session)")
        new_state = None
        event_suppressed = True

    if event_name and not event_suppressed:
        status["event"] = event_name

    if event_name == "PreToolUse" and new_state is not None:
        tool_name = data.get("tool_name", "")
        if tool_name in USER_INTERACTION_TOOLS:
            new_state = "confirming"
            trace(f"USER_INTERACTION: {tool_name} -> confirming")

    if event_name == "PermissionRequest" and new_state is not None:
        new_state = "confirming"
        trace("PERMISSION_REQUEST -> confirming")

    tz = timezone(timedelta(hours=8))
    status["timestamp"] = datetime.now(tz).isoformat()

    if sid := data.get("session_id"):
        status["session_id"] = sid
    if tid := data.get("turn_id"):
        status["turn_id"] = tid
    if cwd := data.get("cwd"):
        status["cwd"] = cwd
    if model := data.get("model"):
        status["model"] = model

    if new_state is not None:
        status["state"] = new_state
        trace(f"STATE -> {new_state}")
        try:
            import osd_cdc

            reply = osd_cdc.push_state(new_state)
            trace(f"OSD {osd_cdc.command_for_state(new_state)} → {reply.strip()!r}")
        except Exception as exc:
            trace(f"OSD skip: {exc}")

    if event_name in TOOL_EVENTS:
        status["last_tool_time"] = time.time()
        tool, detail = extract_tool_detail(event_name, data)
        if tool:
            status["last_tool"] = tool
        if detail:
            status["last_tool_detail"] = detail

    if event_name == "SessionStart":
        status.pop("last_tool", None)
        status.pop("last_tool_detail", None)
        status.pop("last_tool_time", None)

    write_status_atomic(status)
    append_event(
        build_event_record(data, suppressed=event_suppressed, state=new_state)
    )


if __name__ == "__main__":
    main()
