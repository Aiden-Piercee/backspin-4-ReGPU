@echo off
rem Builds build\topspin4.exe: the host code plus the recompiled-module libraries in prebuilt\
rem (recompiled sources in out\). Copies the runtime DLLs from prebuilt\dlls next to the exe.
setlocal
cd /d "%~dp0"
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"
set "ROOTF=%ROOT:\=/%"
set "LLVM=C:/Program Files/LLVM/bin"
set "PATH=C:\Program Files (x86)\Windows Kits\10\bin\10.0.22621.0\x64;%PATH%"
rem clang runs out of memory with more parallel jobs
set CMAKE_BUILD_PARALLEL_LEVEL=3
rem ninja from PATH, else where winget puts it
for %%n in (ninja.exe) do set "NINJA=%%~$PATH:n"
if not defined NINJA set "NINJA=%LOCALAPPDATA%\Microsoft\WinGet\Links\ninja.exe"
if not exist build\build.ninja (
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_C_COMPILER="%LLVM%/clang-cl.exe" -DCMAKE_CXX_COMPILER="%LLVM%/clang-cl.exe" ^
        -DCMAKE_LINKER="%LLVM%/lld-link.exe" -DCMAKE_AR="%LLVM%/llvm-lib.exe" -DCMAKE_MT="%LLVM%/llvm-mt.exe" ^
        -DCMAKE_RC_COMPILER="%LLVM%/llvm-rc.exe" -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
        -DTS4_OUT_DIR="%ROOTF%/out" -DTS4_PREBUILT_PPC="%ROOTF%/prebuilt" || exit /b 1
)
cmake --build build --target topspin4 || exit /b 1
copy /y prebuilt\dlls\*.dll build\ >nul
echo build\topspin4.exe is ready.
