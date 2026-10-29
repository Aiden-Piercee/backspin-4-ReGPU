@echo off
rem Draws that read the shadow map are painted red: which draws receive shadows.
set "REGPU_DIAG_SHADOW_TINT=1"
call "%~dp0_launch.bat" C_DIAG_tint 2 "C DIAG: shadow receivers red"
