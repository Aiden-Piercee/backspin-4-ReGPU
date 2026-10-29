@echo off
rem Depth-only alpha test only for textured draws, so the title's texture-less clear quad (alpha 0) writes the map's depth. Main candidate.
set "REGPU_FIX_SHADOW_CLEAR=1"
call "%~dp0_launch.bat" E_FIX_clear 4 "E FIX: shadow-map clear quad kept"
