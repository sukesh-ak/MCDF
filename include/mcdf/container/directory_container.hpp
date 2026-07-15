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

  // Writes a member, creating parent directories as needed (e.g. signatures/).
  // Path-escape guarded like read(). Not part of the read-only Container base.
  Result<void> write(std::string_view rel_path, std::string_view bytes) const;

  // Removes a member; also removes its parent directory if it becomes empty.
  Result<void> remove(std::string_view rel_path) const;

  // Appends bytes to a member (append-only writes, e.g. audit.log).
  Result<void> append(std::string_view rel_path, std::string_view bytes) const;

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
