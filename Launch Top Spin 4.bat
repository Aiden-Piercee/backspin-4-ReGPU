@echo off
rem Starts Top Spin 4 with the profile/shader cache in user\ and the debug server on port 7341
rem (tools\regpuctl.py). Log: user\topspin4.log
set "TS4_USER_DIR=%~dp0user"
set REGPU_DEBUG=1
set "REGPU_LOG=%~dp0user\topspin4.log"
start "" "%~dp0build\topspin4.exe" "%~dp0game"
