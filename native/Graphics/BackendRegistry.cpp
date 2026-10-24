#include "Graphics/IGraphicsBackend.h"

namespace ff::graphics {

std::string_view ToString(GraphicsApi api)
{
    switch (api) {
    case GraphicsApi::DX9: return "DX9";
    case GraphicsApi::DX11: return "DX11";
    case GraphicsApi::DX12: return "DX12";
    case GraphicsApi::Vulkan: return "Vulkan";
    case GraphicsApi::OpenGL: return "OpenGL";
    default: return "Unknown";
    }
}

// Phase 1: no in-game backend ships yet. DX11 (Phase 3) and DX12 (Phase 4) register here;
// see Graphics/DX11/README.md and Graphics/DX12/README.md for the planned integration points.
std::unique_ptr<IGraphicsBackend> CreateBackend(GraphicsApi) { return nullptr; }

bool BackendAvailable(GraphicsApi) { return false; }

} // namespace ff::graphics
