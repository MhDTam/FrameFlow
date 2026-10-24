#include "Core/Strings.h"

#include <windows.h>

#include <algorithm>

namespace ff::core {

std::string WideToUtf8(std::wstring_view wide)
{
    if (wide.empty()) return {};
    const int len = ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                                          nullptr, 0, nullptr, nullptr);
    if (len <= 0) return {};
    std::string out(static_cast<size_t>(len), '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                          out.data(), len, nullptr, nullptr);
    return out;
}

std::wstring Utf8ToWide(std::string_view utf8)
{
    if (utf8.empty()) return {};
    const int len = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    if (len <= 0) return {};
    std::wstring out(static_cast<size_t>(len), L'\0');
    ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), out.data(), len);
    return out;
}

std::string ToLowerAscii(std::string_view s)
{
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; });
    return out;
}

std::wstring ToLowerAscii(std::wstring_view s)
{
    std::wstring out(s);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](wchar_t c) { return (c >= L'A' && c <= L'Z') ? static_cast<wchar_t>(c - L'A' + L'a') : c; });
    return out;
}

bool IEquals(std::string_view a, std::string_view b)
{
    return a.size() == b.size() && ToLowerAscii(a) == ToLowerAscii(b);
}

bool IContains(std::string_view haystack, std::string_view needle)
{
    return ToLowerAscii(haystack).find(ToLowerAscii(needle)) != std::string::npos;
}

std::string SanitizeFileName(std::string_view s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                        c == '-' || c == '_' || c == '.';
        out.push_back(ok ? c : '_');
    }
    // Leading dots would create hidden/relative-looking names.
    while (!out.empty() && out.front() == '.') out.erase(out.begin());
    if (out.empty()) out = "unnamed";
    if (out.size() > 96) out.resize(96);
    return out;
}

} // namespace ff::core
