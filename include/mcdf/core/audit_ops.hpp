// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "mcdf/crypto/keys.hpp"
#include "mcdf/error.hpp"
#include "mcdf/model/audit.hpp"

namespace mcdf {

class Container;
class DirectoryContainer;

// Reads audit.log (empty vector if absent).
Result<std::vector<AuditEntry>> read_audit_log(const Container& container);

// Appends a new entry, chaining it to the current last entry.
Result<void> audit_append(const DirectoryContainer& dir, std::string_view action,
                          std::string_view actor, std::string_view timestamp);

struct AuditVerification {
  bool ok = false;
  std::size_t entries = 0;
  std::string error;  // set when the chain is broken
};

// Verifies the hash chain from genesis.
Result<AuditVerification> audit_verify(const Container& container);

// Signs the current log head into audit.checkpoint (detached JWS + head).
Result<void> audit_checkpoint(const DirectoryContainer& dir,
                              const PrivateKey& key, std::string_view kid);

struct CheckpointResult {
  bool present = false;
  bool valid = false;
  std::string kid;
  std::string head;
};

// Verifies audit.checkpoint (signature valid AND its head is in the current chain).
Result<CheckpointResult> audit_verify_checkpoint(const Container& container);

}  // namespace mcdf
