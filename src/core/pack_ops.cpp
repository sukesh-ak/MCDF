// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/core/pack_ops.hpp"

#include <fstream>
#include <map>
#include <system_error>

#include "mcdf/container/container.hpp"
#include "mcdf/container/tar.hpp"

namespace mcdf {
namespace fs = std::filesystem;

Result<std::string> pack_container(const Container& container) {
  auto files = container.list();
  if (!files) return std::unexpected(files.error());

  std::map<std::string, std::string> members;
  for (const auto& path : *files) {
    auto bytes = container.read(path);
    if (!bytes) return std::unexpected(bytes.error());
    members.emplace(path, std::move(*bytes));
  }
  return tar_write(members);
}

Result<void> unpack_archive(std::string_view archive, const fs::path& dest) {
  auto members = tar_read(archive);
  if (!members) return std::unexpected(members.error());

  for (const auto& [name, data] : *members) {
    const fs::path rel(name);
    if (rel.is_absolute()) {
      return fail(ErrorCode::kPathEscape, "absolute path in archive: " + name);
    }
    for (const auto& part : rel) {
      if (part == "..") {
        return fail(ErrorCode::kPathEscape, "'..' in archive path: " + name);
      }
    }
    const fs::path full = dest / rel;
    std::error_code ec;
    fs::create_directories(full.parent_path(), ec);
    std::ofstream out(full, std::ios::binary | std::ios::trunc);
    if (!out) return fail(ErrorCode::kIo, "cannot write: " + full.string());
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!out) return fail(ErrorCode::kIo, "write failed: " + full.string());
  }
  return {};
}

}  // namespace mcdf
