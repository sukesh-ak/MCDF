// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/core/render.hpp"

#include <string>

#include "mcdf/container/container.hpp"
#include "mcdf/core/sign_ops.hpp"
#include "mcdf/crypto/hash.hpp"
#include "mcdf/serialize/parsers.hpp"
#include "mcdf/serialize/render.hpp"
#include "mcdf/version.hpp"

namespace mcdf {
namespace {

std::string html_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

}  // namespace

Result<RenderFormat> parse_render_format(std::string_view name) {
  if (name == "html") return RenderFormat::kHtml;
  if (name == "text") return RenderFormat::kText;
  if (name == "pdf" || name == "docx") {
    return fail(ErrorCode::kUnsupported,
                std::string(name) + " rendering is not available in v1");
  }
  return fail(ErrorCode::kUnsupported, "unknown format: " + std::string(name));
}

std::string_view to_string(RenderFormat format) {
  switch (format) {
    case RenderFormat::kHtml: return "html";
    case RenderFormat::kText: return "text";
  }
  return "unknown";
}

Result<std::string> render(const Container& container, RenderFormat format) {
  if (!container.contains("content.md")) {
    return fail(ErrorCode::kNotFound, "content.md is missing");
  }
  auto content = container.read("content.md");
  if (!content) return std::unexpected(content.error());

  if (format == RenderFormat::kText) {
    return markdown_to_text(*content) + "\n";
  }

  std::string title = "MCDF Document";
  if (container.contains("metadata.yaml")) {
    auto raw = container.read("metadata.yaml");
    if (raw) {
      auto metadata = parse_metadata_yaml(*raw);
      if (metadata && !metadata->title.empty()) title = metadata->title;
    }
  }

  std::string source_hash;
  if (container.contains("manifest.json")) {
    auto canonical = canonical_manifest_bytes(container);
    if (canonical) source_hash = sha256_hex(*canonical);
  }

  const std::string body = markdown_to_html(*content);

  std::string html;
  html += "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
  html += "<meta charset=\"utf-8\">\n";
  html +=
      "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src "
      "'none'; style-src 'unsafe-inline'; img-src data:;\">\n";
  html += "<title>" + html_escape(title) + "</title>\n";
  html += "<meta name=\"generator\" content=\"mcdf/" +
          std::string(version_string()) + "\">\n";
  if (!source_hash.empty()) {
    html += "<meta name=\"mcdf-source\" content=\"sha256:" + source_hash + "\">\n";
  }
  html +=
      "<style>body{font-family:system-ui,-apple-system,sans-serif;"
      "max-width:44rem;margin:2rem auto;padding:0 1rem;line-height:1.6}"
      "pre{overflow-x:auto}</style>\n";
  html += "</head>\n<body>\n";
  html += body;
  html += "</body>\n</html>\n";
  return html;
}

}  // namespace mcdf
