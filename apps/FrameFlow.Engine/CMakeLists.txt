add_executable(FrameFlow.Engine WIN32 main.cpp EngineApp.cpp EngineSettings.cpp MiniOverlay.cpp)
target_link_libraries(FrameFlow.Engine PRIVATE
    ff_core ff_timing ff_telemetry ff_ipc ff_process ff_profiles ff_display ff_graphics ff_etw)
if(MSVC)
    target_link_options(FrameFlow.Engine PRIVATE /ENTRY:wWinMainCRTStartup)
else()
    target_link_options(FrameFlow.Engine PRIVATE -municode)
endif()
