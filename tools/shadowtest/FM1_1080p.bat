@echo off
rem FM1 1080p: face control (no switch)
rem Build build-face (E + edge fix + feet-box fix + REGPU_TEX_SIGN switch), 1920x1080, profile shadowtest\FM1, screen 2.
rem First launch compiles shaders for a few minutes (fresh cache).
setlocal
cd /d "%~dp0..\.."

set "TS4_USER_DIR=%CD%\shadowtest\FM1"
set "REGPU_LOG=shadowtest\FM1_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [FM1 1080p: face control (no switch)]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=0"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo FM1 1080p: face control (no switch)
start "" "build-face\topspin4.exe" game
