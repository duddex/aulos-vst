cmake_minimum_required(VERSION 3.15)
project(aulos_standalone CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Phase 1: offline WAV render harness proving the Rack-free engine.
add_executable(render_wav
    test/render_wav.cpp
    dsp/AulosEngine.cpp
)
target_include_directories(render_wav PRIVATE dsp)

if (MSVC)
    target_compile_options(render_wav PRIVATE /O2 /fp:fast)
else()
    target_compile_options(render_wav PRIVATE -O2 -ffast-math)
endif()
