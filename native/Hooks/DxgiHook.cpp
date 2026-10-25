// DX11/DX12 DXGI front-edge pacing. Loaded only by FrameFlow.Launch into an unprotected x64 game.
// Import hooks discover swap chains; their vtables are patched in this process only.
#include "Graphics/PresentPacer.h"
#include "Core/Log.h"
#include "Core/Strings.h"
#include "Timing/Clock.h"

#include <windows.h>
#include <psapi.h>
#include <d3d11.h>
#include <d3d12.h>
#include <dxgi1_2.h>

#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {

using CreateFactory = HRESULT(WINAPI*)(REFIID, void**);
using CreateFactory2 = HRESULT(WINAPI*)(UINT, REFIID, void**);
using CreateDeviceAndSwapChain = decltype(&::D3D11CreateDeviceAndSwapChain);
using CreateDevice = decltype(&::D3D11CreateDevice);
using Present = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain*, UINT, UINT);
using Present1 = HRESULT(STDMETHODCALLTYPE*)(IDXGISwapChain1*, UINT, UINT, const DXGI_PRESENT_PARAMETERS*);
using CreateSwapChain = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
using CreateSwapChainForHwnd = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, HWND,
    const DXGI_SWAP_CHAIN_DESC1*, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*, IDXGISwapChain1**);
using CreateSwapChainForCoreWindow = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*, IUnknown*,
    const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);
using CreateSwapChainForComposition = HRESULT(STDMETHODCALLTYPE*)(IDXGIFactory2*, IUnknown*,
    const DXGI_SWAP_CHAIN_DESC1*, IDXGIOutput*, IDXGISwapChain1**);

struct PatchedSlot { void** slot; void* original; };
std::mutex g_mutex;
std::vector<PatchedSlot> g_slots;
std::unordered_map<IDXGISwapChain*, const char*> g_chainApi;
std::shared_ptr<ff::graphics::PresentPacer> g_pacer;
std::mutex g_waitMutex;
IDXGISwapChain* g_primary = nullptr; // identity only: never dereferenced after a Present call
LARGE_INTEGER g_lastPrimary{};
LARGE_INTEGER g_frequency{};
std::atomic<bool> g_running{true};
std::atomic<unsigned> g_presentSamples{0};
std::atomic<bool> g_diagnostics{false};
thread_local unsigned g_presentDepth = 0;

struct PresentDepthScope {
    PresentDepthScope() { ++g_presentDepth; }
    ~PresentDepthScope() { --g_presentDepth; }
};

std::string DescribeAddress(const void* address)
{
    HMODULE module = nullptr;
    if (!::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(address), &module)) return "unknown";
    wchar_t path[MAX_PATH]{};
    if (!::GetModuleFileNameW(module, path, MAX_PATH)) return "unknown";
    const wchar_t* name = std::wcsrchr(path, L'\\');
    return std::format("{}+{:#x}", ff::core::WideToUtf8(name ? name + 1 : path),
        reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module));
}

void LogReentryStack()
{
    void* frames[16]{};
    const USHORT count = ::CaptureStackBackTrace(0, static_cast<DWORD>(std::size(frames)), frames, nullptr);
    for (USHORT i = 0; i < count; ++i)
        FF_LOG_INFO("Present reentry stack {}: {}", i, DescribeAddress(frames[i]));
}

template<class F> F Original(void** slot, F fallback = nullptr)
{
    std::scoped_lock lock(g_mutex);
    for (const auto& p : g_slots) if (p.slot == slot) return reinterpret_cast<F>(p.original);
    return fallback;
}

bool Patch(void** slot, void* replacement)
{
    std::scoped_lock lock(g_mutex);
    for (const auto& p : g_slots) if (p.slot == slot) return true;
    DWORD old = 0;
    if (!::VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) return false;
    void* previous = ::InterlockedExchangePointer(slot, replacement);
    DWORD ignored = 0;
    ::VirtualProtect(slot, sizeof(void*), old, &ignored);
    g_slots.push_back({slot, previous});
    return true;
}

std::shared_ptr<ff::graphics::PresentPacer> BeforePresent(IDXGISwapChain* chain,
                                                         std::int64_t hookEntryQpc)
{
    // A game can have UI/video swap chains. Pace only the first visible game window's chain;
    // switch if it stops presenting for two seconds (recreation or display change).
    DXGI_SWAP_CHAIN_DESC desc{};
    if (FAILED(chain->GetDesc(&desc)) || !desc.OutputWindow ||
        !::IsWindowVisible(desc.OutputWindow) || ::IsIconic(desc.OutputWindow)) return {};
    std::shared_ptr<ff::graphics::PresentPacer> selected;
    {
        std::scoped_lock lock(g_mutex);
        LARGE_INTEGER now{};
        ::QueryPerformanceCounter(&now);
        if (g_primary != chain && g_lastPrimary.QuadPart != 0 &&
            now.QuadPart - g_lastPrimary.QuadPart > 2 * g_frequency.QuadPart) {
            g_primary = nullptr;
            g_pacer.reset();
        }
        if (!g_primary) g_primary = chain;
        if (g_primary == chain) {
            g_lastPrimary = now;
            if (!g_pacer) {
                const auto it = g_chainApi.find(chain);
                const char* api = it == g_chainApi.end() ? "DXGI" : it->second;
                g_pacer = std::make_shared<ff::graphics::PresentPacer>(api);
                FF_LOG_INFO("{} swap chain selected; session {}", api, g_pacer->HasSession() ? "created" : "failed");
            }
            selected = g_pacer;
        }
    }
    if (selected) {
        std::scoped_lock lock(g_waitMutex);
        selected->BeforePresent(hookEntryQpc);
    }
    return selected;
}

HRESULT STDMETHODCALLTYPE HookPresent(IDXGISwapChain* self, UINT interval, UINT flags) noexcept
{
    const unsigned depth = g_presentDepth;
    PresentDepthScope depthScope;
    const auto hookEntry = ff::timing::Clock::Now();
    auto original = Original(reinterpret_cast<void***>(self)[0] + 8, static_cast<Present>(nullptr));
    if (!original) return E_FAIL;
    const bool diagnostics = g_diagnostics.load(std::memory_order_relaxed);
    const unsigned sample = diagnostics ? g_presentSamples.fetch_add(1, std::memory_order_relaxed) : 3;
    if (diagnostics && sample < 3) {
        try { FF_LOG_INFO("Present sample {}: depth {}, chain {}, original {} ({}), flags {}, interval {}",
                          sample, depth, static_cast<void*>(self), reinterpret_cast<void*>(original),
                          DescribeAddress(reinterpret_cast<void*>(original)), flags, interval);
              if (depth == 1) LogReentryStack(); }
        catch (...) {}
    }
    std::shared_ptr<ff::graphics::PresentPacer> paced;
    if (!(flags & DXGI_PRESENT_TEST)) {
        try { paced = BeforePresent(self, hookEntry); } catch (...) { /* fail open */ }
    }
    const HRESULT hr = original(self, interval, flags);
    if (diagnostics && sample < 3) {
        try { FF_LOG_INFO("Present sample {} returned HRESULT {:#x}", sample, static_cast<unsigned>(hr)); }
        catch (...) {}
    }
    if (paced) {
        try { std::scoped_lock lock(g_waitMutex); paced->AfterPresent(); } catch (...) { /* fail open */ }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE HookPresent1(IDXGISwapChain1* self, UINT interval, UINT flags,
    const DXGI_PRESENT_PARAMETERS* params) noexcept
{
    const unsigned depth = g_presentDepth;
    PresentDepthScope depthScope;
    const auto hookEntry = ff::timing::Clock::Now();
    auto original = Original(reinterpret_cast<void***>(self)[0] + 22, static_cast<Present1>(nullptr));
    if (!original) return E_FAIL;
    const bool diagnostics = g_diagnostics.load(std::memory_order_relaxed);
    const unsigned sample = diagnostics ? g_presentSamples.fetch_add(1, std::memory_order_relaxed) : 3;
    if (diagnostics && sample < 3) {
        try { FF_LOG_INFO("Present1 sample {}: depth {}, chain {}, original {} ({}), flags {}, interval {}",
                          sample, depth, static_cast<void*>(self), reinterpret_cast<void*>(original),
                          DescribeAddress(reinterpret_cast<void*>(original)), flags, interval);
              if (depth == 1) LogReentryStack(); }
        catch (...) {}
    }
    std::shared_ptr<ff::graphics::PresentPacer> paced;
    if (!(flags & DXGI_PRESENT_TEST)) {
        try { paced = BeforePresent(self, hookEntry); } catch (...) { /* fail open */ }
    }
    const HRESULT hr = original(self, interval, flags, params);
    if (diagnostics && sample < 3) {
        try { FF_LOG_INFO("Present1 sample {} returned HRESULT {:#x}", sample, static_cast<unsigned>(hr)); }
        catch (...) {}
    }
    if (paced) {
        try { std::scoped_lock lock(g_waitMutex); paced->AfterPresent(); } catch (...) { /* fail open */ }
    }
    return hr;
}

void PatchSwapChain(IDXGISwapChain* chain, const char* api)
{
    if (!chain) return;
    {
        std::scoped_lock lock(g_mutex);
        g_chainApi[chain] = api;
    }
    auto** table = *reinterpret_cast<void***>(chain);
    Patch(table + 8, reinterpret_cast<void*>(&HookPresent));
    IDXGISwapChain1* chain1 = nullptr;
    if (SUCCEEDED(chain->QueryInterface(__uuidof(IDXGISwapChain1), reinterpret_cast<void**>(&chain1)))) {
        auto** table1 = *reinterpret_cast<void***>(chain1);
        Patch(table1 + 22, reinterpret_cast<void*>(&HookPresent1));
        chain1->Release();
    }
}

const char* ApiForSwapChainDevice(IUnknown* device)
{
    if (!device) return "DXGI";
    ID3D12CommandQueue* queue = nullptr;
    if (SUCCEEDED(device->QueryInterface(__uuidof(ID3D12CommandQueue), reinterpret_cast<void**>(&queue)))) {
        queue->Release();
        return "DX12";
    }
    return "DX11";
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChain(IDXGIFactory* self, IUnknown* device,
    DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** out) noexcept
{
    auto original = Original((*reinterpret_cast<void***>(self)) + 10, static_cast<CreateSwapChain>(nullptr));
    if (!original) return E_FAIL;
    const HRESULT hr = original(self, device, desc, out);
    if (SUCCEEDED(hr) && out) PatchSwapChain(*out, ApiForSwapChainDevice(device));
    return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChainForHwnd(IDXGIFactory2* self, IUnknown* device, HWND hwnd,
    const DXGI_SWAP_CHAIN_DESC1* desc, const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* fullscreen,
    IDXGIOutput* output, IDXGISwapChain1** out) noexcept
{
    auto original = Original((*reinterpret_cast<void***>(self)) + 15, static_cast<CreateSwapChainForHwnd>(nullptr));
    if (!original) return E_FAIL;
    const HRESULT hr = original(self, device, hwnd, desc, fullscreen, output, out);
    if (SUCCEEDED(hr) && out) PatchSwapChain(*out, ApiForSwapChainDevice(device));
    return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChainForCoreWindow(IDXGIFactory2* self, IUnknown* device,
    IUnknown* window, const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output, IDXGISwapChain1** out) noexcept
{
    auto original = Original((*reinterpret_cast<void***>(self)) + 16, static_cast<CreateSwapChainForCoreWindow>(nullptr));
    if (!original) return E_FAIL;
    const HRESULT hr = original(self, device, window, desc, output, out);
    if (SUCCEEDED(hr) && out) PatchSwapChain(*out, ApiForSwapChainDevice(device));
    return hr;
}

HRESULT STDMETHODCALLTYPE HookCreateSwapChainForComposition(IDXGIFactory2* self, IUnknown* device,
    const DXGI_SWAP_CHAIN_DESC1* desc, IDXGIOutput* output, IDXGISwapChain1** out) noexcept
{
    auto original = Original((*reinterpret_cast<void***>(self)) + 24, static_cast<CreateSwapChainForComposition>(nullptr));
    if (!original) return E_FAIL;
    const HRESULT hr = original(self, device, desc, output, out);
    if (SUCCEEDED(hr) && out) PatchSwapChain(*out, ApiForSwapChainDevice(device));
    return hr;
}

void PatchFactory(IUnknown* unknown)
{
    if (!unknown) return;
    IDXGIFactory* base = nullptr;
    if (SUCCEEDED(unknown->QueryInterface(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&base)))) {
        auto** table = *reinterpret_cast<void***>(base);
        Patch(table + 10, reinterpret_cast<void*>(&HookCreateSwapChain));
        base->Release();
    }
    IDXGIFactory2* factory2 = nullptr;
    if (SUCCEEDED(unknown->QueryInterface(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory2)))) {
        auto** table = *reinterpret_cast<void***>(factory2);
        Patch(table + 15, reinterpret_cast<void*>(&HookCreateSwapChainForHwnd));
        Patch(table + 16, reinterpret_cast<void*>(&HookCreateSwapChainForCoreWindow));
        Patch(table + 24, reinterpret_cast<void*>(&HookCreateSwapChainForComposition));
        factory2->Release();
    }
}

void PatchDeviceFactory(ID3D11Device* device)
{
    if (!device) return;
    IDXGIDevice* dxgiDevice = nullptr;
    if (FAILED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice)))) return;
    IDXGIAdapter* adapter = nullptr;
    if (SUCCEEDED(dxgiDevice->GetAdapter(&adapter))) {
        IDXGIFactory* factory = nullptr;
        if (SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&factory)))) {
            PatchFactory(factory);
            factory->Release();
        }
        adapter->Release();
    }
    dxgiDevice->Release();
}

// When loaded after a game has already created its swap chain, factory imports alone are too late.
// A temporary DX11 swap chain exposes DXGI's shared Present vtable so existing chains can be paced.
// This is best effort: unusual/private swap-chain implementations may use a different vtable.
void PrimeExistingPresentVtable()
{
    if (!::GetModuleHandleW(L"d3d11.dll") || !::GetModuleHandleW(L"dxgi.dll")) return;
    HWND window = ::CreateWindowExW(0, L"STATIC", L"", WS_POPUP, 0, 0, 2, 2,
                                    nullptr, nullptr, ::GetModuleHandleW(nullptr), nullptr);
    if (!window) return;
    DXGI_SWAP_CHAIN_DESC desc{};
    desc.BufferDesc.Width = 2;
    desc.BufferDesc.Height = 2;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.OutputWindow = window;
    desc.Windowed = TRUE;
    desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    IDXGISwapChain* chain = nullptr;
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* context = nullptr;
    if (SUCCEEDED(::D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
                                                  nullptr, 0, D3D11_SDK_VERSION, &desc,
                                                  &chain, &device, nullptr, &context)) && chain) {
        PatchSwapChain(chain, "DX11");
        {
            std::scoped_lock lock(g_mutex);
            g_chainApi.erase(chain);
        }
    }
    if (chain) { chain->Release(); chain = nullptr; }
    if (device) {
        IDXGIDevice* dxgiDevice = nullptr;
        IDXGIAdapter* adapter = nullptr;
        IDXGIFactory2* factory = nullptr;
        IDXGISwapChain1* flipChain = nullptr;
        if (SUCCEEDED(device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice))) &&
            SUCCEEDED(dxgiDevice->GetAdapter(&adapter)) &&
            SUCCEEDED(adapter->GetParent(__uuidof(IDXGIFactory2), reinterpret_cast<void**>(&factory)))) {
            DXGI_SWAP_CHAIN_DESC1 flip{};
            flip.Width = 2;
            flip.Height = 2;
            flip.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            flip.SampleDesc.Count = 1;
            flip.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            flip.BufferCount = 2;
            flip.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            if (SUCCEEDED(factory->CreateSwapChainForHwnd(device, window, &flip, nullptr, nullptr, &flipChain))) {
                PatchSwapChain(flipChain, "DX11");
                std::scoped_lock lock(g_mutex);
                g_chainApi.erase(flipChain);
            }
        }
        if (flipChain) flipChain->Release();
        if (factory) factory->Release();
        if (adapter) adapter->Release();
        if (dxgiDevice) dxgiDevice->Release();
    }
    if (context) context->Release();
    if (device) device->Release();
    ::DestroyWindow(window);
}

HRESULT WINAPI HookCreateDXGIFactory(REFIID iid, void** out)
{
    const auto original = reinterpret_cast<CreateFactory>(::GetProcAddress(::GetModuleHandleW(L"dxgi.dll"), "CreateDXGIFactory"));
    const HRESULT hr = original(iid, out);
    if (SUCCEEDED(hr) && out) PatchFactory(static_cast<IUnknown*>(*out));
    return hr;
}
HRESULT WINAPI HookCreateDXGIFactory1(REFIID iid, void** out)
{
    const auto original = reinterpret_cast<CreateFactory>(::GetProcAddress(::GetModuleHandleW(L"dxgi.dll"), "CreateDXGIFactory1"));
    const HRESULT hr = original(iid, out);
    if (SUCCEEDED(hr) && out) PatchFactory(static_cast<IUnknown*>(*out));
    return hr;
}
HRESULT WINAPI HookCreateDXGIFactory2(UINT flags, REFIID iid, void** out)
{
    const auto original = reinterpret_cast<CreateFactory2>(::GetProcAddress(::GetModuleHandleW(L"dxgi.dll"), "CreateDXGIFactory2"));
    const HRESULT hr = original(flags, iid, out);
    if (SUCCEEDED(hr) && out) PatchFactory(static_cast<IUnknown*>(*out));
    return hr;
}

HRESULT WINAPI HookD3D11CreateDeviceAndSwapChain(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver,
    HMODULE software, UINT flags, const D3D_FEATURE_LEVEL* levels, UINT levelCount, UINT sdk,
    const DXGI_SWAP_CHAIN_DESC* desc, IDXGISwapChain** chain, ID3D11Device** device,
    D3D_FEATURE_LEVEL* chosen, ID3D11DeviceContext** context)
{
    const auto original = reinterpret_cast<CreateDeviceAndSwapChain>(
        ::GetProcAddress(::GetModuleHandleW(L"d3d11.dll"), "D3D11CreateDeviceAndSwapChain"));
    const HRESULT hr = original(adapter, driver, software, flags, levels, levelCount, sdk,
        desc, chain, device, chosen, context);
    if (SUCCEEDED(hr)) {
        if (device) PatchDeviceFactory(*device);
        if (chain) PatchSwapChain(*chain, "DX11");
    }
    return hr;
}

HRESULT WINAPI HookD3D11CreateDevice(IDXGIAdapter* adapter, D3D_DRIVER_TYPE driver,
    HMODULE software, UINT flags, const D3D_FEATURE_LEVEL* levels, UINT levelCount, UINT sdk,
    ID3D11Device** device, D3D_FEATURE_LEVEL* chosen, ID3D11DeviceContext** context)
{
    FF_LOG_INFO("D3D11CreateDevice intercepted");
    const auto original = reinterpret_cast<CreateDevice>(
        ::GetProcAddress(::GetModuleHandleW(L"d3d11.dll"), "D3D11CreateDevice"));
    const HRESULT hr = original(adapter, driver, software, flags, levels, levelCount, sdk,
        device, chosen, context);
    if (SUCCEEDED(hr) && device) PatchDeviceFactory(*device);
    return hr;
}

void PatchImports(HMODULE module)
{
    auto* base = reinterpret_cast<std::byte*>(module);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    const auto dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress) return;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        if (!imp->OriginalFirstThunk || !imp->FirstThunk) continue;
        const char* dll = reinterpret_cast<char*>(base + imp->Name);
        const bool dxgi = _stricmp(dll, "dxgi.dll") == 0;
        const bool d3d11 = _stricmp(dll, "d3d11.dll") == 0;
        if (!dxgi && !d3d11) continue;
        auto* names = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->OriginalFirstThunk);
        auto* slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + imp->FirstThunk);
        for (; names->u1.AddressOfData; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) continue;
            const char* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData)->Name;
            void* hook = nullptr;
            if (dxgi && std::strcmp(name, "CreateDXGIFactory") == 0) hook = reinterpret_cast<void*>(&HookCreateDXGIFactory);
            if (dxgi && std::strcmp(name, "CreateDXGIFactory1") == 0) hook = reinterpret_cast<void*>(&HookCreateDXGIFactory1);
            if (dxgi && std::strcmp(name, "CreateDXGIFactory2") == 0) hook = reinterpret_cast<void*>(&HookCreateDXGIFactory2);
            if (d3d11 && std::strcmp(name, "D3D11CreateDeviceAndSwapChain") == 0)
                hook = reinterpret_cast<void*>(&HookD3D11CreateDeviceAndSwapChain);
            if (d3d11 && std::strcmp(name, "D3D11CreateDevice") == 0)
                hook = reinterpret_cast<void*>(&HookD3D11CreateDevice);
            if (hook) Patch(reinterpret_cast<void**>(&slots->u1.Function), hook);
        }
    }
}

DWORD WINAPI Worker(void* self)
{
    ff::core::Log::Initialize(L"hook");
    ::QueryPerformanceFrequency(&g_frequency);
    const std::wstring diagnosticsName = L"Local\\FrameFlow.HookDiagnostics." +
        std::to_wstring(::GetCurrentProcessId());
    HANDLE diagnostics = ::OpenEventW(SYNCHRONIZE, FALSE, diagnosticsName.c_str());
    g_diagnostics = diagnostics != nullptr;
    if (diagnostics) ::CloseHandle(diagnostics);
    // Keep this module loaded for the lifetime of the game: patched import/vtable entries point here.
    HMODULE pinned = nullptr;
    ::GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(self), &pinned);
    const std::wstring readyName = L"Local\\FrameFlow.HookReady." + std::to_wstring(::GetCurrentProcessId());
    HANDLE ready = ::CreateEventW(nullptr, TRUE, FALSE, readyName.c_str());
    bool firstScan = true;
    while (g_running.load()) {
        HMODULE modules[1024]{};
        DWORD bytes = 0;
        if (::EnumProcessModules(::GetCurrentProcess(), modules, sizeof(modules), &bytes)) {
            const DWORD count = (bytes < sizeof(modules) ? bytes : sizeof(modules)) / sizeof(HMODULE);
            for (DWORD i = 0; i < count; ++i) if (modules[i] != pinned) PatchImports(modules[i]);
        }
        if (firstScan) {
            PrimeExistingPresentVtable();
            FF_LOG_INFO("First import scan complete: {} patched slots", g_slots.size());
            if (ready) ::SetEvent(ready);
            firstScan = false;
        }
        ::Sleep(250);
    }
    if (ready) ::CloseHandle(ready);
    return 0;
}
} // namespace

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH) {
        ::DisableThreadLibraryCalls(self);
        HANDLE thread = ::CreateThread(nullptr, 0, Worker, self, 0, nullptr);
        if (thread) ::CloseHandle(thread);
    } else if (reason == DLL_PROCESS_DETACH) {
        g_running.store(false);
    }
    return TRUE;
}
