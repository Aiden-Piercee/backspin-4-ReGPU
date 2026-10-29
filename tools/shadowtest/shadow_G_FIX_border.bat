@echo off
rem Outside the shadow map: white (lit) instead of the edge texels.
set "REGPU_FIX_SHADOW_BORDER=1"
call "%~dp0_launch.bat" G_FIX_border 6 "G FIX: white border on shadow maps"
