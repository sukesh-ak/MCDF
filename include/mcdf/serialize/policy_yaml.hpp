// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>

#include "mcdf/error.hpp"
#include "mcdf/model/encryption_policy.hpp"

namespace mcdf {

Result<EncryptionPolicy> parse_encryption_policy_yaml(std::string_view yaml);
std::string encryption_policy_to_yaml(const EncryptionPolicy& policy);

}  // namespace mcdf
