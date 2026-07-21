// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>

#include "mcdf/model/metadata.hpp"
#include "mcdf/model/schema.hpp"

namespace mcdf {

// Deterministic YAML emission for the editable members (metadata.yaml and
// schema.yaml). Keys are written in a fixed order and scalars are quoted
// whenever a plain YAML scalar could be re-typed or mis-parsed, so the output
// round-trips through parse_metadata_yaml / parse_schema_yaml and reads as
// strings in any typed YAML implementation. Clients (CLI, Studio) must use
// these instead of emitting YAML themselves.
std::string metadata_to_yaml(const Metadata& metadata);
std::string schema_to_yaml(const Schema& schema);

}  // namespace mcdf
