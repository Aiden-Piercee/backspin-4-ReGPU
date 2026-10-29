@echo off
rem FM2 1080p: texture sign applied (signed, biased channels)
rem Build build-face (E + edge fix + feet-box fix + REGPU_TEX_SIGN switch), 1920x1080, profile shadowtest\FM2, screen 2.
rem First launch compiles shaders for a few minutes (fresh cache).
setlocal
cd /d "%~dp0..\.."
set "REGPU_TEX_SIGN=1"
set "TS4_USER_DIR=%CD%\shadowtest\FM2"
set "REGPU_LOG=shadowtest\FM2_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [FM2 1080p: texture sign applied (signed, biased channels)]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=1"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo FM2 1080p: texture sign applied (signed, biased channels)
start "" "build-face\topspin4.exe" game
