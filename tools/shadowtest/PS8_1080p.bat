@echo off
rem PS8 1080p: vertex fetch layout baked into each shader
rem Build build-feet (E + edge fix + feet-box fix), 1920x1080, profile shadowtest\PS8, screen 2. Half-drawn player shadow suspect.
setlocal
cd /d "%~dp0..\.."
set "REGPU_BAKED_VFETCH=1"
set "TS4_USER_DIR=%CD%\shadowtest\PS8"
set "REGPU_LOG=shadowtest\PS8_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [PS8 1080p: vertex fetch layout baked into each shader]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT="
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo PS8 1080p: vertex fetch layout baked into each shader
start "" "build-feet\topspin4.exe" game
