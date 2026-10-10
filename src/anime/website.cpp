// AKENO STREAM PS5 - Website providers: a website read by one of AKENO's adapters.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "anime/website.hpp"

namespace akeno::anime
{
namespace
{
// A website provider whose adapter is not in this build: kept, never loaded
// as anything else.
class UnavailableProvider final : public SourceProvider
{
  public:
    explicit UnavailableProvider(ProviderConfig config) : config_{std::move(config)}
    {
    }
    [[nodiscard]] const ProviderConfig &config() const override
    {
        return config_;
    }
    LoadedCatalog load(const net::CancelFlag &) override
    {
        LoadedCatalog out;
        out.error = "The website adapter \"" + config_.adapter + "\" is not available.";
        return out;
    }
    Resolution resolve(const Series &, const Season &, const Episode &,
                       const net::CancelFlag &) override
    {
        Resolution r;
        r.availability = Availability::provider_unsupported;
        r.detail = "The website adapter \"" + config_.adapter + "\" is not available.";
        return r;
    }

  private:
    ProviderConfig config_;
};
} // namespace

int website_adapter_version(const std::string &adapter_id) noexcept
{
    if (adapter_id == "schema-org" || adapter_id == "peertube-instance")
        return 1;
    return 0;
}

std::unique_ptr<SourceProvider> make_website_provider(const ProviderConfig &config,
                                                      const ProviderContext &context)
{
    (void)context;
    return std::make_unique<UnavailableProvider>(config);
}
} // namespace akeno::anime
