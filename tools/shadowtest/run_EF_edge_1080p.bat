@echo off
rem Starts E edge and F edge at 1080p (shadow edge-fix build), screen 2.
start "" /b cmd /c "%~dp0E_edge_1080p.bat"
timeout /t 2 /nobreak >nul
start "" /b cmd /c "%~dp0F_edge_1080p.bat"
