// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <map>
#include <string>

namespace mcdf {

// Cryptographic inventory (manifest.json). std::map keeps files ordered by path,
// which matches the canonicalization requirement used later for signing.
struct Manifest {
  std::string mcdf_version;
  std::string hash_algorithm;
  std::map<std::string, std::string> files;  // container-relative path -> hash
};

}  // namespace mcdf
