// AKENO STREAM PS5 - Bundled clips, public test streams and user streams.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "providers/provider.hpp"

namespace akeno
{
class OpenCatalog final : public Provider
{
  public:
    // app_dir holds assets/selftest; data_dir may hold streams.json.
    OpenCatalog(std::string app_dir, std::string data_dir);

    [[nodiscard]] ProviderInfo info() const override;
    ShelvesResult home(const net::CancelFlag &cancel) override;

    // Clips packaged with the app (playable without network).
    [[nodiscard]] std::vector<MediaItem> bundled() const;
    // Public DRM-free HLS test streams operated by third parties.
    [[nodiscard]] static std::vector<MediaItem> public_streams();
    // streams.json in the data folder: [{"title":..., "url":..., ...}]
    [[nodiscard]] std::vector<MediaItem> user_streams(std::string *error = nullptr) const;
    [[nodiscard]] std::string user_streams_path() const;

  private:
    std::string app_dir_;
    std::string data_dir_;
};

// Parses the user stream list; invalid entries are skipped and counted.
std::vector<MediaItem> parse_user_streams(const std::string &text, std::string *error);
} // namespace akeno
