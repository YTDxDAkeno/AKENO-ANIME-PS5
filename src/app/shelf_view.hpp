// AKENO STREAM PS5 - Rows of artwork cards with controller focus.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "app/input.hpp"
#include "providers/model.hpp"
#include "ui/image_cache.hpp"
#include "ui/painter.hpp"

#include <functional>
#include <vector>

namespace akeno
{
class Store;

class ShelfView final
{
  public:
    void set(std::vector<Shelf> shelves, bool keep_focus = false);
    [[nodiscard]] const std::vector<Shelf> &shelves() const noexcept
    {
        return shelves_;
    }
    [[nodiscard]] bool empty() const noexcept
    {
        return shelves_.empty();
    }
    // Moves focus; returns false when the move leaves the view (e.g. up from the first row).
    bool handle(input::Button button);
    [[nodiscard]] const MediaItem *focused() const;
    [[nodiscard]] int focused_row() const noexcept
    {
        return row_;
    }
    [[nodiscard]] int focused_column() const;
    void focus(int row, int column);
    // Advances scroll animation; returns true while moving.
    bool animate(bool reduce_motion);
    void render(ui::Painter &p, const ui::Rect &area, ui::ImageCache &images, const Store *store,
                bool has_focus);
    // Height of one row for a given orientation (title + card + text + gap).
    static int row_height(bool portrait);

  private:
    std::vector<Shelf> shelves_;
    std::vector<int> columns_; // focused column per row (remembered)
    std::vector<float> scroll_x_;
    int row_ = 0;
    float scroll_y_ = 0.0f;
    float target_y_ = 0.0f;
};
} // namespace akeno
