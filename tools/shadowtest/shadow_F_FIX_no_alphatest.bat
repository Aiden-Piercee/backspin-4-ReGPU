@echo off
rem No alpha test at all in depth-only passes (what Xenia does). Watch for blocky shadows of cut-out objects.
set "REGPU_NO_DEPTH_ALPHATEST=1"
call "%~dp0_launch.bat" F_FIX_no_alphatest 5 "F FIX: no alpha test in depth-only passes"
