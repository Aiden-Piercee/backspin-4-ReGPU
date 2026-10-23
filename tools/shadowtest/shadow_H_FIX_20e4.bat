@echo off
rem The depth copy handed to shaders as D24FS8 (20e4 float) bits instead of a plain value.
set "REGPU_FIX_SHADOW_AS_20E4=1"
call "%~dp0_launch.bat" H_FIX_20e4 7 "H FIX: depth copy read as 20e4 float bits"
