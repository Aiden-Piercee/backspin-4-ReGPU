@echo off
rem Starts every shadow variant (A-I), tiled on screen 2.
for %%f in ("%~dp0shadow_*.bat") do (
  start "" /b cmd /c "%%~f"
  timeout /t 2 /nobreak >nul
)
