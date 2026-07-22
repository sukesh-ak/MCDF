// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// Fuzzes every string-level member parser plus JCS canonicalization - the
// YAML/JSON surfaces an untrusted container's members reach.
#include <string_view>

#include <mcdf/serialize/audit_log.hpp>
#include <mcdf/serialize/canonical_json.hpp>
#include <mcdf/serialize/parsers.hpp>
#include <mcdf/serialize/policy_yaml.hpp>

#include "driver.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string_view text(reinterpret_cast<const char*>(data), size);
  (void)mcdf::parse_metadata_yaml(text);
  (void)mcdf::parse_schema_yaml(text);
  (void)mcdf::parse_manifest_json(text);
  (void)mcdf::parse_encryption_policy_yaml(text);
  (void)mcdf::parse_audit_log(text);
  (void)mcdf::canonicalize_json(text);
  return 0;
}
