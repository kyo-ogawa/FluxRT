cmake_minimum_required(VERSION 3.20)
project(FluxRTTop CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Sources ───────────────────────────────────────────────────────────────────
add_library(FluxRTTop SHARED
    src/FluxRTTop.cpp
    src/ProcessLauncher.cpp
)

# ── Include paths ─────────────────────────────────────────────────────────────
target_include_directories(FluxRTTop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/TD_SDK
)

# ── Compiler flags ────────────────────────────────────────────────────────────
target_compile_options(FluxRTTop PRIVATE /W3 /WX-)
target_compile_definitions(FluxRTTop PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

# ── Output name (TD expects just the base name, no 'lib' prefix) ──────────────
set_target_properties(FluxRTTop PROPERTIES
    OUTPUT_NAME "FluxRTTop"
    PREFIX ""
)
