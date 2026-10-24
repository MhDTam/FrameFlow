#pragma once

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace ff::engine {

// FrameFlow's own click-through mini window. It follows the selected game's client area
// and draws independently of RTSS or any other overlay software.
class MiniOverlay {
public:
    MiniOverlay();
    ~MiniOverlay();
    MiniOverlay(const MiniOverlay&) = delete;
    MiniOverlay& operator=(const MiniOverlay&) = delete;

    void Update(HWND gameWindow, DWORD gamePid, double fps, double frametimeMs,
                double low1Fps, double targetMs, const std::deque<double>& history);
    void Clear();

private:
    struct FrameData {
        HWND gameWindow = nullptr;
        DWORD gamePid = 0;
        double fps = 0;
        double frametimeMs = 0;
        double low1Fps = 0;
        double targetMs = 0;
        std::vector<float> graph;
        bool active = false;
    };

    void Run();
    void Paint(HWND window);
    void Draw(HDC screen);
    static LRESULT CALLBACK WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam);

    std::mutex dataMutex_;
    FrameData data_;
    std::uint64_t revision_ = 0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

} // namespace ff::engine
