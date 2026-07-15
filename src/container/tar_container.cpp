// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/container/tar_container.hpp"

#include <fstream>
#include <iterator>

#include "mcdf/container/tar.hpp"

namespace mcdf {
namespace fs = std::filesystem;

Result<std::unique_ptr<TarContainer>> TarContainer::from_bytes(
    std::string archive) {
  auto members = tar_read(archive);
  if (!members) return std::unexpected(members.error());
  std::map<std::string, std::string> map;
  for (auto& [path, data] : *members) map.emplace(path, std::move(data));
  return std::unique_ptr<TarContainer>(new TarContainer(std::move(map)));
}

Result<std::unique_ptr<TarContainer>> TarContainer::open(const fs::path& file) {
  std::ifstream in(file, std::ios::binary);
  if (!in) return fail(ErrorCode::kNotFound, "cannot open archive: " + file.string());
  std::string bytes((std::istreambuf_iterator<char>(in)),
                    std::istreambuf_iterator<char>());
  return from_bytes(std::move(bytes));
}

bool TarContainer::contains(std::string_view rel_path) const {
  return members_.contains(std::string(rel_path));
}

Result<std::string> TarContainer::read(std::string_view rel_path) const {
  const auto it = members_.find(std::string(rel_path));
  if (it == members_.end())
    return fail(ErrorCode::kNotFound, "member not found: " + std::string(rel_path));
  return it->second;
}

Result<std::vector<std::string>> TarContainer::list() const {
  std::vector<std::string> out;
  out.reserve(members_.size());
  for (const auto& [path, data] : members_) out.push_back(path);
  return out;  // std::map keeps them sorted
}

}  // namespace mcdf
