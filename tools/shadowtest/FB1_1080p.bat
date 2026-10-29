@echo off
rem FB1 1080p: feet-box fix (8-bit output clamped)
rem Build build-feet: E + edge fix + clamp of shader output on 8-bit targets (feet-box fix), 1920x1080, profile shadowtest\FB1, screen 2.
setlocal
cd /d "%~dp0..\.."

set "TS4_USER_DIR=%CD%\shadowtest\FB1"
set "REGPU_LOG=shadowtest\FB1_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [FB1 1080p: feet-box fix (8-bit output clamped)]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=0"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo FB1 1080p: feet-box fix (8-bit output clamped)
start "" "build-feet\topspin4.exe" game
