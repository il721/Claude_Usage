# Claude Usage Widget

Win32 desktop widget (C++/WinAPI) that shows Claude API usage from claude.ai.

## Data sources

The widget reads one file — `%USERPROFILE%\.claude\widget_limits.json` — which
has two producers: **Claude Code writes it natively (~60s) while a session runs**,
and the **Chrome extension** writes it otherwise. `get_limits.py` detects a live
session (recent `~/.claude/projects/**/*.jsonl` write) and tags its output
`SESSION` / `EXTENSION`; on a missing/stale file it emits `FALLBACK` and the widget
uses a `ccusage` estimate. A corner dot shows the active source (blue = session,
grey = extension, amber = ccusage). The extension is optional while a session runs.

## Build & run

Use `/clion-cpp` — it handles the MinGW PATH fix, compiler flags, and detached launch.

## Distribution

Use `/pack` to rebuild `Claude_Usage_Widget.zip`.

## Standing rule — keep the archive in sync

Whenever you successfully edit any of these files:
- `main.cpp`
- `get_limits.py`
- `get_daily.py`
- `extension/background.js`
- `extension/content.js`
- `extension/manifest.json`
- `native_host/host.py`
- `native_host/host.bat`
- `setup.bat`

…ask the user at the end of your response:

> "Want me to update `Claude_Usage_Widget.zip` with the latest changes? (`/pack`)"

Do not ask if the file was only read (not modified), and do not ask more than once per conversation turn.
