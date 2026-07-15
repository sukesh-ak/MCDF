// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "mcdf/container/container.hpp"
#include "mcdf/error.hpp"

namespace mcdf {

// A container backed by a directory on the filesystem (the authoring form).
class DirectoryContainer : public Container {
 public:
  // Opens an existing directory as a container.
  static Result<std::unique_ptr<DirectoryContainer>> open(
      const std::filesystem::path& root);

  bool contains(std::string_view rel_path) const override;
  Result<std::string> read(std::string_view rel_path) const override;
  Result<std::vector<std::string>> list() const override;

  const std::filesystem::path& root() const noexcept { return root_; }

 private:
  explicit DirectoryContainer(std::filesystem::path root)
      : root_(std::move(root)) {}

  // Maps a container-relative path to a filesystem path, rejecting absolute
  // paths and any '..' component (no escaping the container).
  Result<std::filesystem::path> resolve(std::string_view rel_path) const;

  std::filesystem::path root_;
};

}  // namespace mcdf
