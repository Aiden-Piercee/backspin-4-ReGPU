@echo off
rem PS3 1080p: no back-face culling
rem Build build-feet (E + edge fix + feet-box fix), 1920x1080, profile shadowtest\PS3, screen 2. Half-drawn player shadow suspect.
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_CULL=1"
set "TS4_USER_DIR=%CD%\shadowtest\PS3"
set "REGPU_LOG=shadowtest\PS3_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [PS3 1080p: no back-face culling]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo PS3 1080p: no back-face culling
start "" "build-feet\topspin4.exe" game
