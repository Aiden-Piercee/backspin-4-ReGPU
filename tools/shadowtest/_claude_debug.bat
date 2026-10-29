@echo off
rem Claude's own debug instance: profile shadowtest\claude, port 7351, main screen, 1080p, build-test. Safe to close.
cd /d "%~dp0..\.."
set "TS4_USER_DIR=%CD%\shadowtest\claude"
set "REGPU_LOG=shadowtest\claude.log"
set "REGPU_WINDOW_TITLE=Claude test - safe to close"
set "REGPU_MONITOR=1"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG=1"
set "REGPU_DEBUG_PORT=7351"
start "" "build-test\topspin4.exe" game
