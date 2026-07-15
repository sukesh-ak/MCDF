// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <vector>

namespace mcdf {

struct SchemaSection {
  std::string id;
  std::string title;
  bool required = false;
};

// Logical document structure (schema.yaml).
struct Schema {
  std::string document_type;
  std::vector<SchemaSection> sections;
};

}  // namespace mcdf
