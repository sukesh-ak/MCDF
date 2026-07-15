// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
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
};

// Loads the standard members (content.md, schema.yaml, metadata.yaml,
// manifest.json) from a container into a Document.
Result<Document> load_document(const Container& container);

}  // namespace mcdf
