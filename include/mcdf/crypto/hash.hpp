// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>

#include "mcdf/error.hpp"

namespace mcdf {

// Lowercase-hex SHA-256 digest of data.
std::string sha256_hex(std::string_view data);

// Hashes data with the named algorithm (algorithm agility). Currently the only
// supported value is "sha256"; anything else yields ErrorCode::kUnsupported.
Result<std::string> hash_hex(std::string_view algorithm, std::string_view data);

}  // namespace mcdf
