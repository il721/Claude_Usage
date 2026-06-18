@echo off
rem Shim so settings.json can call the hook by a stable path regardless of the
rem repo location. The first arg (ask|done|clear) is forwarded; the hook JSON
rem arrives on stdin from Claude Code. Always exit 0 so it can't block the CLI.
rem
rem `clear` is wired to PostToolUse (matches every tool), so it fires very
rem frequently -- handle it inline here (pure file delete, same "remove ALL
rem flags" semantics as the .py) to avoid a ~200ms Python cold start per tool.
if /i "%~1"=="clear" (
    del /q "%USERPROFILE%\.claude\widget-alerts\*.flag" >nul 2>nul
    exit /b 0
)
python "%~dp0widget_signal.py" %* 2>nul
exit /b 0
