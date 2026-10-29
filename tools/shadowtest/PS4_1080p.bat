@echo off
rem PS4 1080p: culling winding flipped
rem Build build-feet (E + edge fix + feet-box fix), 1920x1080, profile shadowtest\PS4, screen 2. Half-drawn player shadow suspect.
setlocal
cd /d "%~dp0..\.."
set "REGPU_CULL_FLIP=1"
set "TS4_USER_DIR=%CD%\shadowtest\PS4"
set "REGPU_LOG=shadowtest\PS4_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [PS4 1080p: culling winding flipped]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo PS4 1080p: culling winding flipped
start "" "build-feet\topspin4.exe" game
