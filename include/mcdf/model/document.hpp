// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"
#include "mcdf/model/manifest.hpp"
#include "mcdf/model/metadata.hpp"
#include "mcdf/model/schema.hpp"
#include "mcdf/serialize/markdown.hpp"

namespace mcdf {

class Container;

// An in-memory view of an MCDF document loaded from a container. Members that
// are absent leave the corresponding has_* flag false (inspect tolerates
// partial containers; full validation arrives in a later milestone).
struct Document {
  Metadata metadata;
  Schema schema;
  Manifest manifest;
  std::string content;              // raw content.md
  std::vector<Heading> headings;    // parsed from content.md

  bool has_metadata = false;
  bool has_schema = false;
  bool has_manifest = false;
  bool has_content = false;

  // Members whose stored bytes are ciphertext (spec 5.2). A sealed member is
  // present but not parsed: `has_content` stays true for a sealed content.md
  // (the member is there) while `headings` stays empty (nothing readable), and
  // a sealed schema.yaml leaves `has_schema` false because a schema nobody can
  // read makes no structural claims.
  std::vector<std::string> sealed;

  bool is_sealed(std::string_view path) const {
    return std::find(sealed.begin(), sealed.end(), path) != sealed.end();
  }
};

// Loads the standard members (content.md, schema.yaml, metadata.yaml,
// manifest.json) from a container into a Document.
Result<Document> load_document(const Container& container);

}  // namespace mcdf
