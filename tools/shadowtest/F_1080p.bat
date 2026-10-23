@echo off
rem F 1080p on the shadow fix build (build-fix), 1920x1080, profile shadowtest\F_FIX_no_alphatest, screen 2 slot 1.
rem Both shadow fixes are built in. Plus: no alpha test at all in depth-only passes.
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_DEPTH_ALPHATEST=1"
set "TS4_USER_DIR=%CD%\shadowtest\F_FIX_no_alphatest"
set "REGPU_LOG=shadowtest\F_FIX_no_alphatest_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [F 1080p]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=1"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo F 1080p
start "" "build-fix\topspin4.exe" game
