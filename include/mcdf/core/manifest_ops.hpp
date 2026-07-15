// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"
#include "mcdf/model/manifest.hpp"

namespace mcdf {

class Container;

inline constexpr const char* kMcdfVersion = "1.0";
inline constexpr const char* kDefaultHashAlgorithm = "sha256";

// Result of checking a container against its manifest.
struct ManifestVerification {
  bool ok = false;
  std::vector<std::string> missing;     // listed in manifest but absent/unreadable
  std::vector<std::string> extra;       // present in container but not listed
  std::vector<std::string> mismatched;  // present but hash differs
};

// True if a member is excluded from the manifest file list per the spec
// (manifest.json itself, audit.log, and anything under signatures/).
bool is_manifest_excluded(std::string_view rel_path);

// Builds a manifest by hashing every non-excluded container member.
Result<Manifest> build_manifest(const Container& container,
                                std::string algorithm = kDefaultHashAlgorithm);

// Serializes a manifest to canonical (RFC 8785) JSON bytes.
Result<std::string> manifest_to_canonical_json(const Manifest& manifest);

// Verifies a container's contents against a manifest.
Result<ManifestVerification> verify_manifest(const Container& container,
                                             const Manifest& manifest);

}  // namespace mcdf
