@echo off
rem Every shadow-map lookup reads the near plane: shows the whole area the shadow map covers.
set "REGPU_DIAG_SHADOW_DARK=1"
call "%~dp0_launch.bat" B_DIAG_dark 1 "B DIAG: shadow lookups forced dark"
