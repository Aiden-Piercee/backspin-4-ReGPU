@echo off
rem FB2 1080p: control, clamp off (= E edge)
rem Build build-feet: E + edge fix + clamp of shader output on 8-bit targets (feet-box fix), 1920x1080, profile shadowtest\FB2, screen 2.
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_UNORM_CLAMP=1"
set "TS4_USER_DIR=%CD%\shadowtest\FB2"
set "REGPU_LOG=shadowtest\FB2_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [FB2 1080p: control, clamp off (= E edge)]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=1"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo FB2 1080p: control, clamp off (= E edge)
start "" "build-feet\topspin4.exe" game
