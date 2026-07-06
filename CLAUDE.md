# Claude Usage Widget

Win32 desktop widget (C++/WinAPI) that shows Claude API usage from claude.ai.

## Build & run

Use `/clion-cpp` — it handles the MinGW PATH fix, compiler flags, and detached launch.

## Distribution

Use `/pack` to rebuild `Claude_Usage_Widget.zip`.

## Standing rule — keep the archive in sync

Whenever you successfully edit any of these files:
- `main.cpp`
- `get_limits.py`
- `get_daily.py`
- `get_zai.py`
- `extension/background.js`
- `extension/content.js`
- `extension/manifest.json`
- `native_host/host.py`
- `native_host/host.bat`
- `setup.bat`

…ask the user at the end of your response:

> "Want me to update `Claude_Usage_Widget.zip` with the latest changes? (`/pack`)"

Do not ask if the file was only read (not modified), and do not ask more than once per conversation turn.

## Deferred work

- **Fully-opaque alert border (DEFERRED 2026-06-18).** The yellow alert border is currently dimmed along with the body by the widget's opacity setting (`SetLayeredWindowAttributes` + `LWA_ALPHA` applies one alpha to the whole window). Making only the border stay 100% opaque needs either a separate click-through overlay window or a per-pixel-alpha (`UpdateLayeredWindow`) rewrite of the paint path — a non-trivial change the user chose not to pursue for now. Do not start it unless asked.

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

Rules:
- For codebase questions, first run `graphify query "<question>"` when graphify-out/graph.json exists. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
