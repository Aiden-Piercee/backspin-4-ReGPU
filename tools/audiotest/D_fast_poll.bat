@echo off
rem Audio D: decoder polls every 0.2 ms
rem Suspect: decode latency. ReXGlue decodes inside the kick; here kicks were picked up every 1 ms.
setlocal
set "TS4_XMA_POLL_US=200"
call "%~dp0_run.bat" D "decoder polls every 0.2 ms" 7454
