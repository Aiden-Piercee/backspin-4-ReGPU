@echo off
rem The shadow fix build (build-fix): your own profile, debug server on (port 7341), screen 2.
cd /d "%~dp0..\.."
set "REGPU_DEBUG=1"
set "REGPU_MONITOR=2"
start "" "build-fix\topspin4.exe" game
