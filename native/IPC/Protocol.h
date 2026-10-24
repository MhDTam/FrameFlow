#pragma once
#include "Core/Json.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

// Versioned UI <-> engine protocol.
// Transport: named pipe \\.\pipe\FrameFlow.Engine.v1, UTF-8 JSON, one message per line ('\n').
// Every message: { "v": 1, "type": "<MessageType>", "id"?: <int>, ...payload }
// Requests carrying "id" get a {"type":"Ack","id":...,"ok":bool,"error"?:...} response.
// The canonical description lives in shared/Protocols/ipc-protocol.md.
namespace ff::ipc::protocol {

constexpr int kVersion = 1;
constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\FrameFlow.Engine.v1";
constexpr std::size_t kMaxMessageBytes = 64 * 1024;

namespace type {
// UI -> engine
constexpr std::string_view Hello = "Hello";
constexpr std::string_view GetState = "GetState";
constexpr std::string_view EnablePacing = "EnablePacing";
constexpr std::string_view DisablePacing = "DisablePacing";
constexpr std::string_view SetFpsLimit = "SetFpsLimit";
constexpr std::string_view SetPacingMode = "SetPacingMode";
constexpr std::string_view SetLimitMethod = "SetLimitMethod";
constexpr std::string_view SetProtectedRisk = "SetProtectedRisk";
constexpr std::string_view SelectGame = "SelectGame";
constexpr std::string_view ClearManualGame = "ClearManualGame";
constexpr std::string_view SetLogLevel = "SetLogLevel";
constexpr std::string_view SetEngineSettings = "SetEngineSettings";
constexpr std::string_view Shutdown = "Shutdown";
// engine -> UI
constexpr std::string_view Ack = "Ack";
constexpr std::string_view EngineStatus = "EngineStatus";
constexpr std::string_view GameDetected = "GameDetected";
constexpr std::string_view GameClosed = "GameClosed";
constexpr std::string_view TelemetryUpdate = "TelemetryUpdate";
constexpr std::string_view Error = "Error";
} // namespace type

core::Json Make(std::string_view messageType);
core::Json MakeAck(std::int64_t id, bool ok, std::string_view error = {});
core::Json MakeError(std::string_view code, std::string_view message);

// Validation of untrusted input. All return nullopt when the value is out of range or malformed.
std::optional<double> ValidateFps(const core::Json& msg);
std::optional<std::wstring> ValidateExePath(const core::Json& msg);

} // namespace ff::ipc::protocol
