#!/usr/bin/env python3
"""Claude Code hook -> Claude Usage Widget alert bridge.

Claude Code runs this on lifecycle events and passes the hook payload as JSON on
stdin (we only need `session_id`). The desired action is the first CLI arg:

    widget_signal.py ask     # Notification  -> session needs input  (flashing)
    widget_signal.py done    # Stop          -> session finished work (solid)
    widget_signal.py clear   # UserPromptSubmit / SessionEnd -> you responded

It writes/removes one flag file per session in ~/.claude/widget-alerts/, which the
widget polls. Always exits 0 so it never blocks Claude Code.
"""
import sys
import os
import json
import pathlib


def main() -> int:
    action = sys.argv[1] if len(sys.argv) > 1 else ""

    try:
        data = json.load(sys.stdin)
    except Exception:
        data = {}

    # session_id is a UUID; fall back to a constant so a missing id still works.
    sid = str(data.get("session_id") or "unknown")
    sid = "".join(c for c in sid if c.isalnum() or c in "-_") or "unknown"

    alerts = pathlib.Path(os.path.expanduser("~")) / ".claude" / "widget-alerts"
    flag = alerts / (sid + ".flag")

    if action in ("ask", "done"):
        alerts.mkdir(parents=True, exist_ok=True)
        flag.write_text(action, encoding="utf-8")
    elif action == "clear":
        try:
            flag.unlink()
        except FileNotFoundError:
            pass

    return 0


if __name__ == "__main__":
    sys.exit(main())
