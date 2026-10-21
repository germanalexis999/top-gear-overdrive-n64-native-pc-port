cmake_minimum_required(VERSION 3.20)
project(top_gear_overdrive_port C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (WIN32)
    add_compile_definitions(NOMINMAX)
endif()

option(TGOD_BUILD_GAME "Build the game runner (requires local ROM-generated C)" ON)
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
    src/f3dex2.cpp
    generated/RecompiledFuncs/lookup.cpp
    ${RECOMP_SOURCES}
)

target_include_directories(tgod_native PRIVATE
    include
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
)
endif()

# Small ROM-independent checks for the renderer and cooperative thread queues.
add_executable(tgod_regression_checks EXCLUDE_FROM_ALL
    tools/renderer_regression_checks.cpp
    src/f3dex2.cpp
)
target_include_directories(tgod_regression_checks PRIVATE include)
target_link_libraries(tgod_regression_checks PRIVATE ultramodern)
