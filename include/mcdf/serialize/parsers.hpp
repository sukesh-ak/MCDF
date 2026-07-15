// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string_view>

#include "mcdf/error.hpp"
#include "mcdf/model/manifest.hpp"
#include "mcdf/model/metadata.hpp"
#include "mcdf/model/schema.hpp"

namespace mcdf {

Result<Metadata> parse_metadata_yaml(std::string_view yaml);
Result<Schema> parse_schema_yaml(std::string_view yaml);
Result<Manifest> parse_manifest_json(std::string_view json);

}  // namespace mcdf
