// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mcdf/error.hpp"

namespace mcdf {

// Serializes members (iterated sorted by path) into a deterministic USTAR
// archive: mtime/uid/gid are zeroed, mode is 0644, owner names are empty - so
// identical content always yields identical bytes.
Result<std::string> tar_write(const std::map<std::string, std::string>& members);

// Parses a USTAR archive into (path, bytes) pairs, verifying header checksums.
Result<std::vector<std::pair<std::string, std::string>>> tar_read(
    std::string_view archive);

}  // namespace mcdf
