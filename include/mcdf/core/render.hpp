// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>

#include "mcdf/error.hpp"

namespace mcdf {

class Container;

enum class RenderFormat { kHtml, kText };

Result<RenderFormat> parse_render_format(std::string_view name);
std::string_view to_string(RenderFormat format);

// Renders a container's content.md to the requested format. HTML output is a
// full, self-contained document: a strict CSP (no external fetches, no scripts),
// the title from metadata, and a provenance stamp (container source hash +
// renderer version). Deterministic: same container + renderer -> same bytes.
Result<std::string> render(const Container& container, RenderFormat format);

}  // namespace mcdf
