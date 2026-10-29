@echo off
rem PS6 1080p: vertex buffers always copied, never read in place
rem Build build-feet (E + edge fix + feet-box fix), 1920x1080, profile shadowtest\PS6, screen 2. Half-drawn player shadow suspect.
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_DIRECT_FETCH=1"
set "TS4_USER_DIR=%CD%\shadowtest\PS6"
set "REGPU_LOG=shadowtest\PS6_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [PS6 1080p: vertex buffers always copied, never read in place]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=1"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo PS6 1080p: vertex buffers always copied, never read in place
start "" "build-feet\topspin4.exe" game
