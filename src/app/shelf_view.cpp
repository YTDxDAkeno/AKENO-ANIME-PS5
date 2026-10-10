// AKENO STREAM PS5 - Rows of artwork cards with controller focus.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/shelf_view.hpp"

#include "app/store.hpp"

#include <algorithm>
#include <cmath>

namespace akeno
{
namespace th = ui::theme;

namespace
{
int card_w(bool portrait)
{
    return portrait ? th::kPortraitCardW : th::kLandscapeCardW;
}
int card_h(bool portrait)
{
    return portrait ? th::kPortraitCardH : th::kLandscapeCardH;
}
constexpr int kTitleBlock = 56; // row heading
constexpr int kTextBlock = 78;  // title + subtitle under the card
constexpr int kRowGap = 26;
} // namespace

int ShelfView::row_height(bool portrait)
{
    return kTitleBlock + card_h(portrait) + kTextBlock + kRowGap;
}

void ShelfView::set(std::vector<Shelf> shelves, bool keep_focus)
{
    // Drop empty rows; they cannot hold focus.
    shelves.erase(std::remove_if(shelves.begin(), shelves.end(),
                                 [](const Shelf &s) { return s.items.empty(); }),
                  shelves.end());
    const int old_row = row_;
    std::vector<int> old_columns = columns_;
    shelves_ = std::move(shelves);
    columns_.assign(shelves_.size(), 0);
    scroll_x_.assign(shelves_.size(), 0.0f);
    if (keep_focus)
    {
        for (std::size_t i = 0; i < shelves_.size() && i < old_columns.size(); ++i)
            columns_[i] =
                std::min<int>(old_columns[i], static_cast<int>(shelves_[i].items.size()) - 1);
        row_ = std::clamp(old_row, 0, std::max(0, static_cast<int>(shelves_.size()) - 1));
    }
    else
    {
        row_ = 0;
        scroll_y_ = target_y_ = 0.0f;
    }
}

int ShelfView::focused_column() const
{
    return row_ < static_cast<int>(columns_.size()) ? columns_[static_cast<std::size_t>(row_)] : 0;
}

const MediaItem *ShelfView::focused() const
{
    if (shelves_.empty() || row_ >= static_cast<int>(shelves_.size()))
        return nullptr;
    const auto &items = shelves_[static_cast<std::size_t>(row_)].items;
    const int c = focused_column();
    return c < static_cast<int>(items.size()) ? &items[static_cast<std::size_t>(c)] : nullptr;
}

void ShelfView::focus(int row, int column)
{
    if (shelves_.empty())
        return;
    row_ = std::clamp(row, 0, static_cast<int>(shelves_.size()) - 1);
    columns_[static_cast<std::size_t>(row_)] = std::clamp(
        column, 0, static_cast<int>(shelves_[static_cast<std::size_t>(row_)].items.size()) - 1);
}

bool ShelfView::handle(input::Button button)
{
    if (shelves_.empty())
        return false;
    const int rows = static_cast<int>(shelves_.size());
    auto &column = columns_[static_cast<std::size_t>(row_)];
    const int count = static_cast<int>(shelves_[static_cast<std::size_t>(row_)].items.size());
    switch (button)
    {
    case input::Button::left:
        if (column == 0)
            return false;
        --column;
        return true;
    case input::Button::right:
        if (column + 1 >= count)
            return false;
        ++column;
        return true;
    case input::Button::up:
        if (row_ == 0)
            return false;
        --row_;
        return true;
    case input::Button::down:
        if (row_ + 1 >= rows)
            return false;
        ++row_;
        return true;
    default:
        return false;
    }
}

bool ShelfView::animate(bool reduce_motion)
{
    bool moving = false;
    const auto approach = [&](float &value, float target)
    {
        if (reduce_motion || std::fabs(target - value) < 0.75f)
        {
            moving = moving || value != target;
            value = target;
            return;
        }
        value += (target - value) * 0.28f;
        moving = true;
    };
    approach(scroll_y_, target_y_);
    for (std::size_t r = 0; r < shelves_.size(); ++r)
    {
        const bool portrait = shelves_[r].portrait;
        const int step = card_w(portrait) + th::kCardGap;
        const int visible = std::max(1, (th::kWidth - 2 * th::kMarginX + th::kCardGap) / step);
        const int count = static_cast<int>(shelves_[r].items.size());
        // Keep the focused card one slot away from the right edge when possible.
        int first = 0;
        const int c = columns_[r];
        if (count > visible)
            first = std::clamp(c - (visible - 2), 0, count - visible);
        approach(scroll_x_[r], static_cast<float>(first * step));
    }
    return moving;
}

void ShelfView::render(ui::Painter &p, const ui::Rect &area, ui::ImageCache &images,
                       const Store *store, bool has_focus)
{
    // Vertical position: the focused row starts at the top of the area.
    int offset = 0;
    for (int r = 0; r < row_; ++r)
        offset += row_height(shelves_[static_cast<std::size_t>(r)].portrait);
    target_y_ = static_cast<float>(offset);

    // Clip vertically to the area; leave room for the focus ring on the left and let
    // the next card run off the right edge of the screen as a scroll cue.
    p.s.push_clip({area.x - 20, area.y, th::kWidth - area.x + 20, area.h});
    int y = area.y - static_cast<int>(scroll_y_);
    for (std::size_t r = 0; r < shelves_.size(); ++r)
    {
        const Shelf &shelf = shelves_[r];
        const int h = row_height(shelf.portrait);
        if (y + h < area.y)
        {
            y += h;
            continue;
        }
        if (y > area.bottom())
            break;
        const bool row_focused = has_focus && static_cast<int>(r) == row_;
        p.text(area.x, y + 6, shelf.title, th::kHeading,
               row_focused ? th::kText : th::kTextSecondary);
        const int cw = card_w(shelf.portrait), ch = card_h(shelf.portrait);
        const int step = cw + th::kCardGap;
        const int card_y = y + kTitleBlock;
        int x = area.x - static_cast<int>(scroll_x_[r]);
        for (std::size_t c = 0; c < shelf.items.size(); ++c, x += step)
        {
            if (x + cw < area.x - step)
                continue;
            if (x >= th::kWidth)
                break;
            const MediaItem &item = shelf.items[c];
            const bool icon = item.icon_art && !item.image_url.empty();
            const gfx::Image *art =
                item.image_url.empty() || icon ? nullptr : images.get(item.image_url, cw, ch);
            const gfx::Image *icon_image =
                icon ? images.get(item.image_url, ch * 2 / 5, ch * 2 / 5) : nullptr;
            double progress = 0.0;
            if (store && item.playable)
                for (const auto &e : store->history())
                    if (e.item.key() == item.key())
                    {
                        progress = e.finished() ? 0.0 : e.progress();
                        break;
                    }
            const bool focused = row_focused && static_cast<int>(c) == columns_[r];
            p.media_card({x, card_y, cw, ch}, item, art, focused, progress, true, icon_image);
        }
        y += h;
    }
    p.s.pop_clip();
}
} // namespace akeno
