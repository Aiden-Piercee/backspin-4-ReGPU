@echo off
rem F edge 1080p on the shadow edge-fix build (build-edge), 1920x1080, profile shadowtest\F_edge, screen 2 slot 1.
rem Both shadow fixes plus the edge fix (no thin outline at the shadow map's border). Plus: no alpha test at all in depth-only passes.
rem REGPU_NO_RECT_SNAP=1 would turn the edge fix off.
setlocal
cd /d "%~dp0..\.."
set "REGPU_NO_DEPTH_ALPHATEST=1"
set "TS4_USER_DIR=%CD%\shadowtest\F_edge"
set "REGPU_LOG=shadowtest\F_edge_1080p.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [F edge 1080p]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=1"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
echo F edge 1080p
start "" "build-edge\topspin4.exe" game
