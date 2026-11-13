cmake_minimum_required(VERSION 3.20)
project(top_gear_overdrive_port C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
    add_compile_definitions(NOMINMAX)
endif()

option(TGOD_BUILD_GAME "Build the game runner (requires local ROM-generated C)" ON)
option(TGOD_ENABLE_IPO "Enable interprocedural optimization for the game runner when supported" OFF)
set(TGOD_PGO_MODE "OFF" CACHE STRING "GCC profile-guided optimization: OFF, GENERATE or USE")
set_property(CACHE TGOD_PGO_MODE PROPERTY STRINGS OFF GENERATE USE)
set(TGOD_PGO_DIR "${CMAKE_CURRENT_BINARY_DIR}/pgo-profile" CACHE PATH "Local GCC profile data directory")
add_executable(tgod_shader_compiler EXCLUDE_FROM_ALL tools/compile_gpu_shader.cpp)
target_link_libraries(tgod_shader_compiler PRIVATE d3dcompiler)
set(TGOD_GPU_SHADER "${CMAKE_CURRENT_BINARY_DIR}/gpu-generated/gpu_shader.hpp")
add_custom_command(OUTPUT "${TGOD_GPU_SHADER}"
    COMMAND $<TARGET_FILE:tgod_shader_compiler> "${TGOD_GPU_SHADER}"
    DEPENDS tgod_shader_compiler src/gpu_raster_shader.inc src/gpu_present_shader.inc src/gpu_import_shader.inc
    VERBATIM)
set(TGOD_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/N64ModernRuntime" CACHE PATH "N64ModernRuntime checkout")
set(TGOD_RECOMPILER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/N64Recomp" CACHE PATH "N64Recomp checkout")
if (NOT EXISTS "${TGOD_RUNTIME_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Dependencies missing. Run tools/bootstrap.ps1 first.")
endif()
add_subdirectory("${TGOD_RUNTIME_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/tools/N64ModernRuntime")

# Gather generated recompiled functions
if (TGOD_BUILD_GAME)
set(TGOD_SDL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/SDL" CACHE PATH "Pinned SDL3 checkout")
if (NOT EXISTS "${TGOD_SDL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "SDL3 missing. Run tools/bootstrap.ps1 or set TGOD_SDL_DIR.")
endif()
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TESTS OFF CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
# Input only: Win32/DXGI and WaveOut continue to own video and audio.
foreach(subsystem AUDIO VIDEO GPU RENDER CAMERA DIALOG TRAY)
    set(SDL_${subsystem} OFF CACHE BOOL "" FORCE)
endforeach()
add_subdirectory("${TGOD_SDL_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/tools/SDL" EXCLUDE_FROM_ALL)
if(MINGW)
    # windres loses quoting for inherited -I paths containing spaces. This
    # resource only includes the system winresrc.h; compile it without those -I's.
    get_target_property(sdl_sources SDL3-shared SOURCES)
    list(FILTER sdl_sources EXCLUDE REGEX "/version\\.rc$")
    set_property(TARGET SDL3-shared PROPERTY SOURCES "${sdl_sources}")
    set(sdl_version_object "${CMAKE_CURRENT_BINARY_DIR}/sdl-version.obj")
    add_custom_command(OUTPUT "${sdl_version_object}"
        COMMAND "${CMAKE_RC_COMPILER}" -O coff "${TGOD_SDL_DIR}/src/core/windows/version.rc" "${sdl_version_object}"
        DEPENDS "${TGOD_SDL_DIR}/src/core/windows/version.rc" VERBATIM)
    set_source_files_properties("${sdl_version_object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
    add_custom_target(tgod_sdl_version DEPENDS "${sdl_version_object}")
    add_dependencies(SDL3-shared tgod_sdl_version)
    target_sources(SDL3-shared PRIVATE "${sdl_version_object}")
endif()
file(GLOB RECOMP_SOURCES
    "generated/RecompiledFuncs/funcs_*.c"
)
if (NOT RECOMP_SOURCES OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/RecompiledFuncs/recomp_overlays.inl")
    message(FATAL_ERROR "Generate local game sources with tools/prepare_recomp.py before building the runner, or set TGOD_BUILD_GAME=OFF for ROM-independent checks.")
endif()

add_executable(tgod_native
    src/controller_profiles.cpp
    src/controller_mapper.cpp
    src/sdl_input.cpp
    src/graphics_enhancements.cpp
    src/main.cpp
    src/rom_loader.cpp
    src/f3dex2.cpp
    src/gpu_raster.cpp
    "${TGOD_GPU_SHADER}"
    generated/RecompiledFuncs/lookup.cpp
    ${RECOMP_SOURCES}
)

if(TGOD_ENABLE_IPO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT TGOD_IPO_SUPPORTED OUTPUT TGOD_IPO_ERROR LANGUAGES C CXX)
    if(NOT TGOD_IPO_SUPPORTED)
        message(FATAL_ERROR "IPO requested but unavailable: ${TGOD_IPO_ERROR}")
    endif()
    set_property(TARGET tgod_native PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()

if(NOT TGOD_PGO_MODE STREQUAL "OFF")
    if(NOT CMAKE_C_COMPILER_ID STREQUAL "GNU" OR NOT CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        message(FATAL_ERROR "TGOD_PGO_MODE requires GCC for both C and C++")
    endif()
    if(NOT CMAKE_BUILD_TYPE STREQUAL "Release" OR TGOD_ENABLE_IPO)
        message(FATAL_ERROR "PGO requires a Release build with TGOD_ENABLE_IPO=OFF")
    endif()
    if(TGOD_PGO_MODE STREQUAL "GENERATE")
        target_compile_definitions(tgod_native PRIVATE TGOD_PGO_GENERATE=1)
        target_compile_options(tgod_native PRIVATE "-fprofile-generate=${TGOD_PGO_DIR}" -fprofile-update=atomic)
        target_link_options(tgod_native PRIVATE "-fprofile-generate=${TGOD_PGO_DIR}")
    elseif(TGOD_PGO_MODE STREQUAL "USE")
        if(NOT EXISTS "${TGOD_PGO_DIR}")
            message(FATAL_ERROR "PGO profile directory missing; train the GENERATE build first")
        endif()
        target_compile_options(tgod_native PRIVATE "-fprofile-use=${TGOD_PGO_DIR}" -fprofile-correction -Werror=missing-profile)
        target_link_options(tgod_native PRIVATE "-fprofile-use=${TGOD_PGO_DIR}")
        # Keep the instrumented and optimized main() control flow identical.
        # In USE mode no counters are registered, so the final dump is a no-op.
        target_compile_definitions(tgod_native PRIVATE TGOD_PGO_GENERATE=1)
        target_link_libraries(tgod_native PRIVATE gcov)
    else()
        message(FATAL_ERROR "TGOD_PGO_MODE must be OFF, GENERATE or USE")
    endif()
endif()

target_include_directories(tgod_native PRIVATE
    include
    "${CMAKE_CURRENT_BINARY_DIR}/gpu-generated"
    src
    generated/RecompiledFuncs
    "${TGOD_RECOMPILER_DIR}/include"
    "${TGOD_RUNTIME_DIR}/librecomp/include"
    "${TGOD_RUNTIME_DIR}/ultramodern/include"
    "${TGOD_RUNTIME_DIR}/thirdparty"
    "${TGOD_RUNTIME_DIR}/thirdparty/concurrentqueue"
)

target_link_libraries(tgod_native PRIVATE
    SDL3::SDL3-shared
    librecomp
    ultramodern
    miniz
    user32
    gdi32
    winmm
    comdlg32
    shell32
    bcrypt
    d3d11
    dxgi
)
add_custom_command(TARGET tgod_native POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_FILE:SDL3-shared> $<TARGET_FILE_DIR:tgod_native>/SDL3.dll)
endif()

# Small ROM-independent checks for the renderer and cooperative thread queues.
add_executable(tgod_cpu_benchmark EXCLUDE_FROM_ALL tools/cpu_raster_benchmark.cpp src/f3dex2.cpp)
target_include_directories(tgod_cpu_benchmark PRIVATE include)
add_executable(tgod_native_gpu_benchmark EXCLUDE_FROM_ALL tools/cpu_raster_benchmark.cpp src/f3dex2.cpp src/gpu_raster.cpp "${TGOD_GPU_SHADER}")
target_compile_definitions(tgod_native_gpu_benchmark PRIVATE TGOD_BENCH_GPU)
target_include_directories(tgod_native_gpu_benchmark PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/gpu-generated")
target_link_libraries(tgod_native_gpu_benchmark PRIVATE d3d11 dxgi)

add_executable(tgod_regression_checks EXCLUDE_FROM_ALL
    tools/renderer_regression_checks.cpp
    src/f3dex2.cpp
    src/rom_loader.cpp
)
target_include_directories(tgod_regression_checks PRIVATE include)
target_link_libraries(tgod_regression_checks PRIVATE ultramodern comdlg32 bcrypt)

# Requires a hardware D3D11 adapter; separate from ROM-independent software checks.
add_executable(tgod_gpu_checks EXCLUDE_FROM_ALL
    tools/gpu_regression_checks.cpp src/gpu_raster.cpp src/f3dex2.cpp "${TGOD_GPU_SHADER}")
target_include_directories(tgod_gpu_checks PRIVATE include "${CMAKE_CURRENT_BINARY_DIR}/gpu-generated")
target_link_libraries(tgod_gpu_checks PRIVATE d3d11 dxgi d3dcompiler)
