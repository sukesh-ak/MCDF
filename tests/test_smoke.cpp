// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <string>

#include <mcdf/version.hpp>

// Composed from the constants rather than compared against a literal. A
// hard-coded expectation here froze while the library moved, so this test
// agreed with a version the build had not produced for two releases and
// reported nothing. The constants themselves are tied to the CMake project
// version by static_asserts in src/version.cpp, so the chain is: project()
// -> header constants -> the string this returns.
TEST_CASE("version string matches the declared version") {
  const std::string declared = std::to_string(mcdf::version_major) + "." +
                               std::to_string(mcdf::version_minor) + "." +
                               std::to_string(mcdf::version_patch);
  CHECK(mcdf::version_string() == declared);
}
