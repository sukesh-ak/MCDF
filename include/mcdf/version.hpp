// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#pragma once

#include <string_view>

namespace mcdf {

// Kept in step with project(mcdf VERSION ...) by static_asserts in
// version.cpp - changing one without the other fails the build.
inline constexpr int version_major = 0;
inline constexpr int version_minor = 8;
inline constexpr int version_patch = 5;

// Library version as "MAJOR.MINOR.PATCH".
std::string_view version_string() noexcept;

}  // namespace mcdf
