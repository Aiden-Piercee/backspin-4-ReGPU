@echo off
rem Builds build-audio: the host code only, linked with the recompiled-module libraries in prebuilt and
rem the recompiled sources in out (see Build.bat). One build serves every
rem variant: they differ only in TS4_XMA_* switches.
setlocal
cd /d "%~dp0..\.."
set "MAIN=%CD%"
set "MAINF=%MAIN:\=/%"
rem ninja from PATH, else where winget puts it
for %%n in (ninja.exe) do set "NINJA=%%~$PATH:n"
if not defined NINJA set "NINJA=%LOCALAPPDATA%\Microsoft\WinGet\Links\ninja.exe"
set "LLVM=C:/Program Files/LLVM/bin"
if not exist build-audio\build.ninja (
    cmake -S . -B build-audio -G Ninja -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_C_COMPILER="%LLVM%/clang-cl.exe" -DCMAKE_CXX_COMPILER="%LLVM%/clang-cl.exe" ^
        -DCMAKE_LINKER="%LLVM%/lld-link.exe" -DCMAKE_AR="%LLVM%/llvm-lib.exe" -DCMAKE_MT="%LLVM%/llvm-mt.exe" ^
        -DCMAKE_RC_COMPILER="%LLVM%/llvm-rc.exe" -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
        -DTS4_OUT_DIR="%MAINF%/out" -DTS4_PREBUILT_PPC="%MAINF%/prebuilt" || exit /b 1
)
cmake --build build-audio --target topspin4 || exit /b 1
for %%f in (d3dcompiler_47.dll avcodec-62.dll avutil-60.dll) do if not exist "build-audio\%%f" copy /y "%MAIN%\prebuilt\dlls\%%f" build-audio\ >nul
echo build-audio\topspin4.exe is ready.
