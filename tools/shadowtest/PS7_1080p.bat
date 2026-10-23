@echo off
rem PS7 1080p: vertex-fetch-patched shader copies translated on their own
rem Build build-feet (E + edge fix + feet-box fix), 1920x1080, profile shadowtest\PS7, screen 2. Half-drawn player shadow suspect.
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_CANONICAL=1"
set "TS4_USER_DIR=%CD%\shadowtest\PS7"
set "REGPU_LOG=shadowtest\PS7_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [PS7 1080p: vertex-fetch-patched shader copies translated on their own]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo PS7 1080p: vertex-fetch-patched shader copies translated on their own
start "" "build-feet\topspin4.exe" game
