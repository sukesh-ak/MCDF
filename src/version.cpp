// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include "mcdf/version.hpp"

namespace mcdf {

// The single source of truth is project(mcdf VERSION ...) in the top-level
// CMakeLists.txt, passed in by src/CMakeLists.txt. The header's constants are
// duplicated there because consumers see the installed header rather than this
// translation unit, so they are checked against the build here: a release that
// bumps one and forgets the other stops compiling.
static_assert(version_major == MCDF_VERSION_MAJOR,
              "mcdf/version.hpp major disagrees with the CMake project version");
static_assert(version_minor == MCDF_VERSION_MINOR,
              "mcdf/version.hpp minor disagrees with the CMake project version");
static_assert(version_patch == MCDF_VERSION_PATCH,
              "mcdf/version.hpp patch disagrees with the CMake project version");

std::string_view version_string() noexcept {
  return MCDF_VERSION_STRING;
}

}  // namespace mcdf
