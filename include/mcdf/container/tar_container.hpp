// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mcdf/container/container.hpp"
#include "mcdf/error.hpp"

namespace mcdf {

// A read-only container backed by a single-file TAR (.mcdf) archive, parsed
// fully into memory.
class TarContainer : public Container {
 public:
  static Result<std::unique_ptr<TarContainer>> open(
      const std::filesystem::path& file);
  static Result<std::unique_ptr<TarContainer>> from_bytes(std::string archive);

  bool contains(std::string_view rel_path) const override;
  Result<std::string> read(std::string_view rel_path) const override;
  Result<std::vector<std::string>> list() const override;

 private:
  explicit TarContainer(std::map<std::string, std::string> members)
      : members_(std::move(members)) {}
  std::map<std::string, std::string> members_;
};

}  // namespace mcdf
