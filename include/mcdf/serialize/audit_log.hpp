// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"
#include "mcdf/model/audit.hpp"

namespace mcdf {

// Serializes an entry to its canonical one-line, tab-separated form (no newline).
std::string audit_entry_to_line(const AuditEntry& entry);

// Parses audit.log text into entries. Blank lines are ignored.
Result<std::vector<AuditEntry>> parse_audit_log(std::string_view text);

}  // namespace mcdf
