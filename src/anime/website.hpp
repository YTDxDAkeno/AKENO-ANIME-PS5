// AKENO STREAM PS5 - Website providers: a website read by one of AKENO's adapters.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "anime/provider.hpp"

#include <memory>
#include <string>

namespace akeno::anime
{
// The settings format version this build reads for a website adapter; 0 for
// an adapter this build does not have (its providers are kept, not loaded).
int website_adapter_version(const std::string &adapter_id) noexcept;

// The episode and playback provider for a website provider (its adapter).
std::unique_ptr<SourceProvider> make_website_provider(const ProviderConfig &config,
                                                      const ProviderContext &context);
} // namespace akeno::anime
