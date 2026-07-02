"""
Reads ~/.claude/widget_limits.json (written by the Chrome extension via Native Messaging)
and prints: session_pct|session_reset|week_pct|week_reset
or FALLBACK (tells widget to use ccusage instead).

Response format from /api/organizations/{uuid}/usage:
  { "five_hour":  { "utilization": 60, "resets_at": "2026-05-28T19:10:01+00:00" },
    "seven_day":  { "utilization": 19, "resets_at": "2026-06-02T07:00:01+00:00" } }
"""
import json, os, sys, time
from datetime import datetime

DATA_FILE = os.path.join(os.environ.get('USERPROFILE', ''), '.claude', 'widget_limits.json')
MAX_AGE   = 3600  # 1 hour

def local_time(iso: str) -> str:
    if not iso:
        return ''
    try:
        dt = datetime.fromisoformat(iso).astimezone()
        return f'{dt.hour:02d}:{dt.minute:02d}'
    except Exception:
        return iso[:16]

def countdown(iso: str) -> str:
    if not iso:
        return ''
    try:
        dt  = datetime.fromisoformat(iso).astimezone()
        rem = (dt - datetime.now().astimezone()).total_seconds()
        if rem < 0:
            rem = 0
        d  = int(rem // 86400)
        h  = int((rem % 86400) // 3600)
        mi = int((rem % 3600) // 60)
        return f'{d}:{h:02d}:{mi:02d}'
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
    wr = countdown(sd.get('resets_at', ''))

    if sp is None and wp is None:
        print('FALLBACK'); return

    print(f'{sp or 0}|{sr}|{wp or 0}|{wr}')

if __name__ == '__main__':
    main()
