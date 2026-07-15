// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/container/directory_container.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <system_error>

namespace mcdf {
namespace fs = std::filesystem;

Result<std::unique_ptr<DirectoryContainer>> DirectoryContainer::open(
    const fs::path& root) {
  std::error_code ec;
  if (!fs::exists(root, ec)) {
    return fail(ErrorCode::kNotFound, "container not found: " + root.string());
  }
  if (!fs::is_directory(root, ec)) {
    return fail(ErrorCode::kInvalidContainer,
                "container is not a directory: " + root.string());
  }
  return std::unique_ptr<DirectoryContainer>(new DirectoryContainer(root));
}

Result<fs::path> DirectoryContainer::resolve(std::string_view rel_path) const {
  fs::path rel(rel_path);
  if (rel.is_absolute()) {
    return fail(ErrorCode::kPathEscape,
                "absolute member path not allowed: " + std::string(rel_path));
  }
  for (const auto& part : rel) {
    if (part == "..") {
      return fail(ErrorCode::kPathEscape,
                  "'..' not allowed in member path: " + std::string(rel_path));
    }
  }
  return root_ / rel;
}

bool DirectoryContainer::contains(std::string_view rel_path) const {
  auto p = resolve(rel_path);
  if (!p) return false;
  std::error_code ec;
  return fs::is_regular_file(*p, ec);
}

Result<std::string> DirectoryContainer::read(std::string_view rel_path) const {
  auto p = resolve(rel_path);
  if (!p) return std::unexpected(p.error());

  std::error_code ec;
  if (!fs::is_regular_file(*p, ec)) {
    return fail(ErrorCode::kNotFound, "member not found: " + std::string(rel_path));
  }
  std::ifstream in(*p, std::ios::binary);
  if (!in) {
    return fail(ErrorCode::kIo, "cannot open member: " + std::string(rel_path));
  }
  std::string data((std::istreambuf_iterator<char>(in)),
                   std::istreambuf_iterator<char>());
  return data;
}

Result<void> DirectoryContainer::write(std::string_view rel_path,
                                       std::string_view bytes) const {
  auto p = resolve(rel_path);
  if (!p) return std::unexpected(p.error());

  std::error_code ec;
  fs::create_directories(p->parent_path(), ec);
  std::ofstream out(*p, std::ios::binary | std::ios::trunc);
  if (!out) {
    return fail(ErrorCode::kIo, "cannot write member: " + std::string(rel_path));
  }
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!out) {
    return fail(ErrorCode::kIo, "write failed: " + std::string(rel_path));
  }
  return {};
}

Result<std::vector<std::string>> DirectoryContainer::list() const {
  std::vector<std::string> out;
  std::error_code ec;
  for (auto it = fs::recursive_directory_iterator(root_, ec);
       !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
    if (it->is_regular_file(ec)) {
      auto rel = fs::relative(it->path(), root_, ec);
      if (!ec) out.push_back(rel.generic_string());
    }
  }
  if (ec) {
    return fail(ErrorCode::kIo, "failed to list container: " + ec.message());
  }
  std::sort(out.begin(), out.end());
  return out;
}

}  // namespace mcdf
