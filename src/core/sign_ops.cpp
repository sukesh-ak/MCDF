// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/core/sign_ops.hpp"

#include "mcdf/container/container.hpp"
#include "mcdf/crypto/jws.hpp"
#include "mcdf/serialize/canonical_json.hpp"

namespace mcdf {

Result<std::string> canonical_manifest_bytes(const Container& container) {
  if (!container.contains("manifest.json")) {
    return fail(ErrorCode::kNotFound, "manifest.json is missing");
  }
  auto raw = container.read("manifest.json");
  if (!raw) return std::unexpected(raw.error());
  return canonicalize_json(*raw);
}

Result<std::string> sign_container(const Container& container,
                                   const PrivateKey& key, std::string_view kid) {
  auto payload = canonical_manifest_bytes(container);
  if (!payload) return std::unexpected(payload.error());
  return jws_sign_detached(key, *payload, kid);
}

Result<std::vector<SignatureCheck>> verify_container(const Container& container) {
  auto payload = canonical_manifest_bytes(container);
  if (!payload) return std::unexpected(payload.error());

  auto files = container.list();
  if (!files) return std::unexpected(files.error());

  std::vector<SignatureCheck> checks;
  for (const auto& f : *files) {
    if (!f.starts_with("signatures/") || !f.ends_with(".sig")) continue;

    SignatureCheck sc;
    sc.file = f;
    auto raw = container.read(f);
    if (!raw) {
      sc.error = raw.error().message;
      checks.push_back(std::move(sc));
      continue;
    }
    auto result = jws_verify_detached(*raw, *payload);
    if (!result) {
      sc.error = result.error().message;
      checks.push_back(std::move(sc));
      continue;
    }
    sc.valid = result->valid;
    sc.kid = result->kid;
    sc.alg = result->alg;
    checks.push_back(std::move(sc));
  }
  return checks;
}

}  // namespace mcdf
