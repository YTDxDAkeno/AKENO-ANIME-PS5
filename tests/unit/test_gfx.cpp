// AKENO STREAM PS5 - Rasterizer, text, image and YUV conversion tests.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "gfx/font.hpp"
#include "gfx/image_decode.hpp"
#include "gfx/qr.hpp"
#include "gfx/surface.hpp"
#include "gfx/yuv.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>

using namespace akeno::gfx;

namespace
{
std::vector<std::uint8_t> read_asset(const char *relative)
{
    const char *root = std::getenv("AKENO_SOURCE_ROOT");
    std::ifstream in(std::string{root ? root : "."} + "/" + relative, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

struct Canvas
{
    std::vector<Pixel> pixels;
    Surface surface;
    Canvas(int w, int h)
        : pixels(static_cast<std::size_t>(w) * h, 0xff000000u), surface{pixels.data(), w, h, w}
    {
    }
    Pixel at(int x, int y) const
    {
        return pixels[static_cast<std::size_t>(y) * surface.width() + x];
    }
};

// One 8x8 NV12 picture of a single colour.
std::vector<std::uint8_t> solid_nv12(std::uint8_t y, std::uint8_t u, std::uint8_t v, unsigned w,
                                     unsigned h, unsigned pitch)
{
    std::vector<std::uint8_t> data(static_cast<std::size_t>(pitch) * (h + h / 2), 0);
    for (unsigned row = 0; row < h; ++row)
        for (unsigned col = 0; col < w; ++col)
            data[row * pitch + col] = y;
    for (unsigned row = 0; row < h / 2; ++row)
        for (unsigned col = 0; col < w; col += 2)
        {
            data[(h + row) * pitch + col] = u;
            data[(h + row) * pitch + col + 1] = v;
        }
    return data;
}
} // namespace

TEST(Surface, FillsClipsAndBlends)
{
    Canvas c(64, 32);
    c.surface.fill({-10, -10, 20, 20}, rgba(255, 0, 0));
    EXPECT_EQ(c.at(0, 0), rgba(255, 0, 0));
    EXPECT_EQ(c.at(9, 9), rgba(255, 0, 0));
    EXPECT_EQ(c.at(10, 10), 0xff000000u);
    c.surface.fill({20, 0, 10, 10}, rgba(255, 255, 255, 128));
    EXPECT_NEAR(red(c.at(25, 5)), 128, 1);
    c.surface.push_clip({40, 0, 4, 4});
    c.surface.fill({0, 0, 64, 32}, rgba(0, 0, 255));
    c.surface.pop_clip();
    EXPECT_EQ(c.at(41, 1), rgba(0, 0, 255));
    EXPECT_EQ(c.at(45, 1), 0xff000000u);
    // Out-of-range rectangles are harmless.
    c.surface.fill({1000, 1000, 50, 50}, rgba(1, 2, 3));
    c.surface.fill({0, 0, -5, 3}, rgba(1, 2, 3));
}

TEST(Surface, RoundedRectangleAntialiasesCorners)
{
    Canvas c(40, 40);
    c.surface.fill_rounded({0, 0, 40, 40}, 12, rgba(255, 255, 255));
    EXPECT_EQ(red(c.at(0, 0)), 0);     // outside the corner arc
    EXPECT_EQ(red(c.at(20, 20)), 255); // centre
    EXPECT_EQ(red(c.at(20, 0)), 255);  // top edge middle
    const unsigned edge = red(c.at(3, 3));
    EXPECT_GT(edge, 0u);
    EXPECT_LT(edge, 255u); // partial coverage on the arc
}

TEST(Surface, ImagesScaleAndCover)
{
    Image src;
    src.resize(4, 2, rgba(10, 20, 30));
    src.pixels[0] = rgba(250, 250, 250);
    const Image half = scale_image(src, 2, 1);
    ASSERT_TRUE(half.valid());
    EXPECT_GT(red(half.pixels[0]), red(half.pixels[1]));
    const Image up = scale_image(src, 8, 4);
    ASSERT_TRUE(up.valid());
    const Image cover = cover_image(src, 3, 3);
    ASSERT_TRUE(cover.valid());
    EXPECT_EQ(cover.width, 3);
    Canvas c(10, 10);
    c.surface.draw_image(up, 1, 1, 255, 2);
    c.surface.draw_image_scaled(src, {0, 0, 10, 5});
    EXPECT_EQ(c.at(9, 4), rgba(10, 20, 30));
}

TEST(Yuv, ConvertsLimitedRangeBt709)
{
    Image target;
    target.resize(8, 8);
    // Limited-range white, black and BT.709 red.
    const struct
    {
        std::uint8_t y, u, v;
        int r, g, b;
    } cases[] = {
        {235, 128, 128, 255, 255, 255}, {16, 128, 128, 0, 0, 0}, {63, 102, 240, 255, 0, 0}};
    for (const auto &k : cases)
    {
        const auto data = solid_nv12(k.y, k.u, k.v, 8, 8, 16);
        Nv12Picture p;
        p.data = data.data();
        p.bytes = data.size();
        p.pitch = 16;
        p.surface_height = 8;
        p.width = 8;
        p.height = 8;
        p.matrix = YuvMatrix::bt709;
        ASSERT_TRUE(convert_nv12(p, target));
        const Pixel px = target.pixels[9];
        EXPECT_NEAR(red(px), k.r, 3);
        EXPECT_NEAR(green(px), k.g, 3);
        EXPECT_NEAR(blue(px), k.b, 3);
        EXPECT_EQ(alpha(px), 255);
    }
}

TEST(Yuv, LetterboxesAndRejectsBadGeometry)
{
    const Rect r = fit_rect(1280, 720, 1920, 1080);
    EXPECT_EQ(r.w, 1920);
    EXPECT_EQ(r.h, 1080);
    const Rect wide = fit_rect(1920, 800, 1920, 1080);
    EXPECT_EQ(wide.w, 1920);
    EXPECT_EQ(wide.h, 800);
    EXPECT_EQ(wide.y, 140);
    const Rect four_three = fit_rect(640, 480, 1920, 1080);
    EXPECT_EQ(four_three.h, 1080);
    EXPECT_EQ(four_three.w, 1440);
    EXPECT_EQ(four_three.x, 240);

    Image target;
    target.resize(16, 9);
    const auto data = solid_nv12(235, 128, 128, 4, 4, 4);
    Nv12Picture p;
    p.data = data.data();
    p.bytes = data.size();
    p.pitch = 4;
    p.surface_height = 4;
    p.width = 4;
    p.height = 4;
    ASSERT_TRUE(convert_nv12(p, target));
    EXPECT_EQ(target.pixels[0], 0xff000000u);       // pillarbox
    EXPECT_GT(red(target.pixels[4 * 16 + 8]), 250); // picture
    p.bytes = data.size() - 1;
    EXPECT_FALSE(convert_nv12(p, target)); // truncated buffer
    p.bytes = data.size();
    p.pitch = 2;
    EXPECT_FALSE(convert_nv12(p, target)); // pitch below width
}

TEST(Yuv, Converts10BitLowAlignedWords)
{
    const unsigned w = 4, h = 4, pitch = 4;
    std::vector<std::uint16_t> data(pitch * (h + h / 2));
    for (unsigned i = 0; i < pitch * h; ++i)
        data[i] = 940; // 10-bit limited white
    for (unsigned i = pitch * h; i < data.size(); ++i)
        data[i] = 512;
    Image target;
    target.resize(4, 4);
    Nv12Picture p;
    p.data = reinterpret_cast<const std::uint8_t *>(data.data());
    p.bytes = data.size() * 2;
    p.pitch = pitch;
    p.surface_height = h;
    p.width = w;
    p.height = h;
    p.bit_depth = 10;
    ASSERT_TRUE(convert_nv12(p, target));
    EXPECT_GT(red(target.pixels[5]), 250);
}

TEST(Font, LoadsInterAndMeasures)
{
    FontEngine fonts;
    auto bytes = read_asset("assets/fonts/Inter-Regular.ttf");
    ASSERT_FALSE(bytes.empty()) << "set AKENO_SOURCE_ROOT";
    ASSERT_TRUE(fonts.load(Weight::regular, std::move(bytes)));
    ASSERT_TRUE(fonts.vector_fonts());
    TextStyle style;
    style.size = 32;
    const int a = fonts.measure("Akeno", style);
    const int b = fonts.measure("Akeno Stream", style);
    EXPECT_GT(a, 40);
    EXPECT_GT(b, a);
    EXPECT_EQ(fonts.measure("", style), 0);
    const std::string cut = fonts.ellipsize("A very long title that will not fit", style, 200);
    EXPECT_LE(fonts.measure(cut, style), 200);
    EXPECT_TRUE(cut.ends_with("..."));
    const auto lines =
        fonts.wrap("Browse anime-related content using supported catalog sources.", style, 300, 2);
    ASSERT_EQ(lines.size(), 2u);
    for (const auto &line : lines)
        EXPECT_LE(fonts.measure(line, style), 300);
    Canvas c(400, 60);
    const int drawn = fonts.draw(c.surface, 5, 5, "Hello", style);
    EXPECT_GT(drawn, 0);
    bool any = false;
    for (Pixel p : c.pixels)
        any = any || red(p) > 128;
    EXPECT_TRUE(any);
}

TEST(Font, FallsBackToPixelFontWithoutFaces)
{
    FontEngine fonts;
    EXPECT_FALSE(fonts.load(Weight::regular, {1, 2, 3, 4}));
    EXPECT_FALSE(fonts.vector_fonts());
    TextStyle style;
    Canvas c(200, 60);
    EXPECT_GT(fonts.draw(c.surface, 0, 0, "ERROR 42", style), 0);
}

TEST(Font, Utf8DecoderIsRobust)
{
    std::size_t pos = 0;
    const std::string s = "a\xC3\xA9\xE3\x81\x82\xF0\x9F\x8C\xB8\xFF\xC3";
    EXPECT_EQ(next_code_point(s, pos), U'a');
    EXPECT_EQ(next_code_point(s, pos), U'é');
    EXPECT_EQ(next_code_point(s, pos), U'あ');
    EXPECT_EQ(next_code_point(s, pos), U'\U0001F338');
    EXPECT_EQ(next_code_point(s, pos), U'�'); // 0xFF
    EXPECT_EQ(next_code_point(s, pos), U'�'); // truncated sequence
    EXPECT_EQ(pos, s.size());
    pos = 0;
    const std::string overlong = "\xC0\xAF";
    EXPECT_EQ(next_code_point(overlong, pos), U'�');
}

TEST(Image, DecodesPngAndRejectsGarbage)
{
    // 2x1 PNG: red then semi-transparent green.
    static const std::uint8_t png[] = {
        0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44,
        0x52, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0xf4,
        0x22, 0x7f, 0x8a, 0x00, 0x00, 0x00, 0x0f, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0xf8,
        0xcf, 0xc0, 0xf0, 0x1f, 0x08, 0x1b, 0x00, 0x10, 0x79, 0x03, 0x7e, 0x7d, 0x63, 0xce, 0xd7,
        0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
    std::string error;
    const Image img = decode_image(png, sizeof(png), &error);
    ASSERT_TRUE(img.valid()) << error;
    EXPECT_EQ(img.width, 2);
    EXPECT_EQ(img.height, 1);
    EXPECT_EQ(img.pixels[0], rgba(255, 0, 0, 255));
    EXPECT_EQ(img.pixels[1], rgba(0, 255, 0, 128));
    const std::uint8_t junk[] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12};
    EXPECT_FALSE(decode_image(junk, sizeof(junk), &error).valid());
    EXPECT_FALSE(error.empty());
    EXPECT_FALSE(decode_image(nullptr, 0).valid());
}

TEST(Qr, EncodesLinks)
{
    const Image qr = make_qr("https://www.youtube.com/watch?v=aqz-KE-bpKQ", 300);
    ASSERT_TRUE(qr.valid());
    EXPECT_EQ(qr.width, qr.height);
    EXPECT_LE(qr.width, 300);
    EXPECT_EQ(qr.pixels[0], rgba(255, 255, 255)); // quiet zone
    EXPECT_FALSE(make_qr("", 300).valid());
}
