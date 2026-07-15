// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"

namespace mcdf {

struct Heading {
  int level = 0;      // 1..6
  std::string text;   // heading text, with any trailing {#id} removed
  std::string id;     // explicit identifier from "{#id}", else empty
};

// Parses CommonMark content and returns its headings in document order,
// extracting the optional "{#id}" attribute used to bind sections to the schema.
Result<std::vector<Heading>> parse_headings(std::string_view markdown);

}  // namespace mcdf
