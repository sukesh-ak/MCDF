// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"

namespace mcdf {

// Abstract read interface over an MCDF container's members. Member paths are
// container-relative and use forward slashes regardless of platform.
class Container {
 public:
  virtual ~Container() = default;

  // True if a regular-file member exists at rel_path.
  virtual bool contains(std::string_view rel_path) const = 0;

  // Reads a member's raw bytes.
  virtual Result<std::string> read(std::string_view rel_path) const = 0;

  // Lists all member paths, sorted, container-relative.
  virtual Result<std::vector<std::string>> list() const = 0;
};

}  // namespace mcdf
