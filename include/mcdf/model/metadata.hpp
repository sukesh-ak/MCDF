// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace mcdf {

struct Author {
  std::string name;
  std::string id;  // e.g. a DID such as did:key:z6Mk...
};

// Descriptive/operational metadata (metadata.yaml).
struct Metadata {
  std::string title;
  std::string version;
  std::vector<Author> authors;
  std::string created_at;
  std::string classification;
  std::string language;
  std::optional<std::string> generated_by;  // provenance for AI-authored content
};

}  // namespace mcdf
