@echo off
rem Every shadow-map lookup reads the far plane. Square gone = the map read is at fault, not the geometry.
set "REGPU_DIAG_SHADOW_LIT=1"
call "%~dp0_launch.bat" A_DIAG_lit 0 "A DIAG: shadow lookups forced lit"
