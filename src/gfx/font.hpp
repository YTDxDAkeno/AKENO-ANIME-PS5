// AKENO STREAM PS5 - Anti-aliased text rendering with FreeType.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "gfx/surface.hpp"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace akeno::gfx
{
enum class Weight : std::uint8_t
{
    regular = 0,
    semibold = 1,
    bold = 2,
};
inline constexpr int kWeightCount = 3;

// Decodes one UTF-8 code point at text[pos], advancing pos. Malformed input
// yields U+FFFD and advances by one byte, so arbitrary bytes are safe.
char32_t next_code_point(std::string_view text, std::size_t &pos) noexcept;

struct TextStyle
{
    int size = 28; // pixel height of the em square
    Weight weight = Weight::regular;
    Pixel color = 0xffffffffu;
};

class FontEngine final
{
  public:
    FontEngine();
    ~FontEngine();
    FontEngine(const FontEngine &) = delete;
    FontEngine &operator=(const FontEngine &) = delete;

    // Takes ownership of TrueType/OpenType bytes for one weight. Returns false
    // when FreeType rejects them; the engine then falls back to a pixel font.
    bool load(Weight weight, std::vector<std::uint8_t> font_bytes);
    // Optional fallback face for code points the primary font lacks (CJK etc.).
    bool load_fallback(std::vector<std::uint8_t> font_bytes);
    [[nodiscard]] bool vector_fonts() const noexcept;
    [[nodiscard]] std::string status() const;

    [[nodiscard]] int line_height(int size) const noexcept;
    [[nodiscard]] int ascent(int size) const noexcept;
    [[nodiscard]] int measure(std::string_view text, const TextStyle &style);
    // Draws with the top of the line box at y. If max_width > 0 the text is
    // truncated with an ellipsis to fit. Returns the drawn width.
    int draw(Surface &surface, int x, int y, std::string_view text, const TextStyle &style,
             int max_width = 0);
    // Breaks text at spaces into lines no wider than width. The last allowed
    // line ends with an ellipsis when the text does not fit in max_lines.
    std::vector<std::string> wrap(std::string_view text, const TextStyle &style, int width,
                                  int max_lines);
    int draw_wrapped(Surface &surface, int x, int y, std::string_view text, const TextStyle &style,
                     int width, int max_lines, int line_spacing = 0);
    // Truncates to max_width with "..." appended when needed.
    std::string ellipsize(std::string_view text, const TextStyle &style, int max_width);

    void clear_cache();
    [[nodiscard]] std::size_t cached_glyphs() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace akeno::gfx
