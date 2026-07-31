// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
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

// Splits "Title {#id}" into text + id (id empty when there is no anchor).
//
// Shared rather than duplicated: the renderer needs exactly the same reading of
// an anchor that schema binding uses, or a heading could bind to a section and
// still render with the anchor showing.
void split_heading_id(std::string_view raw, Heading& out);

// Parses CommonMark content and returns its headings in document order,
// extracting the optional "{#id}" attribute used to bind sections to the schema.
Result<std::vector<Heading>> parse_headings(std::string_view markdown);

}  // namespace mcdf
