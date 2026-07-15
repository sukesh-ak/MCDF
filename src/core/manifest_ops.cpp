// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/core/manifest_ops.hpp"

#include <nlohmann/json.hpp>

#include "mcdf/container/container.hpp"
#include "mcdf/crypto/hash.hpp"
#include "mcdf/serialize/canonical_json.hpp"

namespace nl = nlohmann;

namespace mcdf {

bool is_manifest_excluded(std::string_view rel_path) {
  return rel_path == "manifest.json" || rel_path == "audit.log" ||
         rel_path == "audit.checkpoint" || rel_path.starts_with("signatures/");
}

Result<Manifest> build_manifest(const Container& container,
                                std::string algorithm) {
  auto files = container.list();
  if (!files) return std::unexpected(files.error());

  Manifest m;
  m.mcdf_version = kMcdfVersion;
  m.hash_algorithm = algorithm;
  for (const auto& path : *files) {
    if (is_manifest_excluded(path)) continue;
    auto bytes = container.read(path);
    if (!bytes) return std::unexpected(bytes.error());
    auto digest = hash_hex(algorithm, *bytes);
    if (!digest) return std::unexpected(digest.error());
    m.files[path] = *digest;
  }
  return m;
}

Result<std::string> manifest_to_canonical_json(const Manifest& manifest) {
  nl::json j;
  j["mcdf_version"] = manifest.mcdf_version;
  j["hash_algorithm"] = manifest.hash_algorithm;
  j["files"] = nl::json::object();
  for (const auto& [path, hash] : manifest.files) j["files"][path] = hash;
  return canonicalize_json(j.dump());
}

Result<ManifestVerification> verify_manifest(const Container& container,
                                             const Manifest& manifest) {
  ManifestVerification v;

  for (const auto& [path, expected] : manifest.files) {
    auto bytes = container.read(path);
    if (!bytes) {
      v.missing.push_back(path);
      continue;
    }
    auto digest = hash_hex(manifest.hash_algorithm, *bytes);
    if (!digest) return std::unexpected(digest.error());
    if (*digest != expected) v.mismatched.push_back(path);
  }

  auto files = container.list();
  if (!files) return std::unexpected(files.error());
  for (const auto& path : *files) {
    if (is_manifest_excluded(path)) continue;
    if (!manifest.files.contains(path)) v.extra.push_back(path);
  }

  v.ok = v.missing.empty() && v.extra.empty() && v.mismatched.empty();
  return v;
}

}  // namespace mcdf
