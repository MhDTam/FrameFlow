add_executable(FrameFlow.SessionProbe main.cpp)
target_link_libraries(FrameFlow.SessionProbe PRIVATE ff_ipc ff_telemetry)
