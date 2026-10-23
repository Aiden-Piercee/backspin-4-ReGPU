@echo off
rem FM3 1080p: no anisotropic filtering
rem Build build-face (E + edge fix + feet-box fix + REGPU_TEX_SIGN switch), 1920x1080, profile shadowtest\FM3, screen 2.
rem First launch compiles shaders for a few minutes (fresh cache).
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_ANISO=1"
set "TS4_USER_DIR=%CD%\shadowtest\FM3"
set "REGPU_LOG=shadowtest\FM3_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [FM3 1080p: no anisotropic filtering]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo FM3 1080p: no anisotropic filtering
start "" "build-face\topspin4.exe" game
