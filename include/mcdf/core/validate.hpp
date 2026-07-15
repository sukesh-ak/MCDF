// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"

namespace mcdf {

class Container;
struct Document;

// Conformance profiles (see the spec). Each is a superset of the one before.
enum class Profile { kCore, kIntegrity, kSigned, kEncrypted, kRender };

Result<Profile> parse_profile(std::string_view name);
std::string_view to_string(Profile profile);

// A single validation finding, identified by a stable error code.
struct ValidationIssue {
  std::string code;     // e.g. "E_MANIFEST_HASH_MISMATCH"
  std::string message;  // human-readable detail
};

struct ValidationReport {
  bool ok = false;
  Profile profile = Profile::kCore;
  std::vector<ValidationIssue> issues;
};

// Validates a loaded document/container up to the requested profile.
Result<ValidationReport> validate(const Container& container,
                                  const Document& document, Profile profile);

}  // namespace mcdf
