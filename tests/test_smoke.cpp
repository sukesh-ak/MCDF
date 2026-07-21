// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <mcdf/version.hpp>

TEST_CASE("version string matches the declared version") {
  CHECK(mcdf::version_string() == "0.4.0");
  CHECK(mcdf::version_major == 0);
  CHECK(mcdf::version_minor == 4);
  CHECK(mcdf::version_patch == 0);
}
