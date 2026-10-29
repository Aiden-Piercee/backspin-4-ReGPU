@echo off
rem PS5 1080p: no tall-pass handling (1024 shadow map)
rem Build build-feet (E + edge fix + feet-box fix), 1920x1080, profile shadowtest\PS5, screen 2. Half-drawn player shadow suspect.
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_TALL_PASS=1"
set "TS4_USER_DIR=%CD%\shadowtest\PS5"
set "REGPU_LOG=shadowtest\PS5_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [PS5 1080p: no tall-pass handling (1024 shadow map)]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=0"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo PS5 1080p: no tall-pass handling (1024 shadow map)
start "" "build-feet\topspin4.exe" game
