@echo off
rem FB3 1080p: FB1 + no alpha test in shadow passes (F)
rem Build build-feet: E + edge fix + clamp of shader output on 8-bit targets (feet-box fix), 1920x1080, profile shadowtest\FB3, screen 2.
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_DEPTH_ALPHATEST=1"
set "TS4_USER_DIR=%CD%\shadowtest\FB3"
set "REGPU_LOG=shadowtest\FB3_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [FB3 1080p: FB1 + no alpha test in shadow passes (F)]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo FB3 1080p: FB1 + no alpha test in shadow passes (F)
start "" "build-feet\topspin4.exe" game
