# Top Spin 4 Static Recompilation Worklog & Joblog

This document serves as an active, comprehensive context handoff log for developers and external AI agents (Claude Code, Cursor, Aider, Codex, parallel Antigravity sessions).

## Earlier history

Notes from the first bring-up attempt (before the XenonRecomp + ReGPU port) were removed from this log.

---

## XenonRecomp + ReGPU Migration & Runtime Progress (2026-10-22 – 2026-10-23)

### Milestone 1 & 2: Full Multi-Module Recompilation (`default.xex`, `Loader_DLL.xex`, `Swing_DLL.xex`)
- **Static Recompilation (`56ade77`)**:
  - Dumped PE sections for `default.xex` (`0x82000000`), `Loader_DLL.xex` (`0x82000000` -> rebased `0x83000000`), and `Swing_DLL.xex` (`0x82000000` -> rebased `0x88000000`).
  - Configured switch tables and `setjmp`/`longjmp` addresses across all three modules and recompiled all **65,904 PPC functions** into `out/ppc`, `out/ppc_loader`, and `out/ppc_swing`.
  - Patched PowerPC timebase reads (`mftb` -> `Fm2Timebase()`) across all generated sources and verified 100% symbol linkage with `ts4_linkcheck` and native `topspin4.exe` (54.6 MB) linked against the ReGPU D3D12 backend.
- **Multi-Module Loader & Kernel Exports**:
  - Added `\Device\Image` VFS mapping to `GameDir()` in `src/kernel/imports_io.cpp`.
  - Implemented `LoadedModule` tracking, XEX export table decoding (`hibase | offset`), and `InitModule` (`DllMain` attach lifecycle) in `src/loader.cpp`.
  - Implemented `XexLoadImage`, `XexUnloadImage`, `XexGetProcedureAddress`, and `XexGetModuleHandle` in `src/kernel/imports_mm.cpp`.
  - Fixed host thread dispatch for GPU interrupt and command processor loops via `CreateHostGuestThread` (`src/gpu/gpu.cpp`).

### Milestone 3: Visible Window, Shared Kernel Variables & XAudio2 5.1 Driver
- **Window Visibility (`472590e`)**:
  - Attached the process and guest threads to `WinSta0\Default` desktop (`AttachInteractiveDesktop()` in `src/main.cpp` and `src/kernel/imports_ke.cpp`) and centered the window (`SetWindowPos(..., HWND_TOP, ..., SWP_SHOWWINDOW)`) so the D3D12 window opens visibly when launched from background shells.
- **Shared Kernel Variables Across XEX Modules (`src/loader.cpp`)**:
  - Memoized `KernelVariable(uint32_t ordinal)` in `src/loader.cpp` (`static std::unordered_map<uint32_t, uint32_t> s_vars`) so `default.xex`, `Loader_DLL.xex`, and `Swing_DLL.xex` resolve to the **same** guest addresses for `KeTimeStampBundle` (`0x0AD`) and `XboxHardwareInfo` (`0x156`). Previously, each dynamically loaded XEX allocated its own zeroed `KeTimeStampBundle`, freezing `Loader_DLL`'s internal timers on the legal screen.
- **XAudio2 Callback Argument Indirect Struct (`src/audio/audio.cpp`)**:
  - Traced `Loader_DLL`'s audio callback `ldr_sub_8350DE10` (`0x8350DE10`), which loads `lwz r31, 0(r3)` expecting `r3` to point to a guest struct whose `+0` field holds the `callbackArg` passed to `XAudioRegisterRenderDriverClient`.
  - Updated `src/audio/audio.cpp` to allocate a 16-byte guest callback struct (`g_callbackStruct`) with `StoreBE32(g_callbackStruct + 0, g_callbackArg)` and pass `g_callbackStruct` in `ctx.r3`. Audio now runs at 188 callbacks/s (48 kHz 5.1 float) with 0 dropped frames, immediately advancing past the intro/legal screens.

### Milestone 4: Attract Mode, Title Screen ("Press START button") & Transition into `Swing_DLL.xex`
- **SIMD Instructions (`vcmpbfp128` & `vpkd3d128`)**:
  - Implemented all 25 occurrences of `vcmpbfp128` / `vcmpbfp128.` (XUI 2D/3D bounds clipping with `CR6` all-in-bounds check) and 1 occurrence of `vpkd3d128` (type 2, `DEC3N` 10:10:10:2 signed normalized packing) in `out/ppc_loader/ppc_recomp.24.cpp`, `out/ppc_loader/ppc_recomp.9.cpp`, `out/ppc_swing/ppc_recomp.99.cpp`, and `thirdparty/XenonRecomp/XenonRecomp/recompiler.cpp`.
- **Wide/Narrow Format Exports (`src/kernel/imports_rtl.cpp`)**:
  - Implemented `_vscwprintf`, `_snprintf`, `_snwprintf`, and `swprintf` in `src/kernel/imports_rtl.cpp` with guest PPC varargs reading (`GuestVaList` starting at `ctx.r1.u32 + 0x38` after `r3..r10`), enabling XUI string formatting (`Press START button`, `Loading...`).
  - Reached the **2K Sports Top Spin 4 — Press START button** Title Screen at 60 FPS (~192 draws/frame, 16.6 ms/frame).
- **Unblocking the Title Screen `Loading...` Spinner (`src/kernel/runtime.cpp`, `src/kernel/imports_xam.cpp`)**:
  - Traced `Loader_DLL`'s async task poller (`ldr_sub_8347B460` -> `ldr_sub_833FBB80` -> `XamGetOverlappedExtendedError`): when `XamContentCreateEnumerator` / `XEnumerate` completes with `ERROR_NO_MORE_FILES` (`0x12`), the guest checks `InternalLow == 0` (`ERROR_SUCCESS`). When `InternalLow != 0`, it calls `XamGetOverlappedExtendedError(overlapped)` and expects `HRESULT_FROM_WIN32(ERROR_NO_MORE_FILES)` (`0x80070012`) rather than raw `0x00000012` (which has bit 31 clear, `SUCCEEDED(hr) == true`, causing an infinite loop).
  - Furthermore, `Loader_DLL` registers a guest completion callback `0x833FBAE0` (`ldr_sub_833FBAE0`) at `overlapped + 0x10` (`pfnCompletionRoutine`).
  - Updated `CompleteOverlapped` in `src/kernel/runtime.cpp` to set `InternalLow = 1627` (`X_ERROR_FUNCTION_FAILED`) and `dwExtendedError = 0x80070000 | result` when `result != 0`, and invoke the guest completion routine at `overlapped + 0x10` (`r3 = result, r4 = length, r5 = overlapped`).
  - Implemented `XamUserGetSigninInfo` in `src/kernel/imports_xam.cpp` so only user 0 is signed in (`0x80070525` `X_E_NO_SUCH_USER` for users 1–3).
  - **Result**: Pressing `START` completes DLC enumeration in 0.2s, exits `Loader_DLL.xex`'s main loop, loads `game:\Swing_DLL.xex`, runs `Swing_DLL` `DllMain` (`0x88562DB0`), resolves ordinals 1 (`0x885F5218`), 2 (`0x885F5228`), and 3 (`0x885F5210`), and enters `Swing_DLL`'s main game loop (`swg_sub_889BCB10`).

### Current Investigation: `default.xex` -> `Loader_DLL.xex` -> `Swing_DLL.xex` Handoff & Renderer Init
- **Handoff Architecture**:
  - `default.xex` (`sub_82029418`) allocates a 237 MB heap block (`0x0E2E0000` bytes) and stores it in a shared handoff struct at `0x82031248 + 11048`.
  - When `Loader_DLL.xex` finishes the Title Screen, it spawns a background loading-screen thread (`0x83488F88`), loads `game:\Swing_DLL.xex`, stores `Swing_DLL`'s module handle at `*(0x82031248 + 24)` and its `D3DDevice*` at `*(0x82031248 + 10968)`, sets `*(uint8_t*)(0x82031248 + 0) = 1`, and returns to `default.xex` without unloading `Loader_DLL.xex` yet.
  - `default.xex` (`sub_82029358`) stores cleanup callback `0x82029038` at `*(0x82031248 + 10976)`, sets `*(uint8_t*)(0x82031248 + 9) = 1`, and invokes `Swing_DLL` ordinal 1 (`0x885F5218`) and ordinal 2 (`0x885F5228`, `swg_sub_885F5228`) with `r3 = 0x82031248`.
  - `Swing_DLL`'s renderer init (`swg_sub_887CAE50` in `out/ppc_swing/ppc_recomp.105.cpp:68477`) reads `r30 = 0x82031248`, calls `*(r30 + 10976)` (`sub_82029038`, which calls `Loader_DLL` ordinal 3 and `XexUnloadImage` to stop the loading thread), takes ownership of `D3DDevice*` from `*(r30 + 10968)`, and calls `VdInitializeRingBuffer` (`swg_sub_88292B88`).
- **Next Step**: Trace why `swg_sub_887CAE50` was not called during `swg_sub_889BD370` (`CORETECH` / `LAYOUT` XEX section parsing and service initialization).


### Partner Agent Root-Cause Finding: Why `swg_sub_887CAE50` Was Not Called (`CORETECH` / `LAYOUT` Zeroed in Guest Memory) (2026-10-23 06:22)

- **Root Cause 1 — XEX Resources (`CORETECH`, `LAYOUT`, `54540859`, `BOOT`) live outside PE sections in `image.data`**:
  - In `game/default.xex`, XEX header `0x000002FF` defines three resources inside the `0x180000`-byte decrypted image (`out/ts4.bin`):
    - `54540859` (XDBF): `addr = 0x82033E00`, `size = 0x7B336`
    - `LAYOUT`: `addr = 0x820B0000`, `size = 0xB9BF0`
    - `CORETECH`: `addr = 0x82169C00`, `size = 0x10148`
  - However, `default.xex`'s PE section headers (`out/ts4.sections`) end at `.reloc` (`0x82050200..0x82051370`), and `.data` ends at `0x82033DC8`.
  - In `src/loader.cpp`, both `LoadTitle` (lines 284–297) and `LoadModuleImage` (lines 448–460) only copy `0x600` bytes of the PE header and iterate over `image.sections` (skipping `.reloc`). They **never copy the non-PE pages of `image.data`** into guest memory!
  - Consequently, when `swg_sub_889BD370` calls `XexGetModuleSection(hDefaultXex, "CORETECH", &data, &size)` and `XexGetModuleSection(hDefaultXex, "LAYOUT", &data, &size)`, `XexGetModuleSection` returns `X_STATUS_SUCCESS` with `0x82169C00` and `0x820B0000` (bypassing the disk fallback `CoreTech_Xenon.bin` / `CoreTech_Xenon.xml`), and `Swing_DLL` parses **all-zero memory** at `0x82169C00` and `0x820B0000`.
  - Because `0x82169C00` (`CORETECH`) is all zeros, `swg_sub_8862D660` (`out/ppc_swing/ppc_recomp.83.cpp:11061`) creates the root service container via `swg_sub_882812E0()`, but its `vtable[4]` (`+16`) registers **zero** child services (`28(r3) == 32(r3) == 0`), so `swg_sub_887CAE50` (Renderer service init + `Loader_DLL` unload callback `0x82029038`) is never instantiated or called, and `swg_sub_889BCB10` -> `swg_sub_88611D08` spins forever over an empty service list while `Loader_DLL`'s loading thread keeps rendering `0 draws`.
- **Root Cause 2 — `XexGetModuleSection` ignores `ctx.r3.u32` (`hModule`)**:
  - In `src/kernel/imports_mm.cpp:155`, `XexGetModuleSection` always queries `g_title.xexHeader` (`default.xex`) instead of looking up `LoadedModule* mod = FindLoadedModule(ctx.r3.u32)`.
  - `game/Loader_DLL.xex` has XEX resource `BOOT` (`addr = 0x835D0000`, `size = 0x40`) and `54540859` (`addr = 0x8354BC80`, `size = 0x7B336`) outside its PE sections, and `game/Swing_DLL.xex` has `54540859` (`addr = 0x88D51080`, `size = 0x7B336`) outside its PE sections.
- **Exact Fix for Active Agent**:
  1. In `src/loader.cpp` (`LoadTitle` around line 284 and `LoadModuleImage` around line 448), copy the full decrypted XEX image buffer into guest memory before the section loop:
     ```cpp
     memcpy(GuestPtr(uint32_t(image.base)), image.data.get(), image.size);
     ```
  2. In `src/kernel/imports_mm.cpp` (`XexGetModuleSection` around line 160), resolve the module header from `ctx.r3.u32`:
     ```cpp
     LoadedModule* mod = FindLoadedModule(ctx.r3.u32);
     const uint32_t header = mod ? mod->xexHeader : g_title.xexHeader;
     const uint32_t resources = XexHeaderField(header, 0x000002FF);
     ```


### Partner Agent Tracing Update: `swg_sub_889BD370` Branch Analysis & CoreTech Descriptors [2026-10-23 06:31:00 +11:00]

- **Branch at `0x889BD45C` (`ppc_recomp.138.cpp:29318`)**:
  - `lbz r10, 630(r11)` checks the global CoreTech environment configuration (`m_bUseBinaryResources` at `*(0x88B45B1C) + 630`).
  - If `r10 != 0`: Execution branches to `loc_889BD548` (`ppc_recomp.138.cpp:29443`), which queries `XexGetModuleSection(hDefaultXex, "CORETECH")` (addr `0x82169C00`, size `0x10148`) and `XexGetModuleSection(hDefaultXex, "LAYOUT")` (addr `0x820B0000`, size `0xB9BF0`).
  - If `r10 == 0`: Execution executes lines 29321–29442, which attempts to read `CoreTech_Xenon.xml` (path string at `0x880C1384`, `r11+4996`).
- **Why Both Paths Fail Currently**:
  - For the binary path (`0x889BD548`): `XexGetModuleSection` returns `X_STATUS_SUCCESS` with pointer `0x82169C00`, but guest memory at `0x82169C00` is all zeros because `LoadTitle` in `src/loader.cpp` only copied PE sections up to `.reloc` (`0x82051370`) and skipped the non-PE XEX payload pages (`0x820B0000`–`0x82179D48`) where `LAYOUT` and `CORETECH` reside in `out/ts4.bin`.
  - The XML path (`CoreTech_Xenon.xml`) does not exist on the retail disc image (disc only contains compiled binaries and XUI packages).
- **The Solution Remains**:
  - In `src/loader.cpp` (`LoadTitle` line 284 and `LoadModuleImage` line 448):
    ```cpp
    memcpy(GuestPtr(uint32_t(image.base)), image.data.get(), image.size);
    ```
    This copies all `0x180000` bytes of `default.xex`, all `0x5E0000` bytes of `Loader_DLL.xex`, and all `0xDD0000` bytes of `Swing_DLL.xex`, ensuring all embedded XEX resources (`LAYOUT`, `CORETECH`, `BOOT`, `54540859`) are present at their expected guest virtual addresses.


### Progress Update: Successful `Swing_DLL` Renderer Takeover & Loading Screen APC Support [2026-10-23 12:00:00 +11:00]

- **Implementation of Embedded Resource Payload Copying (`src/loader.cpp`, `src/kernel/imports_mm.cpp`)**:
  - Implemented the full decrypted image copy in `LoadTitle` (line 284) and `LoadModuleImage` (line 448):
    ```cpp
    memcpy(GuestPtr(uint32_t(image.base)), image.data.get(), image.size);
    ```
  - Updated `FindLoadedModule` to recognize module lookups by either `ldrEntry` or `mod.base`.
  - Implemented module-aware resolution in `XexGetModuleSection` (`src/kernel/imports_mm.cpp`), querying `ctx.r3.u32` first and falling back to `g_title.xexHeader`.
- **Runtime Verification**:
  - Verified from runtime execution log:
    - `XexGetModuleSection(mod=40002380, "BOOT")` resolved at `0x835D0000`, size `0x00000040`.
    - `XexGetModuleSection(mod=40002000, "CORETECH")` resolved at `0x82169C00`, size `0x00010148`.
    - `XexGetModuleSection(mod=40002000, "LAYOUT")` resolved at `0x820B0000`, size `0x000B9BF0`.
  - **`Swing_DLL` Renderer Takeover Confirmed**:
    - `Loader_DLL` ordinal 3 cleanly called at `0x83492920` to shut down intro renderer.
    - `Loader_DLL.xex` unloaded (`XexUnloadImage(40002380)`).
    - `Swing_DLL` GPU takeover initiated: interrupt callback set to `0x882929E0` with context `0xA7054180`.
    - GPU command ring buffer expanded and reallocated at `0xAE2F0000` (65,536 dwords).
    - EDRAM render target reset to base `0x2D0`.
    - New `Swing_DLL` vertex shader (`vs 42447dde124e8d71`) dynamically compiled and submitted in Direct3D 12.
    - Verified active 60 FPS rendering of 4 draws per frame in `Swing_DLL`'s loading sequence.
- **Root Cause & Fix for Loading Screen Progress (`src/kernel/imports_io.cpp`)**:
  - Identified `[ 111.0857] NtReadFile with an APC routine is not supported`:
    `Swing_DLL`'s `LoadingScreenThreadProc` submits asynchronous `NtReadFile` requests with a guest completion APC routine (`ctx.r5.u32`).
  - Because `NtReadFile` previously ignored `ctx.r5.u32`, the caller never received completion notification and remained waiting in the loading spinner loop.
  - Implemented APC completion dispatch in `src/kernel/imports_io.cpp`:
    ```cpp
    if (apcRoutine)
    {
        PPCContext apcCtx = ctx;
        apcCtx.r1.u32 -= 0x100;
        StoreBE32(apcCtx.r1.u32, ctx.r1.u32);
        apcCtx.r3.u64 = apcContext;
        apcCtx.r4.u64 = iosb;
        apcCtx.r5.u64 = 0;
        GuestCall(apcCtx, apcRoutine);
    }
    ```
  - Built cleanly; verification run in progress (`task-2686`).
