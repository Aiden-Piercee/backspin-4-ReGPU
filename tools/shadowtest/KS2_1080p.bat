@echo off
rem KS2 1080p: control, player-shadow fix off
rem Build build-kill: E + edge fix + feet-box fix + player-shadow fix, 1920x1080, profile shadowtest\KS2, screen 2.
rem First launch compiles shaders for a few minutes (fresh cache).
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_KILL_SKIP=1"
set "TS4_USER_DIR=%CD%\shadowtest\KS2"
set "REGPU_LOG=shadowtest\KS2_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [KS2 1080p: control, player-shadow fix off]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=1"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo KS2 1080p: control, player-shadow fix off
start "" "build-kill\topspin4.exe" game
