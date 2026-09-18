#!/usr/bin/env bash
# Install project hooks.json into ~/.codex (backup first).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")" && pwd)"
SRC="$ROOT/hooks.json"
DEST="${HOME}/.codex/hooks.json"
mkdir -p "${HOME}/.codex"
if [[ -f "$DEST" ]]; then
  bak="$DEST.bak.$(date +%Y%m%d-%H%M%S)"
  cp "$DEST" "$bak"
  echo "backed up → $bak"
fi
# Refresh absolute path in hooks.json for this machine
python3 - <<PY
import json
from pathlib import Path
dispatch = Path("$ROOT/dispatch.py").resolve()
events = [
  "SessionStart", "UserPromptSubmit", "SubagentStart",
  "PreToolUse", "PostToolUse", "PermissionRequest", "Stop",
]
hooks = {
  "hooks": {
    name: [{"hooks": [{"type": "command", "command": f"/usr/bin/python3 {dispatch}", "timeout": 5}]}]
    for name in events
  }
}
text = json.dumps(hooks, indent=2) + "\n"
Path("$SRC").write_text(text, encoding="utf-8")
Path("$DEST").write_text(text, encoding="utf-8")
print("installed → $DEST")
print("dispatch  →", dispatch)
print("Next: open Codex and /hooks to trust the new hooks if prompted.")
PY
