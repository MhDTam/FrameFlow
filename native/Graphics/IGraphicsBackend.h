#pragma once
#include <cstdint>
#include <memory>
#include <string_view>

namespace ff::graphics {

enum class GraphicsApi : std::uint8_t { Unknown, DX9, DX11, DX12, Vulkan, OpenGL };

std::string_view ToString(GraphicsApi api);

enum class AttachStatus : std::uint8_t {
    Attached,
    NotImplemented,   // backend exists in the architecture but is not shipped yet
    Protected,        // anti-cheat detected: integration deliberately refused
    AccessDenied,     // game runs elevated and FrameFlow does not
    Failed,
};

struct PresentStatistics {
    std::uint64_t presentCount = 0;
    std::int64_t lastPresentQpc = 0;
};

// One implementation per graphics API. The pacing algorithm itself lives in ff::timing and is
// shared; a backend only decides WHERE in the presentation path BeforePresent/AfterPresent run.
//
// V1 plan: DX11 and DX12 share a DXGI backend that intercepts IDXGISwapChain::Present/Present1
// from inside FrameFlow.Hook64.dll. The hook is built only after the standalone timing engine
// has been validated with FrameFlow.TestRenderer + FrameFlow.Benchmark (see docs/ROADMAP.md).
class IGraphicsBackend {
public:
    virtual ~IGraphicsBackend() = default;

    virtual GraphicsApi Api() const = 0;
    virtual bool Initialize() = 0;
    virtual AttachStatus Attach(std::uint32_t pid) = 0;
    virtual void Detach() = 0;
    virtual PresentStatistics GetPresentStatistics() const = 0;

    // Called inside the presenting thread of the target process.
    virtual void BeforePresent() = 0;
    virtual void AfterPresent() = 0;
};

// Returns the backend for an API, or nullptr when that API has no backend in this build.
std::unique_ptr<IGraphicsBackend> CreateBackend(GraphicsApi api);

// Whether an injection-based backend for this API exists in this build.
bool BackendAvailable(GraphicsApi api);

} // namespace ff::graphics
