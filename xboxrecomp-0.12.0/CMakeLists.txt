cmake_minimum_required(VERSION 3.20)
project(xboxrecomp VERSION 0.9.0 LANGUAGES C)

# Xbox Static Recompilation Runtime Library
# Provides link-time libraries for statically recompiled Xbox games.
# Pattern: extract emulator subsystems (xemu) into reusable static libraries.
#
# Libraries:
#   xbox_kernel    - Xbox kernel replacement layer (147 imports → Win32)
#   xbox_d3d8      - D3D8 → D3D11 graphics compatibility
#   xbox_dsound    - DirectSound → software mixer compatibility
#   xbox_input     - Xbox gamepad → XInput compatibility
#   xbox_apu       - MCPX APU audio emulation (extracted from xemu)
#   xbox_nv2a      - NV2A GPU emulation (extracted from xemu)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Common definitions for all targets
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)

# PS Vita (vitasdk): newlib gaps filled by a force-included header, and a
# dlfcn.h stub (no host dynamic loader).
if(VITA)
    set(XBOXRECOMP_VITA_INCLUDE "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/vita_include")
    include_directories(BEFORE SYSTEM "${XBOXRECOMP_VITA_INCLUDE}")
    add_compile_options("SHELL:-include ${XBOXRECOMP_VITA_INCLUDE}/vita_compat.h")
endif()
if(VITA)
    # (v74) scheduled for the Vita's own processor, not a generic ARMv7
    add_compile_options(-mtune=cortex-a9)
endif()

# Compact guest address space: no 4 GB reservation, no fixed host addresses,
# a 256-entry region table instead (recomp_types.h, xbox_memory_layout.c).
# Required on 32-bit hosts (PS Vita, armhf Linux); defaults on for any
# non-x86 target. Propagated to the game through the umbrella target below,
# because the generated code's MEM macros must agree with the runtime.
if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64|i[3-6]86|x86)$")
    set(_xr_compact_default OFF)
else()
    set(_xr_compact_default ON)
endif()
option(XBOXRECOMP_COMPACT_GUEST "Table-mapped guest memory (32-bit / non-x86 hosts)"
       ${_xr_compact_default})
if(XBOXRECOMP_COMPACT_GUEST)
    add_compile_definitions(RECOMP_COMPACT_GUEST=1)
    message(STATUS "xboxrecomp: compact guest address space ON")
endif()

# Option to build examples

# ── Runtime Libraries ─────────────────────────────────────────

# Platform: Win32/NT type vocabulary + POSIX implementation of generic
# Win32 host primitives. Carries no Xbox semantics; provides the foundation
# the kernel HLE and the graphics/audio/input layers build on.
add_subdirectory(src/platform)

# Core: Xbox kernel replacement (memory layout, file I/O, threading, sync)
add_subdirectory(src/kernel)

# Graphics: D3D8 → D3D11 compatibility layer
add_subdirectory(src/d3d)

# Audio: DirectSound → software mixer compatibility
add_subdirectory(src/audio)

# Audio: MCPX APU hardware emulation (voice processor, DSP)
add_subdirectory(src/apu)

# GPU: NV2A hardware emulation (register handlers, MMIO, push buffer)
add_subdirectory(src/nv2a)

# USB: MCPX OHCI host controllers. A title's XAPI is statically linked and
# drives these registers directly, so a pad only exists if they do.
add_subdirectory(src/usb)

# Input: Xbox gamepad → XInput
add_subdirectory(src/input)

# Shared FMV player (Media Foundation -> D3D8 texture)
add_subdirectory(src/video)

# ── Umbrella target ───────────────────────────────────────────

# Convenience target that links all runtime libraries
add_library(xboxrecomp INTERFACE)
if(VITA)
    target_include_directories(xboxrecomp SYSTEM BEFORE INTERFACE "${XBOXRECOMP_VITA_INCLUDE}")
    target_compile_options(xboxrecomp INTERFACE "SHELL:-include ${XBOXRECOMP_VITA_INCLUDE}/vita_compat.h")
endif()
if(XBOXRECOMP_COMPACT_GUEST)
    target_compile_definitions(xboxrecomp INTERFACE RECOMP_COMPACT_GUEST=1)
endif()
target_link_libraries(xboxrecomp INTERFACE
    xbox_kernel
    xbox_d3d8
    xbox_dsound
    xbox_input
    xbox_usb
    xbox_apu
    xbox_nv2a
    xbox_video
)

