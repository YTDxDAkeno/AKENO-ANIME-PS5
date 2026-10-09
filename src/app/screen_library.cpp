// AKENO STREAM PS5 - Local media library (file browser).
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "app/screens.hpp"
#include "core/fs.hpp"
#include "media/remux.hpp"
#include "platform/platform.hpp"

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

namespace akeno
{
namespace th = ui::theme;
using ui::Glyph;
using ui::Icon;
using ui::Pixel;
using ui::Rect;

namespace
{
struct Root
{
    std::string label;
    std::string path;
    Icon icon;
    std::string status; // filled by probe
    bool available = false;
};

std::string human_size(std::uint64_t bytes)
{
    char text[32];
    if (bytes >= (1ull << 30))
        std::snprintf(text, sizeof(text), "%.2f GB", bytes / static_cast<double>(1ull << 30));
    else if (bytes >= (1ull << 20))
        std::snprintf(text, sizeof(text), "%.1f MB", bytes / static_cast<double>(1ull << 20));
    else if (bytes >= 1024)
        std::snprintf(text, sizeof(text), "%.0f KB", bytes / 1024.0);
    else
        std::snprintf(text, sizeof(text), "%llu B", static_cast<unsigned long long>(bytes));
    return text;
}

class LibraryScreen final : public Screen
{
  public:
    explicit LibraryScreen(App &app) : Screen{app}
    {
        const std::string data = platform::data_dir();
        media_dir_ = fs::join(data, "media");
        (void)fs::make_directory(media_dir_);
        roots_.push_back(
            {"Bundled test clips", fs::join(platform::app_dir(), "assets/selftest"), Icon::film});
        roots_.push_back({"AKENO media folder", media_dir_, Icon::folder});
        for (int i = 0; i < 8; ++i)
            roots_.push_back(
                {"USB drive " + std::to_string(i + 1), "/mnt/usb" + std::to_string(i), Icon::usb});
        roots_.push_back({"Extended storage 1", "/mnt/ext0", Icon::usb});
        roots_.push_back({"Extended storage 2", "/mnt/ext1", Icon::usb});
        roots_.push_back({"Console storage /data", "/data", Icon::folder});
        probe();
    }

    void resumed() override
    {
        probe();
    }

    void handle(input::Button b) override
    {
        if (pane_ == 0)
        {
            switch (b)
            {
            case input::Button::up:
                root_ = std::max(0, root_ - 1);
                return;
            case input::Button::down:
                root_ = std::min(static_cast<int>(roots_.size()) - 1, root_ + 1);
                return;
            case input::Button::right:
            case input::Button::cross:
                open_root();
                return;
            case input::Button::triangle:
                probe();
                app_.toast("Storage rescanned", th::kInfo);
                return;
            default:
                return;
            }
        }
        switch (b)
        {
        case input::Button::up:
            entry_ = std::max(0, entry_ - 1);
            return;
        case input::Button::down:
            entry_ = std::min(static_cast<int>(entries_.size()) - 1, entry_ + 1);
            return;
        case input::Button::left:
            pane_ = 0;
            return;
        case input::Button::cross:
            activate();
            return;
        case input::Button::circle:
            if (path_ == roots_[static_cast<std::size_t>(root_)].path || path_.size() <= 1)
                pane_ = 0;
            else
                navigate(fs::parent(path_));
            return;
        case input::Button::triangle:
            navigate(path_);
            return;
        case input::Button::square:
            if (const auto item = focused_item())
            {
                const bool fav = app_.store().toggle_favorite(*item);
                app_.toast(fav ? "Added to Favorites" : "Removed from Favorites",
                           fav ? th::kSuccess : th::kInfo);
            }
            return;
        default:
            return;
        }
    }

    void render(ui::Painter &p, std::uint64_t) override
    {
        p.text(th::kMarginX, 160, "My Library", th::kTitle, th::kText);
        p.text(th::kMarginX, 222, "Play MP4, MKV, MOV and MPEG-TS files with H.264 or HEVC video.",
               th::kBody, th::kTextSecondary);

        // Roots pane.
        const Rect left{th::kMarginX, 290, 560, 700};
        p.panel(left, th::kPanelRadius, gfx::with_alpha(th::kSurface, 220));
        int y = left.y + 16;
        const int visible_roots = 9;
        const int first_root =
            std::clamp(root_ - visible_roots / 2, 0,
                       std::max(0, static_cast<int>(roots_.size()) - visible_roots));
        for (int i = first_root;
             i < static_cast<int>(roots_.size()) && i < first_root + visible_roots; ++i)
        {
            const Root &r = roots_[static_cast<std::size_t>(i)];
            const Rect row{left.x + 12, y, left.w - 24, 72};
            const bool focused = pane_ == 0 && i == root_;
            if (focused)
                p.s.fill_rounded(row, 16, th::kText);
            else if (i == root_)
                p.s.fill_rounded(row, 16, th::kSurfaceRaised);
            const Pixel fg =
                focused ? th::kTextOnAccent : (r.available ? th::kText : th::kTextMuted);
            p.icon(r.icon, row.x + 40, row.y + 36, 32, fg);
            p.text(row.x + 80, row.y + 8, r.label, th::kBodyStrong, fg, row.w - 100);
            p.text(row.x + 80, row.y + 40, r.status, th::kSmall,
                   focused ? th::kTextOnAccent : th::kTextMuted, row.w - 100);
            y += 76;
        }

        // Files pane.
        const Rect right{left.right() + 32, 290, th::kWidth - th::kMarginX - left.right() - 32,
                         700};
        p.panel(right, th::kPanelRadius, gfx::with_alpha(th::kSurface, 220));
        if (path_.empty())
        {
            p.text(right.x + 40, right.y + 40, "Select a storage location", th::kHeading,
                   th::kTextSecondary);
            p.wrapped(right.x + 40, right.y + 100,
                      "Copy your own, legally obtained video files to the AKENO media folder or a "
                      "USB drive. "
                      "Encrypted (DRM) files cannot be played. Folders the title sandbox may not "
                      "read are "
                      "marked accordingly.",
                      th::kBody, th::kTextMuted, right.w - 80, 4);
            p.text(right.x + 40, right.y + 290, "AKENO media folder: " + media_dir_, th::kCaption,
                   th::kTextMuted, right.w - 80);
            return;
        }
        p.text(right.x + 32, right.y + 18, path_, th::kCaptionStrong, th::kTextSecondary,
               right.w - 64);
        if (!error_.empty())
        {
            p.icon(Icon::lock, right.x + 60, right.y + 110, 40, th::kWarning);
            p.wrapped(right.x + 100, right.y + 88, error_, th::kBody, th::kTextSecondary,
                      right.w - 140, 4);
            return;
        }
        if (entries_.empty())
        {
            p.text(right.x + 32, right.y + 80, "This folder is empty.", th::kBody, th::kTextMuted);
            return;
        }
        const int row_h = 74;
        const int visible = (right.h - 70) / row_h;
        const int first = std::clamp(entry_ - visible / 2, 0,
                                     std::max(0, static_cast<int>(entries_.size()) - visible));
        y = right.y + 60;
        for (int i = first; i < static_cast<int>(entries_.size()) && i < first + visible; ++i)
        {
            const fs::Entry &e = entries_[static_cast<std::size_t>(i)];
            const bool playable = !e.directory && media::is_playable_extension(e.name);
            const Rect row{right.x + 12, y, right.w - 24, row_h - 6};
            const bool focused = pane_ == 1 && i == entry_;
            if (focused)
                p.s.fill_rounded(row, 14, th::kText);
            const Pixel fg = focused ? th::kTextOnAccent
                                     : (e.directory || playable ? th::kText : th::kTextMuted);
            p.icon(e.directory ? Icon::folder : (playable ? Icon::film : Icon::info), row.x + 36,
                   row.y + row.h / 2, 30, fg);
            p.text(row.x + 76, row.y + 18, e.name, th::kBody, fg, row.w - 360);
            if (!e.directory)
                p.text_right(row.right() - 24, row.y + 20, human_size(e.size), th::kCaption,
                             focused ? th::kTextOnAccent : th::kTextMuted);
            if (playable)
            {
                const std::string key = "local:" + fs::join(path_, e.name);
                for (const auto &h : app_.store().history())
                    if (h.item.key() == key && h.position > 0.0 && !h.finished())
                    {
                        p.progress({row.x + 76, row.bottom() - 10, 240, 5}, h.progress(),
                                   th::kAccentLibrary, gfx::rgba(255, 255, 255, 50));
                        break;
                    }
            }
            y += row_h;
        }
    }

    [[nodiscard]] std::vector<Hint> hints() const override
    {
        if (pane_ == 0)
            return {{Glyph::cross, "Open"}, {Glyph::triangle, "Rescan"}};
        return {{Glyph::cross, "Play / Open"},
                {Glyph::circle, "Up"},
                {Glyph::square, "Favorite"},
                {Glyph::triangle, "Refresh"}};
    }

  private:
    void probe()
    {
        for (auto &r : roots_)
        {
            std::string error;
            const auto list = fs::list(r.path, &error, 2000);
            if (list)
            {
                std::size_t media = 0;
                for (const auto &e : *list)
                    if (e.directory || media::is_playable_extension(e.name))
                        ++media;
                r.available = true;
                r.status = std::to_string(media) + (media == 1 ? " item" : " items");
            }
            else
            {
                r.available = false;
                r.status =
                    error.find("ermission") != std::string::npos ||
                            error.find("ot permitted") != std::string::npos
                        ? "No access from the title sandbox"
                        : (error.find("No such") != std::string::npos ? "Not connected" : error);
            }
        }
    }

    void open_root()
    {
        const Root &r = roots_[static_cast<std::size_t>(root_)];
        navigate(r.path);
        pane_ = 1;
    }

    void navigate(const std::string &path)
    {
        error_.clear();
        entries_.clear();
        entry_ = 0;
        path_ = path;
        std::string error;
        const auto list = fs::list(path, &error);
        if (!list)
        {
            error_ = "Cannot open this folder: " + error +
                     (error.find("ermission") != std::string::npos
                          ? ". Native titles run in a sandbox; this location is outside it."
                          : ".");
            return;
        }
        for (const auto &e : *list)
            if (!e.name.empty() && e.name[0] != '.')
                entries_.push_back(e);
    }

    std::optional<MediaItem> focused_item() const
    {
        if (entry_ >= static_cast<int>(entries_.size()))
            return std::nullopt;
        const fs::Entry &e = entries_[static_cast<std::size_t>(entry_)];
        if (e.directory || !media::is_playable_extension(e.name))
            return std::nullopt;
        MediaItem item;
        item.provider = "local";
        item.id = fs::join(path_, e.name);
        item.kind = ItemKind::file;
        item.title = e.name;
        item.subtitle = path_;
        item.meta = human_size(e.size);
        item.accent = 0x35c79a;
        item.playable = Playable{media::SourceKind::local_file, item.id};
        return item;
    }

    void activate()
    {
        if (entry_ >= static_cast<int>(entries_.size()))
            return;
        const fs::Entry &e = entries_[static_cast<std::size_t>(entry_)];
        if (e.directory)
        {
            navigate(fs::join(path_, e.name));
            return;
        }
        const auto item = focused_item();
        if (!item)
        {
            app_.toast("Unsupported file type", th::kWarning);
            return;
        }
        if (!fs::safe_path(item->id))
        {
            app_.toast("This path cannot be opened", th::kError);
            return;
        }
        app_.play(*item);
    }

    std::vector<Root> roots_;
    int root_ = 0;
    int pane_ = 0;
    std::string path_;
    std::string media_dir_;
    std::vector<fs::Entry> entries_;
    int entry_ = 0;
    std::string error_;
};
} // namespace

std::unique_ptr<Screen> make_library_screen(App &app)
{
    return std::make_unique<LibraryScreen>(app);
}
} // namespace akeno
