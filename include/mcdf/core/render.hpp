// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#pragma once

#include <string>
#include <string_view>

#include "mcdf/error.hpp"

namespace mcdf {

class Container;

enum class RenderFormat { kHtml, kText };

// Version of the canonical render *format* (spec 10.4), which is independent of
// the library version: every implementation of canonical render N emits the same
// bytes for the same container. Bump only when the output shape changes — 2 added
// heading id attributes, which 1 dropped.
inline constexpr int kCanonicalRenderVersion = 2;

Result<RenderFormat> parse_render_format(std::string_view name);
std::string_view to_string(RenderFormat format);

// Renders a container's content.md to the requested format, in the canonical
// form of spec 10.4: byte-identical across implementations, not merely across
// runs of this one. HTML output is a full, self-contained document with a strict
// CSP (no external fetches, no scripts), the title from metadata, and the
// container's canonical-manifest hash as a provenance stamp.
Result<std::string> render(const Container& container, RenderFormat format);

}  // namespace mcdf
