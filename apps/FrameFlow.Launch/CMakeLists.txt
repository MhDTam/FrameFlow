add_executable(FrameFlow.Launch main.cpp)
target_link_libraries(FrameFlow.Launch PRIVATE ff_core ff_process psapi)
