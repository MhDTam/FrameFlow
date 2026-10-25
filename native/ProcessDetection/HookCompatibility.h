#pragma once

#include "Core/Strings.h"

#include <string_view>

namespace ff::process {

// ETS2 1.61.1.9 recursed through Steam Overlay with the normal DXGI vtable hook.
// Use the Present function detour on opt-in late attach. Direct early launch is still
// blocked because Steam relaunches the executable before FrameFlow reaches its entry point.
inline bool IsKnownUnsafeHookTarget(std::string_view exeName)
{
    return core::IEquals(exeName, "eurotrucks2.exe");
}

} // namespace ff::process
