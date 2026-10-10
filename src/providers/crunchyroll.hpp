// AKENO STREAM PS5 - Crunchyroll: capability report and legal alternatives.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// Crunchyroll offers no public API, no third-party sign-in and streams its
// catalogue with DRM. This adapter exists so the interface can state exactly
// that, list what would be required, and point to legitimate options. It never
// shows a login form and never contacts Crunchyroll's private endpoints.
#pragma once

#include "providers/provider.hpp"

namespace akeno
{
class Crunchyroll final : public Provider
{
  public:
    [[nodiscard]] ProviderInfo info() const override;
    ShelvesResult home(const net::CancelFlag &cancel) override;
};
} // namespace akeno
