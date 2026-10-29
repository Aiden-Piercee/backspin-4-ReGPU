@echo off
rem FM4 1080p: no mipmaps
rem Build build-face (E + edge fix + feet-box fix + REGPU_TEX_SIGN switch), 1920x1080, profile shadowtest\FM4, screen 2.
rem First launch compiles shaders for a few minutes (fresh cache).
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_MIPS=1"
set "TS4_USER_DIR=%CD%\shadowtest\FM4"
set "REGPU_LOG=shadowtest\FM4_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [FM4 1080p: no mipmaps]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo FM4 1080p: no mipmaps
start "" "build-face\topspin4.exe" game
