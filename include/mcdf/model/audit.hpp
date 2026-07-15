// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>

namespace mcdf {

// One line of audit.log. prev_hash chains each entry to the previous one so
// truncation, reordering, or edits are detectable.
struct AuditEntry {
  std::string timestamp;  // RFC 3339
  std::string action;     // e.g. CREATED, SIGNED, ENCRYPTED
  std::string actor;      // a name or did:key
  std::string prev_hash;  // hex SHA-256 of the previous entry line (genesis = 64 zeros)
};

}  // namespace mcdf
