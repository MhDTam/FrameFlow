# Each area is its own static library so dependencies stay explicit:
# Timing knows nothing about IPC, Telemetry knows nothing about processes, etc.

function(ff_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;DEPS;SYSLIBS" ${ARGN})
    add_library(${name} STATIC ${ARG_SOURCES})
    target_include_directories(${name} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(${name} PUBLIC ${ARG_DEPS} ${ARG_SYSLIBS})
endfunction()

ff_library(ff_core
    SOURCES Core/Log.cpp Core/Json.cpp Core/Paths.cpp Core/Strings.cpp
    SYSLIBS shell32 ole32)

ff_library(ff_timing
    SOURCES Timing/Clock.cpp Timing/WaitableTimer.cpp Timing/FramePacer.cpp Timing/NaiveSleepLimiter.cpp
    DEPS ff_core
    SYSLIBS winmm)

ff_library(ff_telemetry
    SOURCES Telemetry/FrameStatistics.cpp
    DEPS ff_core)

ff_library(ff_ipc
    SOURCES IPC/SharedSession.cpp IPC/PipeServer.cpp IPC/Protocol.cpp
    DEPS ff_core ff_telemetry
    SYSLIBS advapi32)

ff_library(ff_process
    SOURCES ProcessDetection/GameScorer.cpp ProcessDetection/AntiCheatDetector.cpp ProcessDetection/ProcessScanner.cpp
    DEPS ff_core ff_ipc
    SYSLIBS psapi user32 advapi32 version)

ff_library(ff_profiles
    SOURCES Profiles/ProfileStore.cpp
    DEPS ff_core
    SYSLIBS version)

ff_library(ff_display
    SOURCES Display/DisplayInfo.cpp Display/FpsPresets.cpp
    DEPS ff_core
    SYSLIBS user32)

ff_library(ff_graphics
    SOURCES Graphics/PresentPacer.cpp Graphics/BackendRegistry.cpp
    DEPS ff_core ff_timing ff_ipc)

ff_library(ff_etw
    SOURCES Etw/PresentMonitor.cpp
    DEPS ff_core
    SYSLIBS advapi32)
