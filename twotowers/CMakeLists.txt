cmake_minimum_required(VERSION 3.20)

# ============================================================================
# The Lord of the Rings: The Two Towers (Xbox, USA) -- static recompilation
#
# src/recomp/gen/ is generated from YOUR copy of default.xbe by
# ../generate.py before configuring; see ../README.md.
# ============================================================================

project(twotowers_recomp C CXX)
if(WIN32)
    enable_language(RC)
endif()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Reproducible, path-free binaries: no build-machine paths in __FILE__ /
# debug info, no link timestamp.
get_filename_component(TT_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
if(MINGW OR CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options("-ffile-prefix-map=${TT_REPO_ROOT}=." "-ffile-prefix-map=${CMAKE_BINARY_DIR}=build")
endif()
if(VITA)
    add_compile_options(-mtune=cortex-a9)   # (v74: the Vita's own processor)
endif()
if(MINGW)
    add_link_options(-Wl,--no-insert-timestamp)
elseif(MSVC)
    add_link_options(/Brepro)
    add_compile_options(/Brepro)
endif()

# Common definitions
add_compile_definitions(
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    ${TT_EXTRA_DEFS}
    # The Two Towers' image (with .data BSS) ends at 0x009194E0; move the
    # runtime's stack / TLS / kernel-data block above it (also applies to the
    # toolkit libraries built by add_subdirectory below).
    XBOX_LOW_AREA_BASE=0x00920000
)

# ── xboxrecomp toolkit ──────────────────────────────────────────
#
# Option A: xboxrecomp as a subdirectory (simplest)
#   Place xboxrecomp/ next to your game project and use a relative path.
#
# Option B: find_package (if you install xboxrecomp system-wide)
#   find_package(xboxrecomp REQUIRED)
#
# Adjust this path to point to your xboxrecomp clone:
set(XBOXRECOMP_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../xboxrecomp-0.12.0" CACHE PATH
    "Path to the xboxrecomp toolkit root directory")

# Add xboxrecomp as a subdirectory -- this builds all 6 runtime libraries
# and provides the 'xboxrecomp' umbrella target to link against.
add_subdirectory(${XBOXRECOMP_DIR} ${CMAKE_BINARY_DIR}/xboxrecomp)

# ── Icons and launcher art ───────────────────────────────────────
# The repository ships a neutral icon. Your own art (for example from your
# own disc's case) can go in resources/local/ -- twotowers.ico and
# cover.bmp -- which git ignores; the launcher also shows a cover.bmp placed
# beside it.
set(TT_RES "${CMAKE_CURRENT_SOURCE_DIR}/resources")
set(TT_ICON "${TT_RES}/app_icon.ico")
if(EXISTS "${TT_RES}/local/twotowers.ico")
    set(TT_ICON "${TT_RES}/local/twotowers.ico")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/app.rc"
    "#include <windows.h>\n1 ICON \"${TT_ICON}\"\n1 24 \"${TT_RES}/app.manifest\"\n")
set(TT_LAUNCHER_RC "#include <windows.h>\n1 ICON \"${TT_ICON}\"\n1 24 \"${TT_RES}/app.manifest\"\n")
if(EXISTS "${TT_RES}/local/cover.bmp")
    string(APPEND TT_LAUNCHER_RC "2 BITMAP \"${TT_RES}/local/cover.bmp\"\n")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/launcher.rc" "${TT_LAUNCHER_RC}")

# ── Recompiled game executable ──────────────────────────────────

# Collect generated recompiled code.
# The recomp pipeline outputs files into src/recomp/gen/.
# With --split 1000, you get gen_0000.c, gen_0001.c, etc.
# CONFIGURE_DEPENDS so re-running the recompiler picks up added/removed files
# without a manual re-configure. A plain GLOB is evaluated once at configure
# time, so a newly generated file (an extra chunk, recomp_stubs_unresolved.c)
# is silently left out of the build and shows up as unresolved externals.
file(GLOB RECOMP_GEN_SOURCES CONFIGURE_DEPENDS "src/recomp/gen/*.c")

# Core game sources
set(GAME_SOURCES
    src/main.c
    src/recomp_manual.c
    src/twotowers_manual.c
    src/e3hud.c
    src/heroes.c
    src/lang.c
    src/lang_ultra.cpp
    tools/ultra_textures/bc7enc.cpp
    tools/ultra_textures/bc7decomp.cpp
    tools/ultra_textures/lodepng.cpp
    src/menus.c
    src/options.c
    src/versus.c
    src/achieve.c
    # Add game-specific sources here, e.g.:
    # src/asset_loader.c
    # src/custom_renderer.c
)

# The recomp pipeline also generates a dispatch header and function declarations.
# These are included by the generated .c files:
#   src/recomp/gen/recomp_funcs.h    - extern declarations for all recompiled functions
#   src/recomp/recomp_dispatch.h     - binary search dispatch table
#   src/recomp/recomp_types.h        - register model, ICALL macros, MEM helpers

if(WIN32)
    list(APPEND GAME_SOURCES ${CMAKE_CURRENT_BINARY_DIR}/app.rc)
endif()
add_executable(${PROJECT_NAME} WIN32
    ${GAME_SOURCES}
    ${RECOMP_GEN_SOURCES}
)
# (1.9.5) the game code's sin/cos/tan: our own (src/rc_fastmath.h), four
# times as fast as the C library's x87 versions and as accurate
set_property(SOURCE ${RECOMP_GEN_SOURCES} src/heroes.c APPEND PROPERTY COMPILE_OPTIONS
    "-include" "${CMAKE_CURRENT_SOURCE_DIR}/src/rc_fastmath.h")

# Link against the xboxrecomp umbrella target (all 6 runtime libraries)
target_link_libraries(${PROJECT_NAME} PRIVATE xboxrecomp)

# Include paths for generated recomp headers
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recomp
    ${XBOXRECOMP_DIR}/include
)

if(WIN32)
    # Windows SDK libraries needed at link time
    # (d3d11.dll and dxgi.dll are loaded at run time: the system's, or
    # DXVK's for renderer=vulkan -- xboxrecomp src/platform/d3d11_dyn.h)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        dxguid
        xinput
        winmm
        # Lets the fault handler resolve the host RIP back to the generated
        # function's symbol, i.e. to the guest function that faulted.
        dbghelp
        bcrypt
    )
elseif(VITA)
    # PS Vita (vitasdk): SDL2 (window through GXM, sound, controls); the
    # picture comes from the software renderer for now. Same guest renames as
    # the Linux build (one ELF, the C library's names).
    include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        SDL2main SDL2
        vitaGL vitashark SceShaccCgExt mathneon taihen_stub
        SceShaccCg_stub SceKernelDmacMgr_stub
        SceDisplay_stub SceCtrl_stub SceAudio_stub SceAudioIn_stub SceAvPlayer_stub
        SceSysmodule_stub SceGxm_stub SceCommonDialog_stub SceAppMgr_stub
        SceAppUtil_stub SceTouch_stub SceHid_stub SceMotion_stub SceIme_stub
        ScePower_stub SceRtc_stub SceLibKernel_stub SceNet_stub SceNetCtl_stub
        pthread m)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-incompatible-pointer-types
        -Wno-int-conversion -Wno-format)
    option(TT_PROFILE "the game thread's frame chain for [PROF] (a cost on every call)" ON)
    if(TT_PROFILE)
        set(TT_PROF_DEF ";RECOMP_PROFILE")
    else()
        set(TT_PROF_DEF "")
        target_compile_definitions(${PROJECT_NAME} PRIVATE TT_NO_PROF)   # (no sampler, no hand-written frames)
    endif()
    set(TT_GUEST_RENAMES "close=tt_guest_close;write=tt_guest_write;lseek=tt_guest_lseek;isatty=tt_guest_isatty")
    set_source_files_properties(${RECOMP_GEN_SOURCES}
        PROPERTIES COMPILE_DEFINITIONS "${TT_GUEST_RENAMES};RECOMP_FAST_REGS${TT_PROF_DEF}")   # (v129: the game thread profiled; v177: -DTT_PROFILE=OFF builds it without)
    # (v188) the generated code's optimisation level: TT_GEN_OPT (e.g. -O2, -Os); empty keeps -O3. 13.7 MB of
    # code at -O3: the game thread's working set may miss the 32 KB instruction cache more than it gains.
    set(TT_GEN_OPT "-O2" CACHE STRING "optimisation flags for the generated code")
    # (v194) profile-guided optimisation of the generated code: TT_PGO=gen counts it while the game runs
    # (the .gcda files dumped every 3 minutes under ux0:data/twotowers/pgo), TT_PGO=use builds from them
    # (TT_PGO_DIR: where they were copied; the code never run keeps its usual optimisation).
    set(TT_PGO "" CACHE STRING "profile-guided optimisation: gen, use, or empty")
    set(TT_PGO_DIR "/home/claude/pgo" CACHE STRING "the .gcda files for TT_PGO=use")
    set(TT_GEN_FLAGS "${TT_GEN_OPT}")
    if(TT_PGO STREQUAL "gen")
        # (the Vita toolchain's libgcov is the freestanding one: no file writing, no value profiling, no
        #  constructors -- the counts' descriptions in a section, gcov_info, and twotowers_manual.c writes
        #  the files itself with __gcov_info_to_gcda)
        list(APPEND TT_GEN_FLAGS -fprofile-generate=/pgo -fprofile-update=single -fno-profile-values -fprofile-info-section=gcov_info)
        target_link_libraries(${PROJECT_NAME} PRIVATE gcov)
        target_compile_definitions(${PROJECT_NAME} PRIVATE TT_PGO_GEN)
    elseif(TT_PGO STREQUAL "use")
        list(APPEND TT_GEN_FLAGS -fprofile-use=${TT_PGO_DIR} -fprofile-partial-training -Wno-missing-profile -Wno-coverage-mismatch)
    endif()
    if(TT_GEN_FLAGS)
        set_source_files_properties(${RECOMP_GEN_SOURCES} PROPERTIES COMPILE_OPTIONS "${TT_GEN_FLAGS}")
    endif()
    # (v113) the sampling profiler on the Bink movie code only (sections BINK*, 0x13A1A0-0x14F800)
    # (v127: off again)
    #set_source_files_properties(src/recomp/gen/recomp_0032.c src/recomp/gen/recomp_0033.c
    #    PROPERTIES COMPILE_DEFINITIONS "${TT_GUEST_RENAMES};RECOMP_FAST_REGS;RECOMP_PROFILE")
    set_source_files_properties(src/twotowers_manual.c src/recomp_manual.c
        PROPERTIES COMPILE_DEFINITIONS "${TT_GUEST_RENAMES}")
    # (sampling profiler: postproc_prof.py's hooks, src/prof_vita.c)
    target_sources(${PROJECT_NAME} PRIVATE src/prof_vita.c)
    # (v132) the films on the video decoder
    target_sources(${PROJECT_NAME} PRIVATE src/hwmovie.c)
    # (v107) the hottest game functions written by hand, compiled like the
    # generated code
    target_sources(${PROJECT_NAME} PRIVATE src/native_hot.c)
    set_source_files_properties(src/native_hot.c
        PROPERTIES COMPILE_DEFINITIONS "${TT_GUEST_RENAMES};RECOMP_FAST_REGS")
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,-q)
    # (v79) vitaGL's 2 MB uniform pool replaced by a bigger one (video/fb_present.c)
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--wrap=vglSetupUniformCircularPool -Wl,--wrap=vglReserveUniformCircularPoolBuffer -Wl,--wrap=vgl_reserve_data_pool -Wl,--wrap=sceGxmShaderPatcherCreateVertexProgram -Wl,--wrap=sceGxmShaderPatcherCreateFragmentProgram -Wl,--wrap=sceGxmSetVertexProgram -Wl,--wrap=sceGxmSetFragmentProgram -Wl,--wrap=sceGxmDraw -Wl,--wrap=vglCreateVertexProgram -Wl,--wrap=sceClibMemcpy -Wl,--wrap=sceGxmSetVertexStream -Wl,--wrap=sceGxmSetFragmentTexture -Wl,--wrap=sceGxmSetVertexUniformBuffer -Wl,--wrap=sceGxmSetFragmentUniformBuffer -Wl,--wrap=sceGxmSetVertexDefaultUniformBuffer -Wl,--wrap=sceGxmSetFragmentDefaultUniformBuffer -Wl,--wrap=scene_reset -Wl,--wrap=sceGxmBeginScene -Wl,--wrap=sceGxmEndScene -Wl,--wrap=sceGxmCreateRenderTarget -Wl,--wrap=glActiveTexture -Wl,--wrap=glAttachShader -Wl,--wrap=glBindAttribLocation -Wl,--wrap=glBindBuffer -Wl,--wrap=glBindFramebuffer -Wl,--wrap=glBindRenderbuffer -Wl,--wrap=glBindSampler -Wl,--wrap=glBindTexture -Wl,--wrap=glBindVertexArray -Wl,--wrap=glBlendEquationSeparate -Wl,--wrap=glBlendFuncSeparate -Wl,--wrap=glBlitFramebuffer -Wl,--wrap=glBufferData -Wl,--wrap=glCheckFramebufferStatus -Wl,--wrap=glClear -Wl,--wrap=glClearColor -Wl,--wrap=glClearDepth -Wl,--wrap=glClearStencil -Wl,--wrap=glColorMask -Wl,--wrap=glCompileShader -Wl,--wrap=glCreateProgram -Wl,--wrap=glCreateShader -Wl,--wrap=glCullFace -Wl,--wrap=glDeleteFramebuffers -Wl,--wrap=glDeleteProgram -Wl,--wrap=glDeleteRenderbuffers -Wl,--wrap=glDeleteShader -Wl,--wrap=glDeleteTextures -Wl,--wrap=glDepthFunc -Wl,--wrap=glDepthMask -Wl,--wrap=glDisable -Wl,--wrap=glDisableVertexAttribArray -Wl,--wrap=glDrawArrays -Wl,--wrap=glDrawElements -Wl,--wrap=glDrawRangeElements -Wl,--wrap=glEnable -Wl,--wrap=glEnableVertexAttribArray -Wl,--wrap=glFinish -Wl,--wrap=glFlush -Wl,--wrap=glFramebufferRenderbuffer -Wl,--wrap=glFramebufferTexture2D -Wl,--wrap=glFrontFace -Wl,--wrap=glGenBuffers -Wl,--wrap=glGenFramebuffers -Wl,--wrap=glGenRenderbuffers -Wl,--wrap=glGenSamplers -Wl,--wrap=glGenTextures -Wl,--wrap=glGenVertexArrays -Wl,--wrap=glGenerateMipmap -Wl,--wrap=glGetError -Wl,--wrap=glGetIntegerv -Wl,--wrap=glGetProgramBinary -Wl,--wrap=glGetProgramInfoLog -Wl,--wrap=glGetProgramiv -Wl,--wrap=glGetShaderInfoLog -Wl,--wrap=glGetShaderiv -Wl,--wrap=glGetString -Wl,--wrap=glGetUniformBlockIndex -Wl,--wrap=glGetUniformLocation -Wl,--wrap=glLinkProgram -Wl,--wrap=glMapBuffer -Wl,--wrap=glPixelStorei -Wl,--wrap=glReadBuffer -Wl,--wrap=glReadPixels -Wl,--wrap=glRenderbufferStorage -Wl,--wrap=glSamplerParameterf -Wl,--wrap=glSamplerParameteri -Wl,--wrap=glScissor -Wl,--wrap=glShaderBinary -Wl,--wrap=glShaderSource -Wl,--wrap=glStencilFuncSeparate -Wl,--wrap=glStencilMask -Wl,--wrap=glStencilOpSeparate -Wl,--wrap=glTexImage2D -Wl,--wrap=glTexParameteri -Wl,--wrap=glTexSubImage2D -Wl,--wrap=glUniform1f -Wl,--wrap=glUniform1i -Wl,--wrap=glUniform4f -Wl,--wrap=glUniform4fv -Wl,--wrap=glUnmapBuffer -Wl,--wrap=glUseProgram -Wl,--wrap=glVertexAttrib4f -Wl,--wrap=glVertexAttribPointer -Wl,--wrap=glViewport -Wl,--wrap=vglAlloc -Wl,--wrap=vglFree -Wl,--wrap=vglGetGxmTexture -Wl,--wrap=vglGetTexDataPointer -Wl,--wrap=vglMalloc -Wl,--wrap=vglMemFree -Wl,--wrap=vglOverloadTexDataPointer -Wl,--wrap=vglSwapBuffers -Wl,--wrap=vglWaitVblankStart -Wl,--wrap=vglGetFrameNumber)
    set(VITA_APP_NAME "The Two Towers")
    set(VITA_TITLEID  "TTWR00001")
    set(VITA_VERSION  "01.00")
    vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
    vita_create_vpk(TwoTowers.vpk ${VITA_TITLEID} eboot.bin
        VERSION ${VITA_VERSION}
        NAME ${VITA_APP_NAME}
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/icon0.png sce_sys/icon0.png
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
        FILE ${CMAKE_CURRENT_SOURCE_DIR}/sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml)
else()
    # Linux: SDL2 (window, OpenGL context, sound, controllers); OpenGL itself
    # is loaded at run time through SDL.
    find_package(SDL2 REQUIRED)
    target_link_libraries(${PROJECT_NAME} PRIVATE SDL2::SDL2 ${CMAKE_DL_LIBS} m pthread)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-incompatible-pointer-types
        -Wno-int-conversion -Wno-format)
    # A few guest functions carry the names of the C library's (the XDK CRT's
    # close/write/lseek/isatty): in one ELF executable they would take the
    # place of the real ones for the host code and for every shared library.
    # The generated code gets them under other names, and nothing in the
    # executable is exported to the libraries it loads.
    set(TT_GUEST_RENAMES "close=tt_guest_close;write=tt_guest_write;lseek=tt_guest_lseek;isatty=tt_guest_isatty")
    set_source_files_properties(${RECOMP_GEN_SOURCES} src/twotowers_manual.c src/recomp_manual.c
        PROPERTIES COMPILE_DEFINITIONS "${TT_GUEST_RENAMES}")
    if(APPLE)
        # macOS (Intel, or Apple silicon through Rosetta 2: the build is
        # x86-64 -- see the README's macOS section). Mach-O's two-level
        # namespace already keeps the executable's symbols from the
        # libraries; the translated code nests deeply on the main thread's
        # stack (64 MB, as on Windows); SDL2 from lib/ beside the program
        # (or Homebrew's).
        # (lld has no -stack_size: a cross build sets LC_MAIN's stack size
        # afterwards -- tools/macho_stack.py; the rpaths are given outright)
        target_link_options(${PROJECT_NAME} PRIVATE -Wl,-stack_size,0x4000000
            -Wl,-rpath,@executable_path/lib -Wl,-rpath,@executable_path/../Frameworks
            -Wl,-rpath,/usr/local/lib -Wl,-rpath,/opt/homebrew/lib)
        set_target_properties(${PROJECT_NAME} PROPERTIES SKIP_BUILD_RPATH ON)
    else()
        file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/exe_exports.map" "{ local: *; };\n")
        target_link_options(${PROJECT_NAME} PRIVATE "-Wl,--version-script=${CMAKE_CURRENT_BINARY_DIR}/exe_exports.map")
        # optional bundled libraries live in lib/ beside the executable; only
        # that (not the build tree's library paths) goes into the binary
        set_target_properties(${PROJECT_NAME} PROPERTIES
            BUILD_WITH_INSTALL_RPATH ON INSTALL_RPATH "$ORIGIN/lib")
    endif()
endif()

# MSVC-specific: large address aware, increase section limit for big generated files
if(MSVC)
    target_link_options(${PROJECT_NAME} PRIVATE
        /LARGEADDRESSAWARE
        /STACK:67108864     # translated code nests deeply on the native stack
    )
    # The generated code files can be very large. Increase object file section limit.
    target_compile_options(${PROJECT_NAME} PRIVATE /bigobj)
endif()

# ── Game-specific additions ─────────────────────────────────────
#
# Add any game-specific libraries, loaders, or custom renderers below.
# Examples:
#
# # Custom texture loader for your game's format
# target_sources(${PROJECT_NAME} PRIVATE src/texture_loader.c)
#
# # If your game uses RenderWare, you may need a RW bridge layer
# target_sources(${PROJECT_NAME} PRIVATE src/rw_bridge.c)
#
# # Additional compile definitions for debugging
# target_compile_definitions(${PROJECT_NAME} PRIVATE
#     ENABLE_ICALL_TRACE=1
#     VERBOSE_KERNEL_LOG=1
# )

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
# (1.9.2: lang_ultra.cpp is C++) the C++ runtime inside, as twotowers-setup's:
# the build's libstdc++ is newer than older systems' (Ubuntu 18.04)
if(UNIX AND NOT APPLE)
    target_link_options(${PROJECT_NAME} PRIVATE -static-libstdc++ -static-libgcc)
endif()
# MinGW (cross) builds: equivalents of the MSVC options above.
if(MINGW)
    target_compile_options(${PROJECT_NAME} PRIVATE -Wno-incompatible-pointer-types
        -Wno-int-conversion -Wno-parentheses-equality)
    # Translated code nests deeply on the native stack.
    target_link_options(${PROJECT_NAME} PRIVATE -Wl,--stack,67108864 -static)
endif()

# First-launch setup / launcher: installs the game files from your ISO or
# points at an extracted folder, then starts twotowers_recomp.exe.
if(WIN32)
# (with the Ultra textures builder: tools/ultra_textures/ultra_build.h)
set(TT_ULTRA_BUILD_SRC tools/ultra_textures/bc7enc.cpp tools/ultra_textures/bc7decomp.cpp tools/ultra_textures/lodepng.cpp)
add_executable(twotowers_launcher WIN32 src/setup_launcher.cpp ${TT_ULTRA_BUILD_SRC} ${CMAKE_CURRENT_BINARY_DIR}/launcher.rc)
set_target_properties(twotowers_launcher PROPERTIES CXX_STANDARD 17
    OUTPUT_NAME "The Two Towers Recompiled")
target_link_libraries(twotowers_launcher PRIVATE comctl32 comdlg32 shell32 ole32 bcrypt)
if(MINGW)
    target_link_options(twotowers_launcher PRIVATE -static -municode)
endif()
elseif(NOT VITA)
# Linux: twotowers-setup (disc check/extraction, a few ini settings), run by
# TwoTowers.sh. Statically linked C++ runtime so it runs on older systems.
add_executable(twotowers-setup src/linux_setup.cpp tools/ultra_textures/bc7enc.cpp tools/ultra_textures/bc7decomp.cpp tools/ultra_textures/lodepng.cpp)
find_package(Threads REQUIRED)
target_link_libraries(twotowers-setup PRIVATE Threads::Threads)
set_target_properties(twotowers-setup PROPERTIES CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON)
if(NOT APPLE)
    target_link_options(twotowers-setup PRIVATE -static-libstdc++ -static-libgcc)
endif()
endif()
