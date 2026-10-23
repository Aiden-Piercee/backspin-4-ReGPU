@echo off
rem B 1080p: shadow lookups forced dark on the shadow fix build (build-fix), 1920x1080, profile shadowtest\B_DIAG_dark, screen 2 (centred).
rem Both shadow fixes are built in. Every shadow-map lookup reads the near plane.
setlocal
cd /d "%~dp0..\.."
set "REGPU_DIAG_SHADOW_DARK=1"
set "TS4_USER_DIR=%CD%\shadowtest\B_DIAG_dark"
set "REGPU_LOG=shadowtest\B_DIAG_dark_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [B 1080p: shadow lookups forced dark]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo B 1080p: shadow lookups forced dark
start "" "build-fix\topspin4.exe" game
