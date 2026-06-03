"""
Reads ~/.claude/widget_limits.json and prints:
  SESSION|session_pct|session_reset|week_pct|week_reset    (a live Claude Code session is writing the file)
  EXTENSION|session_pct|session_reset|week_pct|week_reset  (no session; file fed by the Chrome extension)
  FALLBACK                                                 (file missing/stale -> widget uses ccusage)

widget_limits.json is written natively by Claude Code while a session runs, and by
the Chrome extension otherwise. Both share the same schema, so we read it the same
way and only the SESSION/EXTENSION tag differs.

Response format from /api/organizations/{uuid}/usage:
  { "five_hour":  { "utilization": 60, "resets_at": "2026-05-28T19:10:01+00:00" },
    "seven_day":  { "utilization": 19, "resets_at": "2026-06-02T07:00:01+00:00" } }
"""
import json, os, sys, time
from datetime import datetime

CLAUDE_DIR = os.path.join(os.environ.get('USERPROFILE', ''), '.claude')
DATA_FILE  = os.path.join(CLAUDE_DIR, 'widget_limits.json')
PROJECTS   = os.path.join(CLAUDE_DIR, 'projects')
MAX_AGE          = 3600  # 1 hour — file older than this is considered stale
SESSION_FRESH_SEC = 180  # a live session appends to its transcript within this window

def session_active() -> bool:
    """True if a Claude Code session is running, inferred from a recently-written
    transcript (~/.claude/projects/**/*.jsonl). Walks with an early exit so it stays
    cheap even with many project logs."""
    if not os.path.isdir(PROJECTS):
        return False
    cutoff = time.time() - SESSION_FRESH_SEC
    for root, _dirs, files in os.walk(PROJECTS):
        for name in files:
            if not name.endswith('.jsonl'):
                continue
            try:
                if os.path.getmtime(os.path.join(root, name)) >= cutoff:
                    return True
            except OSError:
                continue
    return False

def local_time(iso: str) -> str:
    if not iso:
        return ''
    try:
        dt  = datetime.fromisoformat(iso).astimezone()
        h   = dt.hour % 12 or 12
        ampm = 'pm' if dt.hour >= 12 else 'am'
        return f'{h}:{dt.minute:02d}{ampm}'
    except Exception:
        return iso[:16]

def local_date_time(iso: str) -> str:
    if not iso:
        return ''
    try:
        dt   = datetime.fromisoformat(iso).astimezone()
        h    = dt.hour % 12 or 12
        ampm = 'pm' if dt.hour >= 12 else 'am'
        mon  = dt.strftime('%b')
        return f'{mon} {dt.day}, {h}{ampm}'
    except Exception:
        return iso[:16]

def main():
    if not os.path.exists(DATA_FILE):
        print('FALLBACK'); return
    if time.time() - os.path.getmtime(DATA_FILE) > MAX_AGE:
        print('FALLBACK'); return

    with open(DATA_FILE, encoding='utf-8') as f:
        data = json.load(f)

    fh = data.get('five_hour')  or {}
    sd = data.get('seven_day')  or {}

    sp = fh.get('utilization')
    sr = local_time(fh.get('resets_at', ''))
    wp = sd.get('utilization')
    wr = local_date_time(sd.get('resets_at', ''))

    if sp is None and wp is None:
        print('FALLBACK'); return

    tag = 'SESSION' if session_active() else 'EXTENSION'
    print(f'{tag}|{sp or 0}|{sr}|{wp or 0}|{wr}')

if __name__ == '__main__':
    main()
