// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/model/document.hpp"

#include "mcdf/container/container.hpp"
#include "mcdf/serialize/markdown.hpp"
#include "mcdf/serialize/parsers.hpp"

namespace mcdf {

Result<Document> load_document(const Container& container) {
  Document doc;

  if (container.contains("content.md")) {
    auto raw = container.read("content.md");
    if (!raw) return std::unexpected(raw.error());
    doc.content = std::move(*raw);
    doc.has_content = true;

    auto headings = parse_headings(doc.content);
    if (!headings) return std::unexpected(headings.error());
    doc.headings = std::move(*headings);
  }

  if (container.contains("metadata.yaml")) {
    auto raw = container.read("metadata.yaml");
    if (!raw) return std::unexpected(raw.error());
    auto parsed = parse_metadata_yaml(*raw);
    if (!parsed) return std::unexpected(parsed.error());
    doc.metadata = std::move(*parsed);
    doc.has_metadata = true;
  }

  if (container.contains("schema.yaml")) {
    auto raw = container.read("schema.yaml");
    if (!raw) return std::unexpected(raw.error());
    auto parsed = parse_schema_yaml(*raw);
    if (!parsed) return std::unexpected(parsed.error());
    doc.schema = std::move(*parsed);
    doc.has_schema = true;
  }

  if (container.contains("manifest.json")) {
    auto raw = container.read("manifest.json");
    if (!raw) return std::unexpected(raw.error());
    auto parsed = parse_manifest_json(*raw);
    if (!parsed) return std::unexpected(parsed.error());
    doc.manifest = std::move(*parsed);
    doc.has_manifest = true;
  }

  return doc;
}

}  // namespace mcdf
