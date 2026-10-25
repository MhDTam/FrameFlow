#pragma once

#include "Core/Strings.h"

#include <string_view>

namespace ff::process {

// ETS2 1.61.1.9 repeatedly access-violated within seconds of DXGI attachment on
// the development machine. Keep its process unmodified until the hook is fixed
// and compatibility is verified against the game itself.
inline bool IsKnownUnsafeHookTarget(std::string_view exeName)
{
    return core::IEquals(exeName, "eurotrucks2.exe");
}

} // namespace ff::process
