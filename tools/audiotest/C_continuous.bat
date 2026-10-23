@echo off
rem Audio C: kicked contexts stay enabled
rem Suspect: lost or coalesced kicks. A kicked context keeps decoding whenever it has room, until locked or cleared.
setlocal
set "TS4_XMA_CONTINUOUS=1"
call "%~dp0_run.bat" C "kicked contexts stay enabled" 7453
