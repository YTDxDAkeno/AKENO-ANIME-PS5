// AKENO STREAM PS5 - Provider addresses as the user types them and as they are compared.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// "video.blender.org", "https://Video.Blender.org:443" and
// "https://video.blender.org/" are one provider. Normalising keeps the
// address the user meant (no guessing of paths, no search-engine fallback)
// and refuses anything that is not a plain http(s) address: no user names or
// passwords, no other schemes, no control characters.
#pragma once

#include <string>
#include <string_view>

namespace akeno::anime
{
inline constexpr std::size_t kMaxAddressLength = 2048;

struct SiteAddress
{
    bool ok = false;
    std::string url; // scheme://host[:port]/path[?query], default port dropped
    std::string why; // when not ok: one sentence for the user
};

// Adds https:// when no scheme is given, lower-cases scheme and host, drops
// the default port and any #fragment; an empty path becomes "/".
SiteAddress normalize_site_address(std::string_view typed);

// True when two addresses name the same provider: the same host (a leading
// "www." aside), port, path (a trailing slash aside) and query. The scheme
// does not matter: an http address that redirects to https is the same site.
bool same_site_address(std::string_view a, std::string_view b);
} // namespace akeno::anime
