@echo off
rem Starts the feet-box variants FB1-FB4 at 1080p on screen 2 (build-feet).
start "" /b cmd /c "%~dp0FB1_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0FB2_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0FB3_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0FB4_1080p.bat"
timeout /t 2 /nobreak >nul
