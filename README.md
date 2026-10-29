# Top Spin 4-ReGPU

A 'native' Windows PC port of Top Spin 4 (Xbox 360), statically recompiled using [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and rendered on Direct3D 12 via [ReGPU](include/regpu/regpu.h).

Top Spin 4 runs as a 'native' x64 Windows application without full system emulation. Guest PowerPC instructions are translated ahead of time into C++ functions (compiled to host machine code), while the Xbox 360 GPU (Xenos) command streams and shaders are processed on Direct3D 12 at 60 FPS.

## Current Status

- Boot and Menus: Boots through legal screens, title screen, DLC enumeration, and the full main menu (Game Modes, Player Zone, Options).
- Gameplay: Enters player select, Academy court, exhibition matches, and tournament modes.
- Graphics: Direct3D 12 rendering pipeline at 60 FPS with EDRAM emulation, MSAA resolve handling, and FSR 1 upscaling support.
- Audio: Multichannel 5.1 XAudio2 audio driver with FFmpeg XMA2 packet decoding and working background music playback.
- Storage: Dedicated user folder next to the executable for saves, player profiles, and precompiled shader pipeline caches.

## Requirements

### System Requirements
- Windows 10 or 11 (64-bit).
- Direct3D 12 capable GPU (Feature Level 12_0 or higher).
- A legal copy of Top Spin 4 for Xbox 360 with extracted game files.

### Build Toolchain
- LLVM Clang toolchain (`clang-cl`, `lld-link`, `llvm-lib`, `llvm-rc`).
- CMake 3.25 or newer.
- Ninja build system.
- Windows 10 or 11 SDK (for `d3dcompiler_47.dll` and Windows headers).
- Python 3.x (used for shader conversion and debug tooling).

## Quick Start

### 1. Link Game Assets
Extract your Top Spin 4 game disc files to a folder on your system, then create a directory junction named `game` in the repository root:

```cmd
cmd /c mklink /J game "C:\Path\To\Extracted\TopSpin4"
```

Verify that `game\default.xex`, `game\Loader_DLL.xex`, and `game\Swing_DLL.xex` exist inside the junction.

### 2. Build the Port
Run the automated build script:

```cmd
Build.bat
```

This compiles the host runtime, links the 65,904 precompiled PowerPC functions from `prebuilt\`, copies required runtime DLLs, and outputs `build\topspin4.exe` (approximately 56 MB).

### 3. Launch the Game
Run the launch script:

```cmd
"Launch Top Spin 4.bat"
```

The game creates and uses `build\user` for:
- Save games and career progression (`build\user\profiles\`)
- Shader pipeline caches (`build\user\pipelines.bin`)
- Runtime log output (`build\user\topspin4.log`)

## Architecture

Top Spin 4 is partitioned into three distinct executable modules:

| Module | Base Address | Function Count | Role |
|---|---|---|---|
| `default.xex` | `0x82000000` | 554 | Bootstrap loader, shared memory setup, and core handoff coordinator |
| `Loader_DLL.xex` | `0x83000000` | 17,653 | Legal screens, intro sequence, XUI title screen, and background loading thread |
| `Swing_DLL.xex` | `0x88000000` | 47,697 | Main engine, physics, AI, courts, player models, animations, and gameplay |

All three modules are recompiled to C++ using XenonRecomp and reside in `out/` with precompiled static libraries in `prebuilt/`.

### Graphics: ReGPU

The graphics subsystem implements the Xbox 360 Xenos GPU command processor on Direct3D 12:
- Translates Xenos microcode directly to HLSL compute and graphics shaders.
- Manages Xbox 360 EDRAM tiling, aliased color and depth clear rectangles, and MSAA resolves.
- Paces presentation locked to the console 60 Hz vblank signal.

### Audio: XAudio2 and XMA

Audio is handled through a multichannel 5.1 48 kHz XAudio2 stream. Game music and sound effects are fed through an asynchronous XMA2 decoder backed by FFmpeg, with MMIO register synchronization to handle dynamic buffer kicks and locks without loss.

## Live Debugging and External Control

When launched with `REGPU_DEBUG=1` (default in `Launch Top Spin 4.bat`), the port exposes a local control server on port 7341. You can inspect and control the game using `tools/regpuctl.py`:

```cmd
py -3 tools\regpuctl.py status             :: View current FPS, draws, and GPU state
py -3 tools\regpuctl.py shot out\shot.png  :: Capture an uncompressed framebuffer screenshot
py -3 tools\regpuctl.py capture            :: Trace draw calls and textures for the next frame
py -3 tools\regpuctl.py threads            :: Inspect registers and call chains of active guest threads
```

## Known Areas of Work

- Predicated tiling: Some in-match camera angles render in two vertical EDRAM tiles; the lower tile occasionally displays stale resolve pixels.
- Audio queueing: Continued tuning of XMA packet streaming latency and frame pacing.
- Rare shader opcodes: A small number of rarely hit microcode instructions log fallback warnings.

## Credits and Licenses

- XenonRecomp: Static PowerPC recompiler developed by hedge-dev.
- ReGPU: Direct3D 12 Xenos GPU implementation developed by Scott (Welshgamer).
- [topspin4-decomp](https://github.com/g-guthrie/topspin4-decomp): Research and decompilation analysis for Top Spin 4.
- Xenia Project: Groundbreaking reverse engineering and research on Xbox 360 architecture and the Xenos GPU.
- FFmpeg: Audio decoding components under LGPL/GPL.
- AMD FidelityFX SDK: FSR upscaling integration under the MIT license.
- Top Spin 4 is the property of 2K Sports and Take-Two Interactive. This project is for preservation and research purposes and requires original game assets.
