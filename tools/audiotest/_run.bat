@echo off
rem Starts one audio variant: %1 letter, %2 what it changes, %3 debug port. The caller sets its
rem switches. Every variant traces the XMA decoder (TS4_TRACE_XMA=1, a few lines every 3 s) and
rem the music file reads into tools\audiotest\logs\<letter>.log. Run one at a time: they share
rem one profile (tools\audiotest\profile, its shader cache copied once from build\user).
cd /d "%~dp0..\.."
set "MAIN=%CD%"
if not exist build-audio\topspin4.exe (
    echo Build first: tools\audiotest\build.bat
    pause
    exit /b 1
)
set "TS4_USER_DIR=%~dp0profile"
if not exist "%TS4_USER_DIR%\shadercache" if exist "%MAIN%\build\user\shadercache" (
    echo Copying a shader cache into the test profile, once...
    robocopy "%MAIN%\build\user" "%TS4_USER_DIR%" /E /NFL /NDL /NJH /NJS /NP >nul
)
if not exist "%~dp0logs" mkdir "%~dp0logs"
set "REGPU_LOG=%~dp0logs\%~1.log"
set "REGPU_WINDOW_TITLE=Top Spin 4 [audio %~1: %~2] test - safe to close"
set "REGPU_MONITOR=2"
set "REGPU_WINDOW_SLOT=0"
set "REGPU_OUTPUT_SIZE=1920x1080"
set "REGPU_DEBUG="
set "REGPU_DEBUG_PORT=%~3"
set "TS4_TRACE_XMA=1"
echo Audio %~1: %~2
start "" "build-audio\topspin4.exe" "%MAIN%\game"
