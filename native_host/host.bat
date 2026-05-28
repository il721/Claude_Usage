@echo off
setlocal

:: Detect Python — skip Microsoft Store shims (WindowsApps)
set "PYTHON="
for /f "tokens=* usebackq" %%p in (`where python 2^>nul`) do (
    echo %%p | findstr /i "WindowsApps" >nul
    if errorlevel 1 (
        if not defined PYTHON set "PYTHON=%%p"
    )
)

:: Fall back to common install paths
if not defined PYTHON (
    for %%p in (
        "C:\Python314\python.exe"
        "C:\Python313\python.exe"
        "C:\Python312\python.exe"
        "C:\Python311\python.exe"
        "C:\Python310\python.exe"
        "%LOCALAPPDATA%\Programs\Python\Python314\python.exe"
        "%LOCALAPPDATA%\Programs\Python\Python313\python.exe"
        "%LOCALAPPDATA%\Programs\Python\Python312\python.exe"
        "%LOCALAPPDATA%\Programs\Python\Python311\python.exe"
    ) do (
        if not defined PYTHON (
            if exist %%p set "PYTHON=%%p"
        )
    )
)
if not defined PYTHON set "PYTHON=python.exe"

"%PYTHON%" "%~dp0host.py" %*
