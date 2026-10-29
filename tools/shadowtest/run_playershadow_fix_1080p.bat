@echo off
rem Player-shadow fix (KS1) and its control (KS2) at 1080p on screen 2 (build-kill).
start "" /b cmd /c "%~dp0KS1_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0KS2_1080p.bat"
timeout /t 2 /nobreak >nul
