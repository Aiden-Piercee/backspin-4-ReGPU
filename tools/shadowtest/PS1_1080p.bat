@echo off
rem PS1 1080p: all 256 shader constants uploaded
rem Build build-feet (E + edge fix + feet-box fix), 1920x1080, profile shadowtest\PS1, screen 2. Half-drawn player shadow suspect.
setlocal
cd /d "%~dp0..\.."
set "REGPU_ALL_CONSTANTS=1"
set "TS4_USER_DIR=%CD%\shadowtest\PS1"
set "REGPU_LOG=shadowtest\PS1_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [PS1 1080p: all 256 shader constants uploaded]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=0"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo PS1 1080p: all 256 shader constants uploaded
start "" "build-feet\topspin4.exe" game
