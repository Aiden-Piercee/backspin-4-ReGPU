@echo off
rem Audio B: current-context register rotates
rem Suspect: the busy-context handshake (0x606). The register only shows a rotating index, never busy, like ReXGlue.
setlocal
set "TS4_XMA_606=rotate"
call "%~dp0_run.bat" B "current-context register rotates" 7452
