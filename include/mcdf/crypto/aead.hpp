// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "mcdf/error.hpp"

namespace mcdf {

// Cryptographically secure random bytes.
std::string random_bytes(std::size_t n);

// AES-256-GCM. `key` must be 32 bytes. A fresh 12-byte nonce is generated
// internally; the output is nonce(12) || ciphertext || tag(16).
Result<std::string> aes256gcm_seal(std::string_view key, std::string_view aad,
                                   std::string_view plaintext);

// Reverses aes256gcm_seal. `sealed` is nonce(12) || ciphertext || tag(16).
// Fails (ErrorCode::kParse) if authentication does not verify.
Result<std::string> aes256gcm_open(std::string_view key, std::string_view aad,
                                   std::string_view sealed);

}  // namespace mcdf
