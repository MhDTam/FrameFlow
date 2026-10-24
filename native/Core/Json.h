#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ff::core {

// Small self-contained JSON value used for profiles, settings and IPC messages.
// Deliberately minimal: no dependencies, bounded parse depth, insertion-ordered objects.
class Json {
public:
    enum class Type : std::uint8_t { Null, Bool, Number, String, Array, Object };

    Json() = default;
    Json(std::nullptr_t) {}
    Json(bool b) : type_(Type::Bool), bool_(b) {}
    Json(int v) : type_(Type::Number), num_(v) {}
    Json(std::int64_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(std::uint32_t v) : type_(Type::Number), num_(v) {}
    Json(std::uint64_t v) : type_(Type::Number), num_(static_cast<double>(v)) {}
    Json(double v) : type_(Type::Number), num_(v) {}
    Json(const char* s) : type_(Type::String), str_(s) {}
    Json(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Json(std::string_view s) : type_(Type::String), str_(s) {}

    static Json MakeArray() { Json j; j.type_ = Type::Array; return j; }
    static Json MakeObject() { Json j; j.type_ = Type::Object; return j; }

    Type type() const { return type_; }
    bool IsNull() const { return type_ == Type::Null; }
    bool IsBool() const { return type_ == Type::Bool; }
    bool IsNumber() const { return type_ == Type::Number; }
    bool IsString() const { return type_ == Type::String; }
    bool IsArray() const { return type_ == Type::Array; }
    bool IsObject() const { return type_ == Type::Object; }

    bool AsBool(bool fallback = false) const { return IsBool() ? bool_ : fallback; }
    double AsNumber(double fallback = 0.0) const { return IsNumber() ? num_ : fallback; }
    std::int64_t AsInt(std::int64_t fallback = 0) const { return IsNumber() ? static_cast<std::int64_t>(num_) : fallback; }
    const std::string& AsString() const;
    std::string AsString(std::string_view fallback) const { return IsString() ? str_ : std::string(fallback); }

    // Array access
    const std::vector<Json>& Items() const { return items_; }
    void Push(Json v);
    size_t Size() const { return IsArray() ? items_.size() : (IsObject() ? keys_.size() : 0); }

    // Object access. Set() converts a Null value into an object.
    Json& Set(std::string_view key, Json value);
    const Json* Find(std::string_view key) const;
    const Json& operator[](std::string_view key) const;
    bool Has(std::string_view key) const { return Find(key) != nullptr; }
    const std::vector<std::string>& Keys() const { return keys_; }

    std::string Dump(bool pretty = false) const;

    // Returns nullopt on malformed input. maxDepth guards against hostile nesting.
    static std::optional<Json> Parse(std::string_view text, int maxDepth = 64);

private:
    void DumpTo(std::string& out, bool pretty, int indent) const;

    Type type_ = Type::Null;
    bool bool_ = false;
    double num_ = 0.0;
    std::string str_;
    std::vector<Json> items_;          // array items, or object values (parallel to keys_)
    std::vector<std::string> keys_;    // object keys
};

} // namespace ff::core
