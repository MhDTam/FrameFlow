#pragma once
#include <string>
#include <string_view>

namespace ff::core {

std::string  WideToUtf8(std::wstring_view wide);
std::wstring Utf8ToWide(std::string_view utf8);

std::string  ToLowerAscii(std::string_view s);
std::wstring ToLowerAscii(std::wstring_view s);

// Case-insensitive ASCII comparison; executable names on Windows are case-insensitive.
bool IEquals(std::string_view a, std::string_view b);
bool IContains(std::string_view haystack, std::string_view needle);

// Maps an arbitrary string to something safe to use as a file name.
std::string SanitizeFileName(std::string_view s);

} // namespace ff::core
