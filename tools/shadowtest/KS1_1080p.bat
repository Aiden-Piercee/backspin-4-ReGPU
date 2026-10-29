@echo off
rem KS1 1080p: player-shadow fix
rem Build build-kill: E + edge fix + feet-box fix + player-shadow fix, 1920x1080, profile shadowtest\KS1, screen 2.
rem First launch compiles shaders for a few minutes (fresh cache).
setlocal
cd /d "%~dp0..\.."

set "TS4_USER_DIR=%CD%\shadowtest\KS1"
set "REGPU_LOG=shadowtest\KS1_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [KS1 1080p: player-shadow fix]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=0"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo KS1 1080p: player-shadow fix
start "" "build-kill\topspin4.exe" game
