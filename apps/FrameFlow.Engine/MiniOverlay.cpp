#include "MiniOverlay.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

namespace ff::engine {
namespace {

constexpr int kWidth = 320;
constexpr int kHeight = 150;
constexpr int kGraphLeft = 18;
constexpr int kGraphRight = kWidth - 18;
constexpr int kGraphTop = 94;
constexpr int kGraphBottom = kHeight - 15;
constexpr wchar_t kWindowClass[] = L"FrameFlow.MiniOverlay";

void Text(HDC dc, int x, int y, COLORREF color, const wchar_t* value)
{
    ::SetTextColor(dc, color);
    ::TextOutW(dc, x, y, value, static_cast<int>(std::wcslen(value)));
}

} // namespace

MiniOverlay::MiniOverlay() : thread_([this] { Run(); }) {}

MiniOverlay::~MiniOverlay()
{
    stop_.store(true, std::memory_order_release);
    if (thread_.joinable()) thread_.join();
}

void MiniOverlay::Update(HWND gameWindow, DWORD gamePid, double fps, double frametimeMs,
                         double low1Fps, double targetMs, const std::deque<double>& history)
{
    if (!std::isfinite(fps) || !std::isfinite(frametimeMs) || fps <= 0 || !gameWindow) {
        Clear();
        return;
    }
    FrameData next;
    next.gameWindow = gameWindow;
    next.gamePid = gamePid;
    next.fps = fps;
    next.frametimeMs = frametimeMs;
    next.low1Fps = low1Fps;
    next.targetMs = targetMs;
    next.active = true;
    const auto count = std::min<std::size_t>(history.size(), 120);
    auto it = history.end();
    for (std::size_t i = 0; i < count; ++i) --it;
    next.graph.reserve(count);
    for (; it != history.end(); ++it)
        next.graph.push_back(static_cast<float>(std::clamp(*it, 0.0, 2000.0)));
    std::scoped_lock lock(dataMutex_);
    data_ = std::move(next);
    ++revision_;
}

void MiniOverlay::Clear()
{
    std::scoped_lock lock(dataMutex_);
    if (data_.active) {
        data_ = FrameData{};
        ++revision_;
    }
}

LRESULT CALLBACK MiniOverlay::WindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == WM_NCCREATE) {
        const auto* created = reinterpret_cast<CREATESTRUCTW*>(lParam);
        ::SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(created->lpCreateParams));
    }
    auto* self = reinterpret_cast<MiniOverlay*>(::GetWindowLongPtrW(window, GWLP_USERDATA));
    switch (message) {
    case WM_NCHITTEST: return HTTRANSPARENT;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
        if (self) self->Paint(window);
        return 0;
    case WM_PRINTCLIENT:
        if (self) self->Draw(reinterpret_cast<HDC>(wParam));
        return 0;
    case WM_NCDESTROY:
        ::SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    default: break;
    }
    return ::DefWindowProcW(window, message, wParam, lParam);
}

void MiniOverlay::Run()
{
    const HINSTANCE instance = ::GetModuleHandleW(nullptr);
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &MiniOverlay::WindowProc;
    wc.hInstance = instance;
    wc.lpszClassName = kWindowClass;
    if (!::RegisterClassExW(&wc)) return;
    HWND window = ::CreateWindowExW(WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                                    WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                                    kWindowClass, L"FrameFlow Mini", WS_POPUP,
                                    0, 0, kWidth, kHeight, nullptr, nullptr, instance, this);
    if (!window) {
        ::UnregisterClassW(kWindowClass, instance);
        return;
    }
    ::SetLayeredWindowAttributes(window, 0, 236, LWA_ALPHA);
    std::uint64_t paintedRevision = 0;
    bool shown = false;
    while (!stop_.load(std::memory_order_acquire)) {
        MSG message{};
        while (::PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            ::TranslateMessage(&message);
            ::DispatchMessageW(&message);
        }
        FrameData frame;
        std::uint64_t revision;
        {
            std::scoped_lock lock(dataMutex_);
            frame = data_;
            revision = revision_;
        }
        DWORD foregroundPid = 0;
        ::GetWindowThreadProcessId(::GetForegroundWindow(), &foregroundPid);
        const bool visible = frame.active && ::IsWindow(frame.gameWindow) &&
            ::IsWindowVisible(frame.gameWindow) && !::IsIconic(frame.gameWindow) &&
            (foregroundPid == frame.gamePid || foregroundPid == 0);
        if (visible) {
            RECT client{};
            POINT topLeft{};
            if (::GetClientRect(frame.gameWindow, &client) && ::ClientToScreen(frame.gameWindow, &topLeft) &&
                client.right - client.left >= kWidth + 20 && client.bottom - client.top >= kHeight + 20) {
                ::SetWindowPos(window, HWND_TOPMOST, topLeft.x + 16, topLeft.y + 16, kWidth, kHeight,
                               SWP_NOACTIVATE | SWP_SHOWWINDOW);
                shown = true;
                if (revision != paintedRevision) {
                    ::InvalidateRect(window, nullptr, FALSE);
                    paintedRevision = revision;
                }
            } else if (shown) {
                ::ShowWindow(window, SW_HIDE);
                shown = false;
            }
        } else if (shown) {
            ::ShowWindow(window, SW_HIDE);
            shown = false;
        }
        ::MsgWaitForMultipleObjectsEx(0, nullptr, 66, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
    ::DestroyWindow(window);
    ::UnregisterClassW(kWindowClass, instance);
}

void MiniOverlay::Paint(HWND window)
{
    PAINTSTRUCT paint{};
    HDC screen = ::BeginPaint(window, &paint);
    if (!screen) return;
    Draw(screen);
    ::EndPaint(window, &paint);
}

void MiniOverlay::Draw(HDC screen)
{
    FrameData frame;
    {
        std::scoped_lock lock(dataMutex_);
        frame = data_;
    }
    HDC buffer = ::CreateCompatibleDC(screen);
    HBITMAP bitmap = ::CreateCompatibleBitmap(screen, kWidth, kHeight);
    if (!buffer || !bitmap) {
        if (bitmap) ::DeleteObject(bitmap);
        if (buffer) ::DeleteDC(buffer);
        return;
    }
    HGDIOBJ previousBitmap = ::SelectObject(buffer, bitmap);
    HBRUSH background = ::CreateSolidBrush(RGB(18, 31, 41));
    RECT all{0, 0, kWidth, kHeight};
    ::FillRect(buffer, &all, background);
    ::DeleteObject(background);
    ::SetBkMode(buffer, TRANSPARENT);

    HFONT titleFont = ::CreateFontW(18, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
                                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                   CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HFONT bodyFont = ::CreateFontW(15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
    HGDIOBJ previousFont = ::SelectObject(buffer, titleFont);
    Text(buffer, 16, 9, RGB(116, 220, 222), L"FrameFlow Mini");
    ::SelectObject(buffer, bodyFont);
    wchar_t line[128]{};
    swprintf_s(line, L"FPS  %.1f       1%% low  %.1f", frame.fps, frame.low1Fps);
    Text(buffer, 16, 35, RGB(241, 249, 251), line);
    swprintf_s(line, L"Frametime  %.2f ms", frame.frametimeMs);
    Text(buffer, 16, 57, RGB(177, 203, 212), line);

    HPEN axis = ::CreatePen(PS_SOLID, 1, RGB(58, 82, 95));
    HPEN previousPen = static_cast<HPEN>(::SelectObject(buffer, axis));
    ::MoveToEx(buffer, kGraphLeft, kGraphBottom, nullptr);
    ::LineTo(buffer, kGraphRight, kGraphBottom);
    ::SelectObject(buffer, previousPen);
    ::DeleteObject(axis);
    if (frame.graph.size() >= 2) {
        const double reference = frame.targetMs > 0 ? frame.targetMs : 1000.0 / frame.fps;
        const double maxValue = std::max(reference * 1.8, 1.0);
        const int graphHeight = kGraphBottom - kGraphTop;
        auto y = [&](float value) {
            return kGraphBottom - static_cast<int>(std::clamp(static_cast<double>(value), 0.0, maxValue) /
                                                    maxValue * graphHeight);
        };
        HPEN target = ::CreatePen(PS_DOT, 1, RGB(66, 112, 122));
        previousPen = static_cast<HPEN>(::SelectObject(buffer, target));
        const int targetY = kGraphBottom - static_cast<int>(reference / maxValue * graphHeight);
        ::MoveToEx(buffer, kGraphLeft, targetY, nullptr);
        ::LineTo(buffer, kGraphRight, targetY);
        ::SelectObject(buffer, previousPen);
        ::DeleteObject(target);
        HPEN graph = ::CreatePen(PS_SOLID, 2, RGB(110, 221, 222));
        previousPen = static_cast<HPEN>(::SelectObject(buffer, graph));
        for (std::size_t i = 0; i < frame.graph.size(); ++i) {
            const int x = kGraphLeft + static_cast<int>(i * (kGraphRight - kGraphLeft) / (frame.graph.size() - 1));
            if (i == 0) ::MoveToEx(buffer, x, y(frame.graph[i]), nullptr);
            else ::LineTo(buffer, x, y(frame.graph[i]));
        }
        ::SelectObject(buffer, previousPen);
        ::DeleteObject(graph);
    }
    ::SelectObject(buffer, previousFont);
    if (bodyFont) ::DeleteObject(bodyFont);
    if (titleFont) ::DeleteObject(titleFont);
    ::BitBlt(screen, 0, 0, kWidth, kHeight, buffer, 0, 0, SRCCOPY);
    ::SelectObject(buffer, previousBitmap);
    ::DeleteObject(bitmap);
    ::DeleteDC(buffer);
}

} // namespace ff::engine
