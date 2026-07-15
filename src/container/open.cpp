// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/container/open.hpp"

#include <system_error>

#include "mcdf/container/directory_container.hpp"
#include "mcdf/container/tar_container.hpp"

namespace mcdf {
namespace fs = std::filesystem;

Result<std::unique_ptr<Container>> open_container(const fs::path& path) {
  std::error_code ec;
  if (fs::is_directory(path, ec)) {
    auto c = DirectoryContainer::open(path);
    if (!c) return std::unexpected(c.error());
    return std::unique_ptr<Container>(std::move(*c));
  }
  if (fs::is_regular_file(path, ec)) {
    auto c = TarContainer::open(path);
    if (!c) return std::unexpected(c.error());
    return std::unique_ptr<Container>(std::move(*c));
  }
  return fail(ErrorCode::kNotFound, "no such container: " + path.string());
}

}  // namespace mcdf
