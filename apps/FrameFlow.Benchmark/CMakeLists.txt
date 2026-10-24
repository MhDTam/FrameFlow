add_executable(FrameFlow.Benchmark main.cpp)
target_link_libraries(FrameFlow.Benchmark PRIVATE ff_core ff_timing ff_telemetry)
