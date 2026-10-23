@echo off
rem Half-drawn player shadow suspects PS1-PS4 at 1080p on screen 2 (build-feet).
start "" /b cmd /c "%~dp0PS3_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0PS4_1080p.bat"
timeout /t 2 /nobreak >nul
