@echo off
rem Receivers show the map values they read: red last, green least, blue most. F12 also writes the map (contrast-stretched) to out\res.
set "REGPU_DIAG_SHADOW_SHOW=1"
call "%~dp0_launch.bat" D_DIAG_show 3 "D DIAG: shadow map values shown"
