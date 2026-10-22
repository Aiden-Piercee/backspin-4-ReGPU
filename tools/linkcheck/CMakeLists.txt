# Link check for milestone 1: compiles the three recompiled Top Spin 4 images
# (out/ppc, out/ppc_loader, out/ppc_swing) and links them with generated import stubs
# and the image table, without the runtime.
cmake_minimum_required(VERSION 3.25)
project(ts4_linkcheck C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "the recompiled code needs clang-cl (it uses clang builtins)")
endif()
get_filename_component(TS4_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

if(MSVC)
    set(TS4_FORCE_INCLUDE "/FI${TS4_ROOT}/src/ppc_hooks.h")
else()
    set(TS4_FORCE_INCLUDE -include "${TS4_ROOT}/src/ppc_hooks.h")
endif()
find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)

# One static library per image: each keeps its own ppc_config.h.
set(TS4_IMAGES "ts4:ppc" "ts4_loader:ppc_loader" "ts4_swing:ppc_swing")
set(TS4_CONFIGS "")
set(TS4_SHARED_HEADERS "")
foreach(image ${TS4_IMAGES})
    string(REPLACE ":" ";" parts "${image}")
    list(GET parts 0 name)
    list(GET parts 1 dir)
    set(ppc_dir "${TS4_ROOT}/out/${dir}")
    file(GLOB sources CONFIGURE_DEPENDS "${ppc_dir}/*.cpp")
    if(NOT sources)
        message(FATAL_ERROR "out/${dir} is empty: run tools/recomp-ts4.ps1 first")
    endif()
    add_library(${name}_linkcheck_ppc STATIC ${sources})
    target_include_directories(${name}_linkcheck_ppc PRIVATE "${ppc_dir}" "${TS4_ROOT}/thirdparty/XenonRecomp/thirdparty/simde")
    target_compile_options(${name}_linkcheck_ppc PRIVATE -Wno-everything -msse4.1 -fno-strict-aliasing ${TS4_FORCE_INCLUDE})
    list(APPEND TS4_CONFIGS "${TS4_ROOT}/config/${name}.toml")
    list(APPEND TS4_SHARED_HEADERS "${ppc_dir}/ppc_recomp_shared.h")
endforeach()

set(generated "${CMAKE_CURRENT_BINARY_DIR}/generated")
add_custom_command(OUTPUT "${generated}/stubs.cpp"
    COMMAND Python3::Interpreter -I "${TS4_ROOT}/tools/genstubs.py" ${TS4_SHARED_HEADERS} "${generated}/stubs.cpp"
    DEPENDS ${TS4_SHARED_HEADERS} "${TS4_ROOT}/tools/genstubs.py"
    COMMENT "Generating import stubs")
add_custom_command(OUTPUT "${generated}/recomp_modules.cpp"
    COMMAND Python3::Interpreter -I "${TS4_ROOT}/tools/genmodules.py" "${generated}/recomp_modules.cpp" ${TS4_CONFIGS}
    DEPENDS ${TS4_CONFIGS} ${TS4_SHARED_HEADERS} "${TS4_ROOT}/tools/genmodules.py"
    COMMENT "Generating the recompiled image table")

add_executable(ts4_linkcheck main.cpp "${generated}/stubs.cpp" "${generated}/recomp_modules.cpp")
target_include_directories(ts4_linkcheck PRIVATE "${CMAKE_CURRENT_LIST_DIR}/shim" "${TS4_ROOT}/src" "${TS4_ROOT}/out/ppc"
    "${TS4_ROOT}/thirdparty/XenonRecomp/thirdparty/simde")
target_compile_options(ts4_linkcheck PRIVATE -msse4.1 ${TS4_FORCE_INCLUDE})
target_link_libraries(ts4_linkcheck PRIVATE ts4_linkcheck_ppc ts4_loader_linkcheck_ppc ts4_swing_linkcheck_ppc)
if(MSVC)
    target_link_options(ts4_linkcheck PRIVATE /STACK:0x1000000)
endif()
