// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"

namespace mcdf {

class Container;

// Which members are ciphertext right now (spec 5.2: the `encrypted_files` list
// in encryption/policy.yaml).
//
// This lives apart from the rest of the encryption code on purpose: loading,
// validating and rendering all have to know what is sealed before they touch a
// member, and none of them needs a key to find out. Reading the list is plain
// YAML, so even a Core-profile implementation with no cryptography at all can —
// and MUST — consult it rather than parse ciphertext as Markdown.
struct SealedMembers {
  bool has_policy = false;
  // False when encryption/policy.yaml is present but unreadable. Callers should
  // treat every member as suspect in that case; `validate` reports it as
  // E_POLICY_INVALID.
  bool policy_readable = false;
  std::vector<std::string> paths;

  bool contains(std::string_view path) const;
};

SealedMembers sealed_members(const Container& container);

}  // namespace mcdf
