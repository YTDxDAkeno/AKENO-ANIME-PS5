// AKENO STREAM PS5 - Small bounded JSON document model.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Parses untrusted API responses with explicit depth and size limits and
// serialises the application's own settings. Numbers are kept as double;
// strings are UTF-8 (\u escapes, including surrogate pairs, are decoded).
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::json
{
enum class Type : std::uint8_t
{
    null,
    boolean,
    number,
    string,
    array,
    object,
};

class Value
{
  public:
    Value() = default;
    Value(std::nullptr_t)
    {
    }
    Value(bool b) : type_{Type::boolean}, bool_{b}
    {
    }
    Value(double n) : type_{Type::number}, number_{n}
    {
    }
    Value(int n) : type_{Type::number}, number_{static_cast<double>(n)}
    {
    }
    Value(long long n) : type_{Type::number}, number_{static_cast<double>(n)}
    {
    }
    Value(const char *s) : type_{Type::string}, string_{s}
    {
    }
    Value(std::string s) : type_{Type::string}, string_{std::move(s)}
    {
    }
    Value(std::string_view s) : type_{Type::string}, string_{s}
    {
    }

    static Value array();
    static Value object();

    [[nodiscard]] Type type() const noexcept
    {
        return type_;
    }
    [[nodiscard]] bool is_null() const noexcept
    {
        return type_ == Type::null;
    }
    [[nodiscard]] bool is_object() const noexcept
    {
        return type_ == Type::object;
    }
    [[nodiscard]] bool is_array() const noexcept
    {
        return type_ == Type::array;
    }
    [[nodiscard]] bool is_string() const noexcept
    {
        return type_ == Type::string;
    }
    [[nodiscard]] bool is_number() const noexcept
    {
        return type_ == Type::number;
    }
    [[nodiscard]] bool is_bool() const noexcept
    {
        return type_ == Type::boolean;
    }

    // Lenient accessors: a missing or differently typed value yields fallback.
    [[nodiscard]] const std::string &
    str(const std::string &fallback = empty_string()) const noexcept;
    [[nodiscard]] double num(double fallback = 0.0) const noexcept;
    [[nodiscard]] long long integer(long long fallback = 0) const noexcept;
    [[nodiscard]] bool boolean(bool fallback = false) const noexcept;

    // Object access. operator[] on a missing key returns a shared null value.
    [[nodiscard]] const Value &operator[](std::string_view key) const noexcept;
    [[nodiscard]] bool has(std::string_view key) const noexcept;
    Value &set(std::string key, Value value);
    [[nodiscard]] const std::map<std::string, Value, std::less<>> &members() const noexcept;

    // Array access.
    [[nodiscard]] const Value &operator[](std::size_t index) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept;
    Value &push(Value value);
    [[nodiscard]] const std::vector<Value> &items() const noexcept;

    // Path lookup such as "data.Page.media" (object keys only).
    [[nodiscard]] const Value &at_path(std::string_view dotted) const noexcept;

    [[nodiscard]] std::string dump(bool pretty = false) const;

    static const std::string &empty_string() noexcept;
    static const Value &null_value() noexcept;

  private:
    void dump_to(std::string &out, bool pretty, int indent) const;

    Type type_ = Type::null;
    bool bool_ = false;
    double number_ = 0.0;
    std::string string_;
    std::shared_ptr<std::vector<Value>> array_;
    std::shared_ptr<std::map<std::string, Value, std::less<>>> object_;
};

struct ParseLimits
{
    std::size_t max_bytes = 8u * 1024u * 1024u;
    int max_depth = 64;
};

struct ParseResult
{
    Value value;
    bool ok = false;
    std::string error;      // empty when ok
    std::size_t offset = 0; // byte offset of the error
};

ParseResult parse(std::string_view text, const ParseLimits &limits = {});

// Escapes for embedding in JSON (used by the writer and GraphQL requests).
std::string escape(std::string_view text);
} // namespace akeno::json
