@echo off
rem Half-drawn player shadow suspects PS5-PS8 at 1080p on screen 2 (build-feet).
start "" /b cmd /c "%~dp0PS5_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0PS6_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0PS7_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0PS8_1080p.bat"
timeout /t 2 /nobreak >nul
