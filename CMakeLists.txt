cmake_minimum_required(VERSION 3.21)
project(FrameFlow VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
    message(FATAL_ERROR "FrameFlow V1 targets x64 only.")
endif()

set(FF_BIN_DIR ${CMAKE_SOURCE_DIR}/bin CACHE PATH "Directory for built executables and DLLs")
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${FF_BIN_DIR})
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
    string(TOUPPER ${cfg} CFG)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CFG} ${FF_BIN_DIR})
endforeach()

add_compile_definitions(
    UNICODE _UNICODE
    WIN32_LEAN_AND_MEAN NOMINMAX
    _WIN32_WINNT=0x0A00
    FF_VERSION_STRING="${PROJECT_VERSION}")

if(MSVC)
    add_compile_options(/W4 /WX /permissive- /utf-8 /Zc:__cplusplus /EHsc /MP)
    # Static CRT: engine/renderer/benchmark run without a VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    add_compile_options(-Wall -Wextra -Werror)
endif()

enable_testing()

add_subdirectory(native)
add_subdirectory(apps/FrameFlow)
add_subdirectory(apps/FrameFlow.Engine)
add_subdirectory(apps/FrameFlow.TestRenderer)
add_subdirectory(apps/FrameFlow.DX12Probe)
add_subdirectory(apps/FrameFlow.SessionProbe)
add_subdirectory(apps/FrameFlow.Benchmark)
add_subdirectory(apps/FrameFlow.Launch)
add_subdirectory(native/Hooks)
add_subdirectory(tests)
