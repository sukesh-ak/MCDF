// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>

namespace mcdf {

// CommonMark -> sanitized HTML fragment. Raw HTML and scripts are stripped
// (escaped), and heading "{#id}" attributes are removed for clean output.
std::string markdown_to_html(std::string_view markdown);

// CommonMark -> plain text: block text with blank lines between blocks.
std::string markdown_to_text(std::string_view markdown);

}  // namespace mcdf
