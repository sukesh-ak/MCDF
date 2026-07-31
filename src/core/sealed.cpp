// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include "mcdf/core/sealed.hpp"

#include <algorithm>

#include "mcdf/container/container.hpp"
#include "mcdf/serialize/policy_yaml.hpp"

namespace mcdf {
namespace {
constexpr const char* kPolicyPath = "encryption/policy.yaml";
}  // namespace

bool SealedMembers::contains(std::string_view path) const {
  return std::find(paths.begin(), paths.end(), path) != paths.end();
}

SealedMembers sealed_members(const Container& container) {
  SealedMembers sealed;
  if (!container.contains(kPolicyPath)) return sealed;
  sealed.has_policy = true;

  auto raw = container.read(kPolicyPath);
  if (!raw) return sealed;
  auto policy = parse_encryption_policy_yaml(*raw);
  if (!policy) return sealed;  // policy_readable stays false

  sealed.policy_readable = true;
  sealed.paths = policy->encrypted_files;
  return sealed;
}

}  // namespace mcdf
