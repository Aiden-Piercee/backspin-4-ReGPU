cmake_minimum_required(VERSION 3.25)
project(topspin4 C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "topspin4 needs clang-cl (the recompiled code uses clang builtins)")
endif()

# Release is optimised even when the cache's CMAKE_*_FLAGS_RELEASE are empty (this build's were:
# everything, the recompiled code included, was compiled at /Od, and loading the XEX modules
# alone took 15 s).
add_compile_options("$<$<CONFIG:Release,RelWithDebInfo>:/O2>")

add_subdirectory(thirdparty/XenonRecomp EXCLUDE_FROM_ALL)

# Recompiled PowerPC modules. TS4_OUT_DIR: another checkout's out folder (its recompiled
# sources); TS4_PREBUILT_PPC: another build folder whose ts4_*ppc.lib are linked as they are, so
# a side build (a test variant of the host code) compiles only the host code.
set(TS4_OUT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/out" CACHE PATH "Folder holding ppc, ppc_loader and ppc_swing")
set(TS4_PREBUILT_PPC "" CACHE PATH "Build folder whose recompiled-module libraries are linked instead of compiled")
set(TS4_PPC_DIR "${TS4_OUT_DIR}/ppc")
set(TS4_LOADER_DIR "${TS4_OUT_DIR}/ppc_loader")
set(TS4_SWING_DIR "${TS4_OUT_DIR}/ppc_swing")

file(GLOB TS4_PPC_SOURCES CONFIGURE_DEPENDS "${TS4_PPC_DIR}/*.cpp")
file(GLOB TS4_LOADER_SOURCES CONFIGURE_DEPENDS "${TS4_LOADER_DIR}/*.cpp")
file(GLOB TS4_SWING_SOURCES CONFIGURE_DEPENDS "${TS4_SWING_DIR}/*.cpp")

if(NOT TS4_PPC_SOURCES OR NOT TS4_LOADER_SOURCES OR NOT TS4_SWING_SOURCES)
    message(FATAL_ERROR "Recompiled sources missing: run tools/recomp-ts4.ps1 first")
endif()

set(PPC_FORCE_INCLUDE "/FI${CMAKE_CURRENT_SOURCE_DIR}/src/ppc_hooks.h")
set(PPC_COMPILE_FLAGS -Wno-everything -msse4.1 -fno-strict-aliasing ${PPC_FORCE_INCLUDE})

if(TS4_PREBUILT_PPC)
    foreach(lib ts4_ppc ts4_loader_ppc ts4_swing_ppc)
        add_library(${lib} STATIC IMPORTED)
        set_target_properties(${lib} PROPERTIES IMPORTED_LOCATION "${TS4_PREBUILT_PPC}/${lib}.lib")
    endforeach()
else()
    add_library(ts4_ppc STATIC ${TS4_PPC_SOURCES})
    target_include_directories(ts4_ppc PRIVATE "${TS4_PPC_DIR}" thirdparty/XenonRecomp/thirdparty/simde)
    target_compile_options(ts4_ppc PRIVATE ${PPC_COMPILE_FLAGS})

    add_library(ts4_loader_ppc STATIC ${TS4_LOADER_SOURCES})
    target_include_directories(ts4_loader_ppc PRIVATE "${TS4_LOADER_DIR}" thirdparty/XenonRecomp/thirdparty/simde)
    target_compile_options(ts4_loader_ppc PRIVATE ${PPC_COMPILE_FLAGS})

    add_library(ts4_swing_ppc STATIC ${TS4_SWING_SOURCES})
    target_include_directories(ts4_swing_ppc PRIVATE "${TS4_SWING_DIR}" thirdparty/XenonRecomp/thirdparty/simde)
    target_compile_options(ts4_swing_ppc PRIVATE ${PPC_COMPILE_FLAGS})
endif()

# Built-in compute shaders (DepthCopy, Gamma, Enhance)
set(TS4_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${TS4_GENERATED}")
add_custom_command(OUTPUT "${TS4_GENERATED}/fsr1_cs.h"
    COMMAND py -3 -I "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_fsr1_cs.py" "${TS4_GENERATED}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/fsr1.hlsl" "${CMAKE_CURRENT_SOURCE_DIR}/tools/gen_fsr1_cs.py"
    COMMENT "Generating compute shaders")

file(GLOB_RECURSE TOPSPIN4_SOURCES CONFIGURE_DEPENDS src/*.cpp)
# Exclude optional vendor SDK implementations unless enabled
list(REMOVE_ITEM TOPSPIN4_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/fsr3.cpp")
list(REMOVE_ITEM TOPSPIN4_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/gpu/dlss.cpp")

add_executable(topspin4 ${TOPSPIN4_SOURCES} "${TS4_GENERATED}/fsr1_cs.h")
target_include_directories(topspin4 PRIVATE
    include
    src
    "${TS4_GENERATED}"
    "${TS4_PPC_DIR}"
    thirdparty/XenonRecomp/XenonUtils
    thirdparty/XenonRecomp/thirdparty/simde
)
target_compile_definitions(topspin4 PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
target_compile_options(topspin4 PRIVATE /EHsc -msse4.1 -fno-strict-aliasing -gline-tables-only ${PPC_FORCE_INCLUDE})
target_link_options(topspin4 PRIVATE /STACK:0x1000000 /DEBUG)

target_link_libraries(topspin4 PRIVATE
    ts4_ppc
    ts4_loader_ppc
    ts4_swing_ppc
    XenonUtils
    d3d12
    dxgi
    d3dcompiler
    ws2_32
    winmm
    synchronization
    onecore
    dbghelp
    bcrypt
)

# Optional diagnostic target: link check
add_subdirectory(tools/linkcheck linkcheck)
