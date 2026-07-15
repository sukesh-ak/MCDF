// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string_view>

namespace mcdf {

inline constexpr int version_major = 0;
inline constexpr int version_minor = 1;
inline constexpr int version_patch = 0;

// Library version as "MAJOR.MINOR.PATCH".
std::string_view version_string() noexcept;

}  // namespace mcdf
