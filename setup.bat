@echo off
setlocal

set "PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "REG=%SystemRoot%\System32\reg.exe"

:: Remove trailing backslash from script directory
set "BASE=%~dp0"
if "%BASE:~-1%"=="\" set "BASE=%BASE:~0,-1%"

echo ============================================
echo  Claude Usage Widget - Setup
echo ============================================
echo.

:: 1. Copy get_limits.py next to the .exe so the C++ widget can find it
echo [1/4] Copying get_limits.py next to the widget executable...
copy /Y "%BASE%\get_limits.py" "%BASE%\cmake-build-debug\get_limits.py" >nul 2>&1
copy /Y "%BASE%\get_daily.py"  "%BASE%\cmake-build-debug\get_daily.py"  >nul 2>&1
if errorlevel 1 (
    echo   WARNING: could not copy get_limits.py / get_daily.py - is cmake-build-debug\Claude_Usage.exe built?
) else (
    echo   OK
)

:: 2. Generate com.claude.widget.json with the absolute path to host.bat
echo [2/4] Generating native host manifest...
"%PS%" -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command ^
  "$b = '%BASE%'; $p = $b + '\native_host\host.bat';" ^
  "$j = [PSCustomObject]@{" ^
  "  name = 'com.claude.widget';" ^
  "  description = 'Claude Usage Widget Bridge';" ^
  "  path = $p;" ^
  "  type = 'stdio';" ^
  "  allowed_origins = @('chrome-extension://dfjhnnbfnnfhajbhcgikibbfhbdcampf/')" ^
  "} | ConvertTo-Json;" ^
  "[System.IO.File]::WriteAllText($b + '\native_host\com.claude.widget.json', $j, [System.Text.UTF8Encoding]::new($false))"
if errorlevel 1 (
    echo   WARNING: manifest generation failed
) else (
    echo   OK
)

:: 3. Register the native messaging host in Chrome's registry key
echo [3/4] Registering Chrome native messaging host...
"%REG%" add "HKCU\Software\Google\Chrome\NativeMessagingHosts\com.claude.widget" ^
  /ve /t REG_SZ /d "%BASE%\native_host\com.claude.widget.json" /f >nul
if errorlevel 1 (
    echo   WARNING: registry write failed
) else (
    echo   OK
)

:: 4. Install Claude Code alert hooks (flashing/solid widget border)
echo [4/4] Installing Claude Code alert hooks...
"%PS%" -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "%BASE%\hooks\install_hooks.ps1"
if errorlevel 1 (
    echo   WARNING: hook installation failed
) else (
    echo   OK
)

echo.
echo ============================================
echo  Setup complete!
echo ============================================
echo.
echo  NEXT STEP: Load the Chrome extension manually
echo    1. Open Chrome  -^>  chrome://extensions
echo    2. Enable "Developer mode"  (top-right toggle)
echo    3. Click "Load unpacked"
echo    4. Select folder:  %BASE%\extension
echo.
echo  To start the widget: run cmake-build-debug\Claude_Usage.exe
echo.
pause
