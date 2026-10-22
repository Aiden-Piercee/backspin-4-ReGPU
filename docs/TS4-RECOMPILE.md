# Recompiling Top Spin 4 with XenonRecomp & ReGPU

Milestone 1 of the Top Spin 4 port: recompile Top Spin 4's three XEX images with upstream [XenonRecomp](https://github.com/hedge-dev/XenonRecomp), integrate the Direct3D 12 Xenos renderer [ReGPU](include/regpu/regpu.h), and link everything into a single native Windows executable (`topspin4.exe`).

Function boundaries and jump tables are decoded and translated using XenonRecomp and XenonAnalyse.

## The Three Images

| Image | Base | Config | Output | Symbol Prefix |
|---|---|---|---|---|
| `default.xex` | `0x82000000` | `config/ts4.toml` | `out/ppc` | None |
| `Loader_DLL.xex` | `0x83000000` | `config/ts4_loader.toml` | `out/ppc_loader` | `ldr_` |
| `Swing_DLL.xex` | `0x88000000` | `config/ts4_swing.toml` | `out/ppc_swing` | `swg_` |

## Toolchain & Recompilation Procedure

1. **Point `game` junction to the extracted game assets:**
   ```powershell
   cmd /c mklink /J game <path-to-extracted-disc-files>
   ```

2. **Recompile the three XEX images:**
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\recomp-ts4.ps1
   ```
   This generates:
   - `out/ppc/` (554 functions)
   - `out/ppc_loader/` (17,653 functions)
   - `out/ppc_swing/` (47,697 functions)
   - `src/kernel/stubs.cpp` (weak kernel import fallbacks)
   - `src/recomp_modules.cpp` (multi-module guest address dispatch table)
   - Applies the 49.875 MHz timebase fix via `tools/patch_mftb.py`.

3. **Verify with Link Check:**
   ```powershell
   cmd /c "tools\vcenv.cmd ninja -C build ts4_linkcheck"
   build\linkcheck\ts4_linkcheck.exe
   ```
   Verifies that all 65,904 recompiled function mappings fall within valid code ranges without duplicate guest addresses.

4. **Build the Native Executable:**
   ```powershell
   cmd /c "tools\vcenv.cmd ninja -C build topspin4"
   ```
   Produces `build/topspin4.exe` (54.6 MB native x64 binary).

## Launching & Live Debugging

Run the game with:
```powershell
cmd /c "build\topspin4.exe <path-to-extracted-game-dir>"
```
Or start with `REGPU_DEBUG=1` and drive it using `tools/regpuctl.py`:
```powershell
py -3 tools/regpuctl.py status
py -3 tools/regpuctl.py shot out/shot.png
py -3 tools/regpuctl.py capture
```
