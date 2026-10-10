// AKENO STREAM PS5 - Application version.
// Copyright (C) 2026 AKENO STREAM contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace akeno
{
// Keep in step with contentVersion in sce_sys/param.json (1.0.0 is
// 01.100.000: 0.x releases used 01.00x.000, so it stays higher). A release
// tag vX.Y.Z must match this version (.github/workflows/tooling.yml).
inline constexpr const char *kAppVersion = "1.0.0";
} // namespace akeno
