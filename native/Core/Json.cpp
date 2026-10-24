#include "Core/Json.h"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace ff::core {

namespace {

const Json& NullJson()
{
    static const Json null;
    return null;
}

const std::string& EmptyString()
{
    static const std::string empty;
    return empty;
}

void AppendEscaped(std::string& out, std::string_view s)
{
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (c < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            } else {
                out.push_back(static_cast<char>(c));
            }
        }
    }
    out.push_back('"');
}

void AppendUtf8(std::string& out, std::uint32_t cp)
{
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

class Parser {
public:
    Parser(std::string_view text, int maxDepth) : s_(text), maxDepth_(maxDepth) {}

    std::optional<Json> Run()
    {
        Json v;
        if (!ParseValue(v, 0)) return std::nullopt;
        SkipWs();
        if (pos_ != s_.size()) return std::nullopt;
        return v;
    }

private:
    void SkipWs()
    {
        while (pos_ < s_.size() && (s_[pos_] == ' ' || s_[pos_] == '\t' || s_[pos_] == '\n' || s_[pos_] == '\r')) ++pos_;
    }

    bool Consume(std::string_view lit)
    {
        if (s_.substr(pos_, lit.size()) != lit) return false;
        pos_ += lit.size();
        return true;
    }

    bool ParseValue(Json& out, int depth)
    {
        if (depth > maxDepth_) return false;
        SkipWs();
        if (pos_ >= s_.size()) return false;
        const char c = s_[pos_];
        if (c == '{') return ParseObject(out, depth);
        if (c == '[') return ParseArray(out, depth);
        if (c == '"') {
            std::string str;
            if (!ParseString(str)) return false;
            out = Json(std::move(str));
            return true;
        }
        if (Consume("true")) { out = Json(true); return true; }
        if (Consume("false")) { out = Json(false); return true; }
        if (Consume("null")) { out = Json(); return true; }
        return ParseNumber(out);
    }

    bool ParseNumber(Json& out)
    {
        const size_t start = pos_;
        if (pos_ < s_.size() && (s_[pos_] == '-' || s_[pos_] == '+')) ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') ++pos_;
            else break;
        }
        if (pos_ == start) return false;
        double v = 0.0;
        const char* first = s_.data() + start;
        const char* last = s_.data() + pos_;
        if (*first == '+') ++first;
        auto [ptr, ec] = std::from_chars(first, last, v);
        if (ec != std::errc() || ptr != last || !std::isfinite(v)) return false;
        out = Json(v);
        return true;
    }

    bool ParseHex4(std::uint32_t& cp)
    {
        if (pos_ + 4 > s_.size()) return false;
        cp = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = s_[pos_++];
            cp <<= 4;
            if (c >= '0' && c <= '9') cp |= static_cast<std::uint32_t>(c - '0');
            else if (c >= 'a' && c <= 'f') cp |= static_cast<std::uint32_t>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') cp |= static_cast<std::uint32_t>(c - 'A' + 10);
            else return false;
        }
        return true;
    }

    bool ParseString(std::string& out)
    {
        if (s_[pos_] != '"') return false;
        ++pos_;
        while (pos_ < s_.size()) {
            const char c = s_[pos_++];
            if (c == '"') return true;
            if (static_cast<unsigned char>(c) < 0x20) return false;
            if (c != '\\') { out.push_back(c); continue; }
            if (pos_ >= s_.size()) return false;
            const char e = s_[pos_++];
            switch (e) {
            case '"': out.push_back('"'); break;
            case '\\': out.push_back('\\'); break;
            case '/': out.push_back('/'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'u': {
                std::uint32_t cp = 0;
                if (!ParseHex4(cp)) return false;
                if (cp >= 0xD800 && cp <= 0xDBFF) {
                    std::uint32_t lo = 0;
                    if (!Consume("\\u") || !ParseHex4(lo) || lo < 0xDC00 || lo > 0xDFFF) return false;
                    cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                } else if (cp >= 0xDC00 && cp <= 0xDFFF) {
                    return false;
                }
                AppendUtf8(out, cp);
                break;
            }
            default: return false;
            }
        }
        return false;
    }

    bool ParseArray(Json& out, int depth)
    {
        ++pos_;
        out = Json::MakeArray();
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == ']') { ++pos_; return true; }
        while (true) {
            Json item;
            if (!ParseValue(item, depth + 1)) return false;
            out.Push(std::move(item));
            SkipWs();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == ']') { ++pos_; return true; }
            return false;
        }
    }

    bool ParseObject(Json& out, int depth)
    {
        ++pos_;
        out = Json::MakeObject();
        SkipWs();
        if (pos_ < s_.size() && s_[pos_] == '}') { ++pos_; return true; }
        while (true) {
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != '"') return false;
            std::string key;
            if (!ParseString(key)) return false;
            SkipWs();
            if (pos_ >= s_.size() || s_[pos_] != ':') return false;
            ++pos_;
            Json value;
            if (!ParseValue(value, depth + 1)) return false;
            out.Set(key, std::move(value));
            SkipWs();
            if (pos_ >= s_.size()) return false;
            if (s_[pos_] == ',') { ++pos_; continue; }
            if (s_[pos_] == '}') { ++pos_; return true; }
            return false;
        }
    }

    std::string_view s_;
    size_t pos_ = 0;
    int maxDepth_;
};

} // namespace

const std::string& Json::AsString() const { return IsString() ? str_ : EmptyString(); }

void Json::Push(Json v)
{
    if (type_ == Type::Null) type_ = Type::Array;
    if (type_ != Type::Array) return;
    items_.push_back(std::move(v));
}

Json& Json::Set(std::string_view key, Json value)
{
    if (type_ == Type::Null) type_ = Type::Object;
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) {
            items_[i] = std::move(value);
            return *this;
        }
    }
    keys_.emplace_back(key);
    items_.push_back(std::move(value));
    return *this;
}

const Json* Json::Find(std::string_view key) const
{
    if (type_ != Type::Object) return nullptr;
    for (size_t i = 0; i < keys_.size(); ++i) {
        if (keys_[i] == key) return &items_[i];
    }
    return nullptr;
}

const Json& Json::operator[](std::string_view key) const
{
    const Json* j = Find(key);
    return j ? *j : NullJson();
}

std::string Json::Dump(bool pretty) const
{
    std::string out;
    DumpTo(out, pretty, 0);
    return out;
}

void Json::DumpTo(std::string& out, bool pretty, int indent) const
{
    auto newline = [&](int level) {
        if (!pretty) return;
        out.push_back('\n');
        out.append(static_cast<size_t>(level) * 2, ' ');
    };

    switch (type_) {
    case Type::Null: out += "null"; break;
    case Type::Bool: out += bool_ ? "true" : "false"; break;
    case Type::Number: {
        if (std::isfinite(num_) && num_ == std::floor(num_) && std::fabs(num_) < 9.0e15) {
            out += std::to_string(static_cast<std::int64_t>(num_));
        } else if (std::isfinite(num_)) {
            char buf[40];
            auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), num_);
            out.append(buf, ec == std::errc() ? ptr : buf);
        } else {
            out += "null";
        }
        break;
    }
    case Type::String: AppendEscaped(out, str_); break;
    case Type::Array:
        out.push_back('[');
        for (size_t i = 0; i < items_.size(); ++i) {
            if (i) out.push_back(',');
            newline(indent + 1);
            items_[i].DumpTo(out, pretty, indent + 1);
        }
        if (!items_.empty()) newline(indent);
        out.push_back(']');
        break;
    case Type::Object:
        out.push_back('{');
        for (size_t i = 0; i < keys_.size(); ++i) {
            if (i) out.push_back(',');
            newline(indent + 1);
            AppendEscaped(out, keys_[i]);
            out += pretty ? ": " : ":";
            items_[i].DumpTo(out, pretty, indent + 1);
        }
        if (!keys_.empty()) newline(indent);
        out.push_back('}');
        break;
    }
}

std::optional<Json> Json::Parse(std::string_view text, int maxDepth)
{
    return Parser(text, maxDepth).Run();
}

} // namespace ff::core
