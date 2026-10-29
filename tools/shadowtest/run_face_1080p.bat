@echo off
rem Face grid artifact suspects FM1-FM5 at 1080p on screen 2 (build-face).
start "" /b cmd /c "%~dp0FM1_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0FM2_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0FM3_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0FM4_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0FM5_1080p.bat"
timeout /t 2 /nobreak >nul
