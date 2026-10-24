#include "IPC/Protocol.h"
#include "Core/Strings.h"

#include <cmath>
#include <filesystem>

namespace ff::ipc::protocol {

core::Json Make(std::string_view messageType)
{
    auto j = core::Json::MakeObject();
    j.Set("v", kVersion);
    j.Set("type", messageType);
    return j;
}

core::Json MakeAck(std::int64_t id, bool ok, std::string_view error)
{
    auto j = Make(type::Ack);
    j.Set("id", id);
    j.Set("ok", ok);
    if (!error.empty()) j.Set("error", error);
    return j;
}

core::Json MakeError(std::string_view code, std::string_view message)
{
    auto j = Make(type::Error);
    j.Set("code", code);
    j.Set("message", message);
    return j;
}

std::optional<double> ValidateFps(const core::Json& msg)
{
    const core::Json* v = msg.Find("fps");
    if (!v || !v->IsNumber()) return std::nullopt;
    const double fps = v->AsNumber();
    if (!std::isfinite(fps) || fps < 10.0 || fps > 1000.0) return std::nullopt;
    // Three decimals are more than enough (59.940) and keep profiles readable.
    return std::round(fps * 1000.0) / 1000.0;
}

std::optional<std::wstring> ValidateExePath(const core::Json& msg)
{
    const core::Json* v = msg.Find("path");
    if (!v || !v->IsString()) return std::nullopt;
    const std::string& utf8 = v->AsString();
    if (utf8.empty() || utf8.size() > 1024) return std::nullopt;
    const std::filesystem::path p(core::Utf8ToWide(utf8));
    if (!p.is_absolute()) return std::nullopt;
    if (core::ToLowerAscii(p.extension().wstring()) != L".exe") return std::nullopt;
    // UNC/device paths are never games we manage.
    const std::wstring s = p.wstring();
    if (s.rfind(L"\\\\", 0) == 0) return std::nullopt;
    std::error_code ec;
    if (!std::filesystem::is_regular_file(p, ec)) return std::nullopt;
    return s;
}

} // namespace ff::ipc::protocol
