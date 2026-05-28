@echo off
"C:\Program Files\JetBrains\CLion\bin\mingw\bin\g++.exe" -std=gnu++20 -o cmake-build-debug\Claude_Usage.exe main.cpp -luser32 -lgdi32 -mwindows 2>&1
echo Exit: %ERRORLEVEL%
