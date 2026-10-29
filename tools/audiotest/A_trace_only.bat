@echo off
rem Audio A: parked decoder as it is, traced
rem Baseline: expect the music to fade after a few seconds, as before.
setlocal
call "%~dp0_run.bat" A "parked decoder as it is, traced" 7451
