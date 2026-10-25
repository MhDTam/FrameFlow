#pragma once

#include "Core/Strings.h"

#include <string_view>

namespace ff::process {

// ETS2 1.61.1.9 repeatedly access-violated after attaching to a running process.
// Direct early launch exits before the game initializes because Steam relaunches it.
// Keep both hook paths disabled until a Steam-compatible integration is verified.
inline bool IsKnownUnsafeHookTarget(std::string_view exeName)
{
    return core::IEquals(exeName, "eurotrucks2.exe");
}

} // namespace ff::process
