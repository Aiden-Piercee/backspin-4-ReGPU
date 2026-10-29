@echo off
rem FM5 1080p: DXT and DXN decoded on the CPU, not native BC
rem Build build-face (E + edge fix + feet-box fix + REGPU_TEX_SIGN switch), 1920x1080, profile shadowtest\FM5, screen 2.
rem First launch compiles shaders for a few minutes (fresh cache).
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_NATIVE_BC=1"
set "TS4_USER_DIR=%CD%\shadowtest\FM5"
set "REGPU_LOG=shadowtest\FM5_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [FM5 1080p: DXT and DXN decoded on the CPU, not native BC]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo FM5 1080p: DXT and DXN decoded on the CPU, not native BC
start "" "build-face\topspin4.exe" game
