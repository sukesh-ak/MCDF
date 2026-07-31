// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include "mcdf/model/document.hpp"

#include "mcdf/container/container.hpp"
#include "mcdf/core/sealed.hpp"
#include "mcdf/serialize/markdown.hpp"
#include "mcdf/serialize/parsers.hpp"

namespace mcdf {

Result<Document> load_document(const Container& container) {
  Document doc;

  // What is ciphertext has to be established before anything is parsed. Feeding
  // a sealed member to a YAML or CommonMark parser is not a graceful
  // degradation: YAML errors out and takes the whole load with it, while
  // CommonMark cheerfully produces headings that are not there (spec 5.2, 11).
  doc.sealed = sealed_members(container).paths;

  if (container.contains("content.md")) {
    auto raw = container.read("content.md");
    if (!raw) return std::unexpected(raw.error());
    doc.content = std::move(*raw);
    doc.has_content = true;

    // The member exists either way; sealed just means it has no readable
    // structure, so `headings` stays empty rather than being invented.
    if (!doc.is_sealed("content.md")) {
      auto headings = parse_headings(doc.content);
      if (!headings) return std::unexpected(headings.error());
      doc.headings = std::move(*headings);
    }
  }

  if (container.contains("metadata.yaml") && !doc.is_sealed("metadata.yaml")) {
    auto raw = container.read("metadata.yaml");
    if (!raw) return std::unexpected(raw.error());
    auto parsed = parse_metadata_yaml(*raw);
    if (!parsed) return std::unexpected(parsed.error());
    doc.metadata = std::move(*parsed);
    doc.has_metadata = true;
  }

  if (container.contains("schema.yaml") && !doc.is_sealed("schema.yaml")) {
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
