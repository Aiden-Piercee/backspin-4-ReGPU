@echo off
rem PS2 1080p: no real indexed draws (indices expanded)
rem Build build-feet (E + edge fix + feet-box fix), 1920x1080, profile shadowtest\PS2, screen 2. Half-drawn player shadow suspect.
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_INDEXED=1"
set "TS4_USER_DIR=%CD%\shadowtest\PS2"
set "REGPU_LOG=shadowtest\PS2_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [PS2 1080p: no real indexed draws (indices expanded)]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=1"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo PS2 1080p: no real indexed draws (indices expanded)
start "" "build-feet\topspin4.exe" game
