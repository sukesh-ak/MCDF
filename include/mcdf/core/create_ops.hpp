// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#pragma once

#include <string_view>

#include "mcdf/error.hpp"
#include "mcdf/model/metadata.hpp"

namespace mcdf {

class DirectoryContainer;

// Creates the standard members of a new document in a directory container:
// canonical content.md (spec §10.3 form), metadata.yaml (the title is derived
// from the first heading when empty), and a fresh manifest.json covering
// every member present - copy assets into the directory first and they are
// included. Fails if content.md already exists (never overwrites a document).
Result<void> create_document(const DirectoryContainer& dir,
                             std::string_view markdown, Metadata metadata);

}  // namespace mcdf
