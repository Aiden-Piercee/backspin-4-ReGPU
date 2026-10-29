@echo off
rem E edge 1080p on the shadow edge-fix build (build-edge), 1920x1080, profile shadowtest\E_edge, screen 2 slot 0.
rem Both shadow fixes plus the edge fix (no thin outline at the shadow map's border). 
rem REGPU_NO_RECT_SNAP=1 would turn the edge fix off.
setlocal
cd /d "%~dp0..\.."

set "TS4_USER_DIR=%CD%\shadowtest\E_edge"
set "REGPU_LOG=shadowtest\E_edge_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [E edge 1080p]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=0"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo E edge 1080p
start "" "build-edge\topspin4.exe" game
