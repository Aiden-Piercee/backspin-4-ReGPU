@echo off
rem E 1080p on the shadow fix build (build-fix), 1920x1080, profile shadowtest\E_FIX_clear, screen 2 slot 0.
rem Both shadow fixes are built in.
setlocal
cd /d "%~dp0..\.."

set "TS4_USER_DIR=%CD%\shadowtest\E_FIX_clear"
set "REGPU_LOG=shadowtest\E_FIX_clear_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [E 1080p]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=0"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo E 1080p
start "" "build-fix\topspin4.exe" game
