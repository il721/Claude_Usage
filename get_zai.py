"""
Query z.ai GLM Coding Plan quota and print '<5h_pct>|<week_pct>|<reset5h>|<resetweek>'
as bare integers (no '%') plus formatted local reset times. The z.ai quota endpoint
returns data.limits[]; TOKENS_LIMIT entries each carry a direct `percentage` and a
`nextResetTime` (epoch ms), plus a `number` that identifies the window: number==5 is
the 5-hour window, number==1 is the weekly (1 week) window. Identity comes from `number`,
not reset time — weekly can reset sooner than 5-hour, so sorting by nextResetTime would
swap them. Reset-time format mirrors get_limits.py for 5h ("HH:MM", 24h); weekly is a
countdown ("d:hh:mm"). On any failure prints nothing (the widget then shows '--' / blank).

API key (first found wins):
  env ZAI_API_KEY | GLM_API_KEY | ANTHROPIC_AUTH_TOKEN
  file <USERPROFILE>/.claude/widget_zai_key.txt

Raw response is also written to <USERPROFILE>/.claude/widget_zai_raw.json for debugging.
Endpoint documented by community tools (glm-usage-vscode, openusage).
"""
import os, json, time, urllib.request
from datetime import datetime

ENDPOINT = "https://api.z.ai/api/monitor/usage/quota/limit"

def _key():
    for v in ("ZAI_API_KEY", "GLM_API_KEY", "ANTHROPIC_AUTH_TOKEN"):
        s = os.environ.get(v)
        if s and s.strip():
            return s.strip()
    kf = os.path.join(os.environ.get("USERPROFILE", ""), ".claude", "widget_zai_key.txt")
    try:
        with open(kf, encoding="utf-8") as f:
            s = f.read().strip()
            if s:
                return s
    except Exception:
        pass
    return ""

def _fetch(k):
    req = urllib.request.Request(ENDPOINT, headers={"Authorization": k, "Content-Type": "application/json"})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))  # bypass system SOCKS proxy
    with opener.open(req, timeout=12) as r:
        return r.read().decode("utf-8", "replace")

def _fmt_time(ms):
    try:
        dt = datetime.fromtimestamp(ms / 1000)
        return f"{dt.hour:02d}:{dt.minute:02d}"
    except Exception:
        return ""

def _countdown(ms):
    try:
        rem = ms / 1000 - time.time()
        if rem < 0:
            rem = 0
        d  = int(rem // 86400)
        h  = int((rem % 86400) // 3600)
        mi = int((rem % 3600) // 60)
        return f"{d}:{h:02d}:{mi:02d}"
    except Exception:
        return ""

def main():
    k = _key()
    if not k:
        return
    try:
        body = _fetch(k)
    except Exception:
        return
    try:
        sf = os.path.join(os.environ.get("USERPROFILE", ""), ".claude", "widget_zai_raw.json")
        with open(sf, "w", encoding="utf-8") as f:
            f.write(body)
    except Exception:
        pass
    try:
        limits = (json.loads(body).get("data") or {}).get("limits") or []
    except Exception:
        return
    five = week = None   # (reset, pct) — identified by window `number`, NOT reset time
    for l in limits:
        if l.get("type") != "TOKENS_LIMIT":
            continue
        try:
            pct   = int(round(float(l.get("percentage") or 0)))
            reset = int(l.get("nextResetTime") or 0)
            num   = int(l.get("number") or 0)
        except Exception:
            continue
        if num == 5:        # 5-hour window
            five = (reset, pct)
        elif num == 1:      # weekly window (1 week)
            week = (reset, pct)
    if five is None or week is None:
        return
    r5, p5 = five
    rw, pw = week
    print(f"{p5}|{pw}|{_fmt_time(r5)}|{_countdown(rw)}")

if __name__ == "__main__":
    main()
