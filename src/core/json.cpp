// AKENO STREAM PS5 - Small bounded JSON document model.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/json.hpp"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace akeno::json
{
namespace
{
void append_utf8(std::string &out, char32_t cp)
{
    if (cp < 0x80)
    {
        out += static_cast<char>(cp);
    }
    else if (cp < 0x800)
    {
        out += static_cast<char>(0xc0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
    else if (cp < 0x10000)
    {
        out += static_cast<char>(0xe0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
    else
    {
        out += static_cast<char>(0xf0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3f));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3f));
        out += static_cast<char>(0x80 | (cp & 0x3f));
    }
}

class Parser
{
  public:
    Parser(std::string_view text, const ParseLimits &limits) : text_{text}, limits_{limits}
    {
    }

    ParseResult run()
    {
        ParseResult result;
        if (text_.size() > limits_.max_bytes)
        {
            result.error = "document too large";
            return result;
        }
        skip_ws();
        if (!parse_value(result.value, 0))
        {
            result.error = error_;
            result.offset = pos_;
            result.value = Value{};
            return result;
        }
        skip_ws();
        if (pos_ != text_.size())
        {
            result.error = "trailing characters";
            result.offset = pos_;
            result.value = Value{};
            return result;
        }
        result.ok = true;
        return result;
    }

  private:
    bool fail(const char *message)
    {
        if (error_.empty())
            error_ = message;
        return false;
    }

    void skip_ws() noexcept
    {
        while (pos_ < text_.size() && (text_[pos_] == ' ' || text_[pos_] == '\t' ||
                                       text_[pos_] == '\n' || text_[pos_] == '\r'))
            ++pos_;
    }

    bool literal(std::string_view word)
    {
        if (text_.substr(pos_, word.size()) != word)
            return fail("invalid literal");
        pos_ += word.size();
        return true;
    }

    bool parse_value(Value &out, int depth)
    {
        if (depth > limits_.max_depth)
            return fail("nesting too deep");
        if (pos_ >= text_.size())
            return fail("unexpected end of input");
        switch (text_[pos_])
        {
        case '{':
            return parse_object(out, depth);
        case '[':
            return parse_array(out, depth);
        case '"':
        {
            std::string s;
            if (!parse_string(s))
                return false;
            out = Value{std::move(s)};
            return true;
        }
        case 't':
            out = Value{true};
            return literal("true");
        case 'f':
            out = Value{false};
            return literal("false");
        case 'n':
            out = Value{};
            return literal("null");
        default:
            return parse_number(out);
        }
    }

    bool parse_number(Value &out)
    {
        const std::size_t start = pos_;
        if (pos_ < text_.size() && text_[pos_] == '-')
            ++pos_;
        if (pos_ >= text_.size() || !(text_[pos_] >= '0' && text_[pos_] <= '9'))
            return fail("invalid number");
        if (text_[pos_] == '0')
            ++pos_;
        else
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
                ++pos_;
        if (pos_ < text_.size() && text_[pos_] == '.')
        {
            ++pos_;
            if (pos_ >= text_.size() || !(text_[pos_] >= '0' && text_[pos_] <= '9'))
                return fail("invalid fraction");
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
                ++pos_;
        }
        if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E'))
        {
            ++pos_;
            if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
                ++pos_;
            if (pos_ >= text_.size() || !(text_[pos_] >= '0' && text_[pos_] <= '9'))
                return fail("invalid exponent");
            while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
                ++pos_;
        }
        const std::size_t length = pos_ - start;
        if (length > 64)
            return fail("number too long");
        char buffer[65];
        text_.copy(buffer, length, start);
        buffer[length] = '\0';
        out = Value{std::strtod(buffer, nullptr)};
        return true;
    }

    bool hex4(unsigned &value)
    {
        if (pos_ + 4 > text_.size())
            return fail("truncated escape");
        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = text_[pos_++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<unsigned>(c - 'A' + 10);
            else
                return fail("invalid hex escape");
        }
        return true;
    }

    bool parse_string(std::string &out)
    {
        ++pos_; // opening quote
        while (true)
        {
            if (pos_ >= text_.size())
                return fail("unterminated string");
            const char c = text_[pos_++];
            if (c == '"')
                return true;
            if (static_cast<unsigned char>(c) < 0x20)
                return fail("control character in string");
            if (c != '\\')
            {
                out += c;
                continue;
            }
            if (pos_ >= text_.size())
                return fail("unterminated escape");
            const char e = text_[pos_++];
            switch (e)
            {
            case '"':
            case '\\':
            case '/':
                out += e;
                break;
            case 'b':
                out += '\b';
                break;
            case 'f':
                out += '\f';
                break;
            case 'n':
                out += '\n';
                break;
            case 'r':
                out += '\r';
                break;
            case 't':
                out += '\t';
                break;
            case 'u':
            {
                unsigned unit = 0;
                if (!hex4(unit))
                    return false;
                char32_t cp = unit;
                if (unit >= 0xd800 && unit <= 0xdbff)
                {
                    unsigned low = 0;
                    if (pos_ + 2 <= text_.size() && text_[pos_] == '\\' && text_[pos_ + 1] == 'u')
                    {
                        pos_ += 2;
                        if (!hex4(low))
                            return false;
                        if (low >= 0xdc00 && low <= 0xdfff)
                            cp = 0x10000 + ((unit - 0xd800) << 10) + (low - 0xdc00);
                        else
                            cp = 0xfffd;
                    }
                    else
                    {
                        cp = 0xfffd;
                    }
                }
                else if (unit >= 0xdc00 && unit <= 0xdfff)
                {
                    cp = 0xfffd;
                }
                append_utf8(out, cp);
                break;
            }
            default:
                return fail("invalid escape");
            }
        }
    }

    bool parse_array(Value &out, int depth)
    {
        ++pos_;
        out = Value::array();
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == ']')
        {
            ++pos_;
            return true;
        }
        while (true)
        {
            Value item;
            skip_ws();
            if (!parse_value(item, depth + 1))
                return false;
            out.push(std::move(item));
            skip_ws();
            if (pos_ >= text_.size())
                return fail("unterminated array");
            if (text_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (text_[pos_] == ']')
            {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool parse_object(Value &out, int depth)
    {
        ++pos_;
        out = Value::object();
        skip_ws();
        if (pos_ < text_.size() && text_[pos_] == '}')
        {
            ++pos_;
            return true;
        }
        while (true)
        {
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != '"')
                return fail("expected object key");
            std::string key;
            if (!parse_string(key))
                return false;
            skip_ws();
            if (pos_ >= text_.size() || text_[pos_] != ':')
                return fail("expected ':'");
            ++pos_;
            skip_ws();
            Value item;
            if (!parse_value(item, depth + 1))
                return false;
            out.set(std::move(key), std::move(item));
            skip_ws();
            if (pos_ >= text_.size())
                return fail("unterminated object");
            if (text_[pos_] == ',')
            {
                ++pos_;
                continue;
            }
            if (text_[pos_] == '}')
            {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    std::string_view text_;
    ParseLimits limits_;
    std::size_t pos_ = 0;
    std::string error_;
};
} // namespace

Value Value::array()
{
    Value v;
    v.type_ = Type::array;
    v.array_ = std::make_shared<std::vector<Value>>();
    return v;
}

Value Value::object()
{
    Value v;
    v.type_ = Type::object;
    v.object_ = std::make_shared<std::map<std::string, Value, std::less<>>>();
    return v;
}

const std::string &Value::empty_string() noexcept
{
    static const std::string empty;
    return empty;
}

const Value &Value::null_value() noexcept
{
    static const Value null;
    return null;
}

const std::string &Value::str(const std::string &fallback) const noexcept
{
    return type_ == Type::string ? string_ : fallback;
}

double Value::num(double fallback) const noexcept
{
    return type_ == Type::number ? number_ : fallback;
}

long long Value::integer(long long fallback) const noexcept
{
    if (type_ != Type::number || !std::isfinite(number_) || number_ > 9.0e18 || number_ < -9.0e18)
        return fallback;
    return static_cast<long long>(number_);
}

bool Value::boolean(bool fallback) const noexcept
{
    return type_ == Type::boolean ? bool_ : fallback;
}

const Value &Value::operator[](std::string_view key) const noexcept
{
    if (type_ != Type::object || !object_)
        return null_value();
    const auto found = object_->find(key);
    return found == object_->end() ? null_value() : found->second;
}

bool Value::has(std::string_view key) const noexcept
{
    return type_ == Type::object && object_ && object_->find(key) != object_->end();
}

Value &Value::set(std::string key, Value value)
{
    if (type_ != Type::object || !object_)
        *this = object();
    // Copy-on-write so copies of a document never alias each other's edits.
    if (object_.use_count() > 1)
        object_ = std::make_shared<std::map<std::string, Value, std::less<>>>(*object_);
    auto &slot = (*object_)[std::move(key)];
    slot = std::move(value);
    return slot;
}

const std::map<std::string, Value, std::less<>> &Value::members() const noexcept
{
    static const std::map<std::string, Value, std::less<>> empty;
    return type_ == Type::object && object_ ? *object_ : empty;
}

const Value &Value::operator[](std::size_t index) const noexcept
{
    if (type_ != Type::array || !array_ || index >= array_->size())
        return null_value();
    return (*array_)[index];
}

std::size_t Value::size() const noexcept
{
    if (type_ == Type::array && array_)
        return array_->size();
    if (type_ == Type::object && object_)
        return object_->size();
    return 0;
}

Value &Value::push(Value value)
{
    if (type_ != Type::array || !array_)
        *this = array();
    if (array_.use_count() > 1)
        array_ = std::make_shared<std::vector<Value>>(*array_);
    array_->push_back(std::move(value));
    return array_->back();
}

const std::vector<Value> &Value::items() const noexcept
{
    static const std::vector<Value> empty;
    return type_ == Type::array && array_ ? *array_ : empty;
}

const Value &Value::at_path(std::string_view dotted) const noexcept
{
    const Value *current = this;
    while (!dotted.empty())
    {
        const std::size_t dot = dotted.find('.');
        const std::string_view key = dotted.substr(0, dot);
        current = &(*current)[key];
        if (dot == std::string_view::npos)
            break;
        dotted.remove_prefix(dot + 1);
    }
    return *current;
}

std::string escape(std::string_view text)
{
    std::string out;
    out.reserve(text.size() + 8);
    for (const char c : text)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        default:
            if (static_cast<unsigned char>(c) < 0x20)
            {
                char buffer[8];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned>(c));
                out += buffer;
            }
            else
            {
                out += c;
            }
        }
    }
    return out;
}

void Value::dump_to(std::string &out, bool pretty, int indent) const
{
    const auto newline = [&](int level)
    {
        if (!pretty)
            return;
        out += '\n';
        out.append(static_cast<std::size_t>(level) * 2, ' ');
    };
    switch (type_)
    {
    case Type::null:
        out += "null";
        break;
    case Type::boolean:
        out += bool_ ? "true" : "false";
        break;
    case Type::number:
    {
        char buffer[40];
        if (!std::isfinite(number_))
            std::snprintf(buffer, sizeof(buffer), "null");
        else if (std::floor(number_) == number_ && std::fabs(number_) < 9.0e15)
            std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(number_));
        else
            std::snprintf(buffer, sizeof(buffer), "%.17g", number_);
        out += buffer;
        break;
    }
    case Type::string:
        out += '"';
        out += escape(string_);
        out += '"';
        break;
    case Type::array:
    {
        out += '[';
        bool first = true;
        for (const Value &item : items())
        {
            if (!first)
                out += ',';
            first = false;
            newline(indent + 1);
            item.dump_to(out, pretty, indent + 1);
        }
        if (!first)
            newline(indent);
        out += ']';
        break;
    }
    case Type::object:
    {
        out += '{';
        bool first = true;
        for (const auto &[key, item] : members())
        {
            if (!first)
                out += ',';
            first = false;
            newline(indent + 1);
            out += '"';
            out += escape(key);
            out += pretty ? "\": " : "\":";
            item.dump_to(out, pretty, indent + 1);
        }
        if (!first)
            newline(indent);
        out += '}';
        break;
    }
    }
}

std::string Value::dump(bool pretty) const
{
    std::string out;
    dump_to(out, pretty, 0);
    return out;
}

ParseResult parse(std::string_view text, const ParseLimits &limits)
{
    return Parser{text, limits}.run();
}
} // namespace akeno::json
