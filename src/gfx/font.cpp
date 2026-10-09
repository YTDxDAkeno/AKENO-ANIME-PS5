// AKENO STREAM PS5 - Anti-aliased text rendering with FreeType.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/font.hpp"

#include <ft2build.h>
#include FT_FREETYPE_H

#include <array>
#include <cstdio>
#include <unordered_map>

namespace akeno::gfx
{
namespace
{
constexpr char32_t kReplacement = 0xfffd;
constexpr std::size_t kMaxCachedGlyphs = 6000;

// Minimal 5x7 pixel font used only if FreeType cannot load the bundled fonts,
// so an error screen is still readable.
struct PixelGlyph
{
    char c;
    std::array<std::uint8_t, 7> rows;
};
constexpr PixelGlyph kPixelGlyphs[] = {
    {' ', {0, 0, 0, 0, 0, 0, 0}},        {'.', {0, 0, 0, 0, 0, 12, 12}},
    {',', {0, 0, 0, 0, 12, 4, 8}},       {':', {0, 12, 12, 0, 12, 12, 0}},
    {'-', {0, 0, 0, 31, 0, 0, 0}},       {'_', {0, 0, 0, 0, 0, 0, 31}},
    {'%', {24, 25, 2, 4, 8, 19, 3}},     {'/', {1, 1, 2, 4, 8, 16, 16}},
    {'(', {2, 4, 8, 8, 8, 4, 2}},        {')', {8, 4, 2, 2, 2, 4, 8}},
    {'!', {4, 4, 4, 4, 4, 0, 4}},        {'?', {14, 17, 1, 2, 4, 0, 4}},
    {'\'', {4, 4, 0, 0, 0, 0, 0}},       {'"', {10, 10, 0, 0, 0, 0, 0}},
    {'+', {0, 4, 4, 31, 4, 4, 0}},       {'=', {0, 0, 31, 0, 31, 0, 0}},
    {'<', {2, 4, 8, 16, 8, 4, 2}},       {'>', {8, 4, 2, 1, 2, 4, 8}},
    {'0', {14, 17, 19, 21, 25, 17, 14}}, {'1', {4, 12, 4, 4, 4, 4, 14}},
    {'2', {14, 17, 1, 2, 4, 8, 31}},     {'3', {30, 1, 1, 14, 1, 1, 30}},
    {'4', {2, 6, 10, 18, 31, 2, 2}},     {'5', {31, 16, 16, 30, 1, 1, 30}},
    {'6', {14, 16, 16, 30, 17, 17, 14}}, {'7', {31, 1, 2, 4, 8, 8, 8}},
    {'8', {14, 17, 17, 14, 17, 17, 14}}, {'9', {14, 17, 17, 15, 1, 1, 14}},
    {'A', {14, 17, 17, 31, 17, 17, 17}}, {'B', {30, 17, 17, 30, 17, 17, 30}},
    {'C', {14, 17, 16, 16, 16, 17, 14}}, {'D', {30, 17, 17, 17, 17, 17, 30}},
    {'E', {31, 16, 16, 30, 16, 16, 31}}, {'F', {31, 16, 16, 30, 16, 16, 16}},
    {'G', {14, 17, 16, 23, 17, 17, 14}}, {'H', {17, 17, 17, 31, 17, 17, 17}},
    {'I', {31, 4, 4, 4, 4, 4, 31}},      {'J', {7, 2, 2, 2, 18, 18, 12}},
    {'K', {17, 18, 20, 24, 20, 18, 17}}, {'L', {16, 16, 16, 16, 16, 16, 31}},
    {'M', {17, 27, 21, 21, 17, 17, 17}}, {'N', {17, 25, 21, 19, 17, 17, 17}},
    {'O', {14, 17, 17, 17, 17, 17, 14}}, {'P', {30, 17, 17, 30, 16, 16, 16}},
    {'Q', {14, 17, 17, 17, 21, 18, 13}}, {'R', {30, 17, 17, 30, 20, 18, 17}},
    {'S', {15, 16, 16, 14, 1, 1, 30}},   {'T', {31, 4, 4, 4, 4, 4, 4}},
    {'U', {17, 17, 17, 17, 17, 17, 14}}, {'V', {17, 17, 17, 17, 17, 10, 4}},
    {'W', {17, 17, 17, 21, 21, 21, 10}}, {'X', {17, 17, 10, 4, 10, 17, 17}},
    {'Y', {17, 17, 10, 4, 4, 4, 4}},     {'Z', {31, 1, 2, 4, 8, 16, 31}},
};

const PixelGlyph *pixel_glyph(char32_t c) noexcept
{
    if (c >= 'a' && c <= 'z')
        c = c - 'a' + 'A';
    for (const auto &g : kPixelGlyphs)
        if (static_cast<char32_t>(static_cast<unsigned char>(g.c)) == c)
            return &g;
    return nullptr;
}

struct Glyph
{
    std::vector<std::uint8_t> bitmap;
    int width = 0, height = 0;
    int left = 0, top = 0; // offset from the pen position / baseline
    int advance = 0;       // in pixels, rounded
};

std::uint64_t glyph_key(int face, int size, char32_t c) noexcept
{
    return (static_cast<std::uint64_t>(face) << 56) |
           (static_cast<std::uint64_t>(size & 0xffff) << 32) | static_cast<std::uint64_t>(c);
}
} // namespace

char32_t next_code_point(std::string_view text, std::size_t &pos) noexcept
{
    const auto byte = [&](std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char b0 = byte(pos);
    if (b0 < 0x80)
    {
        ++pos;
        return b0;
    }
    int extra;
    char32_t cp;
    if ((b0 & 0xe0) == 0xc0)
    {
        extra = 1;
        cp = b0 & 0x1f;
    }
    else if ((b0 & 0xf0) == 0xe0)
    {
        extra = 2;
        cp = b0 & 0x0f;
    }
    else if ((b0 & 0xf8) == 0xf0)
    {
        extra = 3;
        cp = b0 & 0x07;
    }
    else
    {
        ++pos;
        return kReplacement;
    }
    // Continuation bytes occupy pos+1 .. pos+extra, all of which must exist.
    if (pos + static_cast<std::size_t>(extra) >= text.size())
    {
        ++pos;
        return kReplacement;
    }
    for (int i = 1; i <= extra; ++i)
    {
        const unsigned char b = byte(pos + i);
        if ((b & 0xc0) != 0x80)
        {
            ++pos;
            return kReplacement;
        }
        cp = (cp << 6) | (b & 0x3f);
    }
    static constexpr char32_t minimum[] = {0, 0x80, 0x800, 0x10000};
    if (cp < minimum[extra] || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
    {
        ++pos;
        return kReplacement;
    }
    pos += static_cast<std::size_t>(extra) + 1;
    return cp;
}

struct FontEngine::Impl
{
    FT_Library library = nullptr;
    // Faces 0..2: regular/semibold/bold, face 3: optional fallback.
    std::array<FT_Face, kWeightCount + 1> faces{};
    std::array<std::vector<std::uint8_t>, kWeightCount + 1> data{};
    std::array<int, kWeightCount + 1> face_size{};
    std::unordered_map<std::uint64_t, Glyph> cache;
    std::string error;

    Impl()
    {
        if (FT_Init_FreeType(&library) != 0)
        {
            library = nullptr;
            error = "FreeType initialisation failed";
        }
    }
    ~Impl()
    {
        for (FT_Face face : faces)
            if (face)
                FT_Done_Face(face);
        if (library)
            FT_Done_FreeType(library);
    }

    bool load_face(int index, std::vector<std::uint8_t> bytes)
    {
        if (!library || bytes.empty())
            return false;
        if (faces[index])
        {
            FT_Done_Face(faces[index]);
            faces[index] = nullptr;
        }
        data[index] = std::move(bytes);
        FT_Face face = nullptr;
        const FT_Error result = FT_New_Memory_Face(
            library, data[index].data(), static_cast<FT_Long>(data[index].size()), 0, &face);
        if (result != 0)
        {
            char message[64];
            std::snprintf(message, sizeof(message), "FreeType rejected font %d (error %d)", index,
                          static_cast<int>(result));
            error = message;
            data[index].clear();
            return false;
        }
        faces[index] = face;
        face_size[index] = 0;
        cache.clear();
        return true;
    }

    [[nodiscard]] FT_Face face_for(Weight weight) const noexcept
    {
        FT_Face face = faces[static_cast<int>(weight)];
        if (!face)
            face = faces[0];
        return face;
    }

    bool set_size(int index, int size)
    {
        if (face_size[index] == size)
            return true;
        if (FT_Set_Pixel_Sizes(faces[index], 0, static_cast<FT_UInt>(size)) != 0)
            return false;
        face_size[index] = size;
        return true;
    }

    const Glyph *glyph(Weight weight, int size, char32_t c)
    {
        int index = faces[static_cast<int>(weight)] ? static_cast<int>(weight) : 0;
        if (!faces[index])
            return nullptr;
        FT_UInt glyph_index = FT_Get_Char_Index(faces[index], c);
        if (glyph_index == 0 && faces[kWeightCount])
        {
            const FT_UInt fallback = FT_Get_Char_Index(faces[kWeightCount], c);
            if (fallback != 0)
            {
                index = kWeightCount;
                glyph_index = fallback;
            }
        }
        const std::uint64_t key = glyph_key(index, size, c);
        if (const auto found = cache.find(key); found != cache.end())
            return &found->second;
        if (cache.size() >= kMaxCachedGlyphs)
            cache.clear();
        if (!set_size(index, size))
            return nullptr;
        FT_Face face = faces[index];
        Glyph out;
        if (FT_Load_Glyph(face, glyph_index, FT_LOAD_TARGET_LIGHT) == 0 &&
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) == 0)
        {
            const FT_Bitmap &bitmap = face->glyph->bitmap;
            out.width = static_cast<int>(bitmap.width);
            out.height = static_cast<int>(bitmap.rows);
            out.left = face->glyph->bitmap_left;
            out.top = face->glyph->bitmap_top;
            out.advance = static_cast<int>((face->glyph->advance.x + 32) >> 6);
            out.bitmap.resize(static_cast<std::size_t>(out.width) * out.height);
            for (int y = 0; y < out.height; ++y)
            {
                const unsigned char *src =
                    bitmap.buffer + static_cast<std::ptrdiff_t>(y) * bitmap.pitch;
                std::copy(src, src + out.width,
                          out.bitmap.begin() + static_cast<std::ptrdiff_t>(y) * out.width);
            }
        }
        else
        {
            out.advance = size / 2;
        }
        return &cache.emplace(key, std::move(out)).first->second;
    }

    [[nodiscard]] bool vector() const noexcept
    {
        return library && faces[0];
    }
};

FontEngine::FontEngine() : impl_{std::make_unique<Impl>()}
{
}
FontEngine::~FontEngine() = default;

bool FontEngine::load(Weight weight, std::vector<std::uint8_t> font_bytes)
{
    return impl_->load_face(static_cast<int>(weight), std::move(font_bytes));
}

bool FontEngine::load_fallback(std::vector<std::uint8_t> font_bytes)
{
    return impl_->load_face(kWeightCount, std::move(font_bytes));
}

bool FontEngine::vector_fonts() const noexcept
{
    return impl_->vector();
}

std::string FontEngine::status() const
{
    if (impl_->vector())
        return impl_->faces[kWeightCount] ? "FreeType (Inter + fallback)" : "FreeType (Inter)";
    return impl_->error.empty() ? "Pixel fallback font" : "Pixel fallback: " + impl_->error;
}

int FontEngine::line_height(int size) const noexcept
{
    return impl_->vector() ? (size * 5 + 2) / 4 : (size * 9) / 7;
}

int FontEngine::ascent(int size) const noexcept
{
    return impl_->vector() ? (size * 31 + 16) / 32 : size;
}

int FontEngine::measure(std::string_view text, const TextStyle &style)
{
    int width = 0;
    std::size_t pos = 0;
    if (!impl_->vector())
    {
        const int scale = std::max(1, style.size / 7);
        while (pos < text.size())
        {
            (void)next_code_point(text, pos);
            width += 6 * scale;
        }
        return width;
    }
    while (pos < text.size())
    {
        const char32_t c = next_code_point(text, pos);
        if (const Glyph *g = impl_->glyph(style.weight, style.size, c))
            width += g->advance;
    }
    return width;
}

std::string FontEngine::ellipsize(std::string_view text, const TextStyle &style, int max_width)
{
    if (max_width <= 0 || measure(text, style) <= max_width)
        return std::string{text};
    const int dots = measure("...", style);
    std::string out;
    int width = 0;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        const std::size_t start = pos;
        const char32_t c = next_code_point(text, pos);
        int advance;
        if (impl_->vector())
        {
            const Glyph *g = impl_->glyph(style.weight, style.size, c);
            advance = g ? g->advance : 0;
        }
        else
        {
            advance = 6 * std::max(1, style.size / 7);
        }
        if (width + advance + dots > max_width)
            break;
        width += advance;
        out.append(text.substr(start, pos - start));
    }
    while (!out.empty() && out.back() == ' ')
        out.pop_back();
    out += "...";
    return out;
}

int FontEngine::draw(Surface &surface, int x, int y, std::string_view text, const TextStyle &style,
                     int max_width)
{
    std::string shortened;
    if (max_width > 0 && measure(text, style) > max_width)
    {
        shortened = ellipsize(text, style, max_width);
        text = shortened;
    }
    const int start_x = x;
    std::size_t pos = 0;
    if (!impl_->vector())
    {
        const int scale = std::max(1, style.size / 7);
        const int top = y + (line_height(style.size) - 7 * scale) / 2;
        while (pos < text.size())
        {
            const char32_t c = next_code_point(text, pos);
            if (const PixelGlyph *g = pixel_glyph(c))
                for (int row = 0; row < 7; ++row)
                    for (int col = 0; col < 5; ++col)
                        if (g->rows[row] & (1u << (4 - col)))
                            surface.fill({x + col * scale, top + row * scale, scale, scale},
                                         style.color);
            x += 6 * scale;
        }
        return x - start_x;
    }
    const int baseline = y + ascent(style.size) + (line_height(style.size) - style.size) / 2;
    while (pos < text.size())
    {
        const char32_t c = next_code_point(text, pos);
        const Glyph *g = impl_->glyph(style.weight, style.size, c);
        if (!g)
            continue;
        if (!g->bitmap.empty())
            surface.blend_mask(x + g->left, baseline - g->top, g->bitmap.data(), g->width,
                               g->height, g->width, style.color);
        x += g->advance;
    }
    return x - start_x;
}

std::vector<std::string> FontEngine::wrap(std::string_view text, const TextStyle &style, int width,
                                          int max_lines)
{
    std::vector<std::string> lines;
    if (width <= 0 || max_lines <= 0)
        return lines;
    std::string current;
    std::size_t pos = 0;
    bool truncated = false;
    while (pos < text.size())
    {
        // Explicit line breaks are honoured.
        if (text[pos] == '\n')
        {
            lines.push_back(current);
            current.clear();
            ++pos;
            if (static_cast<int>(lines.size()) == max_lines)
            {
                truncated = pos < text.size();
                break;
            }
            continue;
        }
        std::size_t end = pos;
        while (end < text.size() && text[end] != ' ' && text[end] != '\n')
            ++end;
        const std::string_view word = text.substr(pos, end - pos);
        std::string candidate =
            current.empty() ? std::string{word} : current + " " + std::string{word};
        if (measure(candidate, style) <= width || current.empty())
        {
            current = std::move(candidate);
        }
        else
        {
            lines.push_back(current);
            current = std::string{word};
            if (static_cast<int>(lines.size()) == max_lines)
            {
                truncated = true;
                current.clear();
                break;
            }
        }
        pos = end;
        while (pos < text.size() && text[pos] == ' ')
            ++pos;
    }
    if (!current.empty() && static_cast<int>(lines.size()) < max_lines)
        lines.push_back(current);
    for (auto &line : lines)
        if (measure(line, style) > width)
            line = ellipsize(line, style, width);
    if (truncated && !lines.empty())
        lines.back() = ellipsize(lines.back() + " ...", style, width);
    return lines;
}

int FontEngine::draw_wrapped(Surface &surface, int x, int y, std::string_view text,
                             const TextStyle &style, int width, int max_lines, int line_spacing)
{
    const auto lines = wrap(text, style, width, max_lines);
    const int step = line_height(style.size) + line_spacing;
    for (std::size_t i = 0; i < lines.size(); ++i)
        draw(surface, x, y + static_cast<int>(i) * step, lines[i], style);
    return static_cast<int>(lines.size()) * step;
}

void FontEngine::clear_cache()
{
    impl_->cache.clear();
}

std::size_t FontEngine::cached_glyphs() const noexcept
{
    return impl_->cache.size();
}
} // namespace akeno::gfx
