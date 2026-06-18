#!/usr/bin/env python3
"""Claude Code hook -> Claude Usage Widget alert bridge.

Claude Code runs this on lifecycle events and passes the hook payload as JSON on
stdin (we only need `session_id`). The desired action is the first CLI arg:

    widget_signal.py ask     # Notification  -> session needs input  (flashing)
    widget_signal.py done    # Stop          -> session finished work (solid)
    widget_signal.py clear   # UserPromptSubmit / SessionEnd -> you responded

It writes/removes flag files in ~/.claude/widget-alerts/, which the widget polls.
`ask`/`done` write one flag for the current session; `clear` removes ALL flags
(see below). Always exits 0 so it never blocks Claude Code.
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
        # The user just submitted a prompt (or the session ended): they're
        # attending to Claude, so silence EVERY session's alert -- same
        # semantics as clicking the widget. Clearing only this session's flag
        # left alerts from other/older sessions lit (a second terminal, or a
        # session that ended without its SessionEnd hook firing -- flags linger
        # up to 2h), so the border only went dark on a manual click.
        for f in alerts.glob("*.flag"):
            try:
                f.unlink()
            except OSError:
                pass

    return 0


if __name__ == "__main__":
    sys.exit(main())
