add_library(FrameFlow.Hook64 SHARED DxgiHook.cpp)
target_link_libraries(FrameFlow.Hook64 PRIVATE ff_core ff_timing ff_ipc ff_graphics ff_minhook d3d11 dxgi psapi)
set_target_properties(FrameFlow.Hook64 PROPERTIES PREFIX "")
