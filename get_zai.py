"""
Query z.ai GLM Coding Plan quota and print '<5h_pct>|<week_pct>|<reset5h>|<resetweek>'
as bare integers (no '%') plus formatted local reset times. The z.ai quota endpoint
returns data.limits[]; TOKENS_LIMIT entries each carry a direct `percentage` and a
`nextResetTime` (epoch ms). The 5-hour window resets sooner than the weekly window,
so sorting by nextResetTime ascending gives 5h first, weekly last. Reset-time format
mirrors get_limits.py for 5h ("HH:MM", 24h); weekly is a countdown ("d:hh:mm"). On any failure
prints nothing (the widget then shows '--' / blank).

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
    toks = []
    for l in limits:
        if l.get("type") != "TOKENS_LIMIT":
            continue
        try:
            pct = int(round(float(l.get("percentage") or 0)))
            reset = int(l.get("nextResetTime") or 0)
        except Exception:
            continue
        toks.append((reset, pct))
    if not toks:
        return
    toks.sort(key=lambda t: t[0])           # earliest reset first = 5h; latest = weekly
    r5, p5 = toks[0]
    rw, pw = toks[-1]
    print(f"{p5}|{pw}|{_fmt_time(r5)}|{_countdown(rw)}")

if __name__ == "__main__":
    main()
