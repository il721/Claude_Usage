@echo off
rem Shim so settings.json can call the hook by a stable path regardless of the
rem repo location. The first arg (ask|done|clear) is forwarded; the hook JSON
rem arrives on stdin from Claude Code. Always exit 0 so it can't block the CLI.
python "%~dp0widget_signal.py" %* 2>nul
exit /b 0
