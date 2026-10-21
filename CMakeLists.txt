cmake_minimum_required(VERSION 3.20)
project(top_gear_overdrive_port C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
    add_compile_definitions(NOMINMAX)
endif()

option(TGOD_BUILD_GAME "Build the game runner (requires local ROM-generated C)" ON)
add_executable(tgod_shader_compiler EXCLUDE_FROM_ALL tools/compile_gpu_shader.cpp)
target_link_libraries(tgod_shader_compiler PRIVATE d3dcompiler)
set(TGOD_GPU_SHADER "${CMAKE_CURRENT_BINARY_DIR}/gpu-generated/gpu_shader.hpp")
add_custom_command(OUTPUT "${TGOD_GPU_SHADER}"
    COMMAND $<TARGET_FILE:tgod_shader_compiler> "${TGOD_GPU_SHADER}"
    DEPENDS tgod_shader_compiler src/gpu_raster_shader.inc src/gpu_present_shader.inc
    VERBATIM)
set(TGOD_RUNTIME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/N64ModernRuntime" CACHE PATH "N64ModernRuntime checkout")
set(TGOD_RECOMPILER_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tools/N64Recomp" CACHE PATH "N64Recomp checkout")
if (NOT EXISTS "${TGOD_RUNTIME_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Dependencies missing. Run tools/bootstrap.ps1 first.")
endif()
add_subdirectory("${TGOD_RUNTIME_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/tools/N64ModernRuntime")

# Gather generated recompiled functions
if (TGOD_BUILD_GAME)
file(GLOB RECOMP_SOURCES
    "generated/RecompiledFuncs/funcs_*.c"
)
if (NOT RECOMP_SOURCES OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/RecompiledFuncs/recomp_overlays.inl")
    message(FATAL_ERROR "Generate local game sources with tools/prepare_recomp.py before building the runner, or set TGOD_BUILD_GAME=OFF for ROM-independent checks.")
endif()

add_executable(tgod_native
    src/main.cpp
    src/rom_loader.cpp
    src/f3dex2.cpp
    src/gpu_raster.cpp
    "${TGOD_GPU_SHADER}"
    generated/RecompiledFuncs/lookup.cpp
    ${RECOMP_SOURCES}
)

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
endif()

# Small ROM-independent checks for the renderer and cooperative thread queues.
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
target_link_libraries(tgod_gpu_checks PRIVATE d3d11 dxgi)
