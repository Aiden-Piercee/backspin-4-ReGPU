@echo off
rem A 1080p: shadow lookups forced lit on the shadow fix build (build-fix), 1920x1080, profile shadowtest\A_DIAG_lit, screen 2 (centred).
rem Both shadow fixes are built in. Every shadow-map lookup reads the far plane.
setlocal
cd /d "%~dp0..\.."
set "REGPU_DIAG_SHADOW_LIT=1"
set "TS4_USER_DIR=%CD%\shadowtest\A_DIAG_lit"
set "REGPU_LOG=shadowtest\A_DIAG_lit_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [A 1080p: shadow lookups forced lit]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo A 1080p: shadow lookups forced lit
start "" "build-fix\topspin4.exe" game
