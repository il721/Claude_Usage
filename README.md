# Claude Usage Widget

A lightweight Win32 desktop widget (C++/WinAPI) that shows your **Claude.ai
rate‑limit usage** at a glance — the current 5‑hour session and 7‑day window,
with reset times — and falls back to **local Claude Code token usage** when
live limits aren't available.

| Full mode | Simple mode |
|-----------|-------------|
| ![Full widget](widget_full_view.png) | ![Simple widget](widget_simple_view.png) |

**Full mode** shows each limit as a progress bar with the percentage and the
reset time beneath it — session on top, week below. **Simple mode** is a compact
two‑cell view of just the numbers. Switch between them by
**double‑clicking** the widget or via the right‑click menu.

---

## How it works

Everything the widget shows comes from one file —
`%USERPROFILE%\.claude\widget_limits.json` (`five_hour` / `seven_day`
utilization + reset times). That file has **two producers**, and the widget
reads it the same way regardless of which one wrote it:

```
 (A) live Claude Code session            (B) no session: Chrome extension
     writes it natively, ~60s                 polls claude.ai /usage with
            │                                  first-party cookies, writes
            │                                  it via native_host/host.py
            ▼                                            │
 ┌────────────────────────────────────────────────────────────────┐
 │  %USERPROFILE%\.claude\widget_limits.json                       │
 │    five_hour.utilization / seven_day.utilization / resets_at    │
 └───────────────────────────────┬────────────────────────────────┘
                                 │ reads
                                 ▼
 ┌────────────────────────────────────────────────────────────────┐
 │ get_limits.py → SESSION|pct|reset|… / EXTENSION|… / FALLBACK     │
 └───────────────────────────────┬────────────────────────────────┘
                                 ▼
 ┌────────────────────────────────────────────────────────────────┐
 │ Claude_Usage.exe (main.cpp) — draws the bars + a source dot      │
 │   ● blue = session   ● grey = extension   ● amber = ccusage      │
 └────────────────────────────────────────────────────────────────┘
```

1. **Live Claude Code session (preferred).** While a `claude` session is
   running, Claude Code itself refreshes `widget_limits.json` every ~60 s — no
   extension, no open browser tab needed. `get_limits.py` detects this by
   checking for a transcript (`~/.claude/projects/**/*.jsonl`) written in the
   last 180 s and tags its output `SESSION`.
2. **Chrome extension (no session).** When no session is active, the extension
   keeps the file fresh: a content script fetches
   `/api/organizations/{uuid}/usage` with your first‑party cookies, a service
   worker (`background.js`) polls every minute via `chrome.alarms`, and
   `native_host/host.py` writes the same `widget_limits.json`. Tagged
   `EXTENSION`.
3. **`get_limits.py`** — reads the file and emits
   `SOURCE|session_pct|session_reset|week_pct|week_reset`, or `FALLBACK` if the
   JSON is missing or stale (>1 h old).
4. **ccusage fallback.** On `FALLBACK`, the widget runs `ccusage` for a
   cost‑based estimate of the current block and week instead. (Tagged by the
   amber dot.)
5. **`get_daily.py`** — separate source for the **More info…** popup. Scans
   `~/.claude/projects/**/*.jsonl` for the last 30 days and reports per‑day
   token counts and per‑model totals.
6. **The widget** (`main.cpp`) renders the bars, the small corner **source
   dot**, and refreshes on a timer.

> **The extension is now optional while you're using Claude Code** — a live
> session feeds the widget on its own. You only need the extension (and an open
> `claude.ai` tab) to keep numbers live when no session is running.

---

## Requirements

- Windows 10/11
- Python 3 on `PATH` (for the `get_*.py` helpers and the native host)
- Google Chrome — **optional**, only for the extension that keeps limits live
  when no Claude Code session is running
- To build from source: CLion's bundled MinGW GCC, or any `g++` with C++20

---

## Install (prebuilt)

1. Unzip `Claude_Usage_Widget.zip`.
2. Run **`setup.bat`**. It will:
   - copy `get_limits.py` / `get_daily.py` next to the `.exe`,
   - generate `native_host/com.claude.widget.json` with the correct absolute
     path, and
   - register the native messaging host under
     `HKCU\Software\Google\Chrome\NativeMessagingHosts`.
3. Start the widget: `cmake-build-debug\Claude_Usage.exe`. While a Claude Code
   session is running it works immediately — no browser needed.
4. *(Optional)* For live numbers when **no** session is running, load the Chrome
   extension: `chrome://extensions` → enable **Developer mode** → **Load
   unpacked** → select the `extension/` folder.

> The extension's ID must match the `allowed_origins` in the native host
> manifest. If you repack the extension and the ID changes, update
> `extension/manifest.json`'s key / `allowed_origins` accordingly.

---

## Build from source

```bash
g++ -std=gnu++20 -o cmake-build-debug/Claude_Usage.exe main.cpp \
    -luser32 -lgdi32 -mwindows
```

In this repo, prefer the **`/clion-cpp`** workflow (handles the MinGW `PATH`
fix and a detached launch) and **`/pack`** to rebuild
`Claude_Usage_Widget.zip`. See `build.bat` for the exact compiler invocation.

---

## Using the widget

- **Drag** anywhere to move it; its position is remembered between runs.
- **Source dot** (small dot in the top‑right corner) shows where the numbers
  come from: **blue** = live Claude Code session, **grey** = Chrome extension,
  **amber** = `ccusage` cost estimate.
- **Double‑click** to toggle between full and Simple mode. Simple mode anchors
  to the lower‑left corner of the full widget, so the bottom‑left stays put as
  the widget shrinks or grows.
- **Right‑click** for the menu:
  - **Refresh now** — re‑read the usage data immediately.
  - **Opacity** — pick a transparency level (25–100%).
  - **More info…** — an info popup with full reset details.
  - **Simple mode** — same toggle as double‑click (shows a ✓ when active).
  - **Exit**.

State is persisted under `%USERPROFILE%\.claude\` (window position, opacity,
`widget_simple.txt` for the Simple‑mode toggle).

---

## Project layout

| Path | Purpose |
|------|---------|
| `main.cpp` | The C++/WinAPI widget (rendering, menu, persistence) |
| `get_limits.py` | Live limits from `widget_limits.json` → widget |
| `get_daily.py` | Local Claude Code token usage (fallback) |
| `extension/` | Chrome extension (`manifest.json`, `background.js`, `content.js`) |
| `native_host/` | Native messaging bridge (`host.py`, `host.bat`) |
| `setup.bat` | One‑shot installer (copies files, writes manifest, registers host) |
| `build.bat` | Reference g++ build command |
| `Claude_Usage_Widget.zip` | Packaged, ready‑to‑deploy bundle |

---

## Notes

- `native_host/com.claude.widget.json` is **machine‑specific** (it embeds an
  absolute path) and is regenerated by `setup.bat`; it is intentionally left
  out of the distributed zip.
- If live limits stop updating, confirm the Chrome extension is loaded and a
  `claude.ai` tab is open — the service worker only fetches when at least one
  is present.
