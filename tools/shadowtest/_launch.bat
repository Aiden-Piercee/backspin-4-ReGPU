@echo off
rem Starts one shadow-test instance of build\shadowtest\topspin4.exe (a copy of the build, so a
rem rebuild never touches a running instance):
rem   its own user folder  shadowtest\<name>      (saves, compiled shaders; copied from shadowtest\base once)
rem   its own log          shadowtest\<name>.log
rem   its window           "Top Spin 4 [<label>]", 560x315, tiled on screen 2 at <slot>
rem   no debug server      (several can run at once)
rem The calling .bat sets the variant's REGPU_* switch first.
rem Usage: call _launch.bat <name> <slot> "<label>"
setlocal
set "NAME=%~1"
set "SLOT=%~2"
set "LABEL=%~3"
cd /d "%~dp0..\.."
if not exist "shadowtest\%NAME%\shadercache" xcopy /e /i /q /y "shadowtest\base" "shadowtest\%NAME%" >nul
set "TS4_USER_DIR=%CD%\shadowtest\%NAME%"
set "REGPU_LOG=shadowtest\%NAME%.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [%LABEL%]"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=%SLOT%"
set "REGPU_OUTPUT_SIZE=560x315"
set "REGPU_DEBUG="
echo %LABEL%
start "" "build\shadowtest\topspin4.exe" game
