@echo off
rem Starts Top Spin 4 with the debug server on port 7341 (tools\regpuctl.py). Saves, settings and
rem shader caches live in build\user; the log is build\user\topspin4.log.
if not exist "%~dp0build\user" mkdir "%~dp0build\user"
set REGPU_DEBUG=1
set REGPU_NO_DIRECT_FETCH=1
set REGPU_NO_DIRECT_INDEX=1
set "REGPU_LOG=%~dp0build\user\topspin4.log"
start "" "%~dp0build\topspin4.exe" "%~dp0game"
