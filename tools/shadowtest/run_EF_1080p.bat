@echo off
rem Starts E and F at 1080p on the shadow fix build, screen 2.
start "" /b cmd /c "%~dp0E_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0F_1080p.bat"
