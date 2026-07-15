// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <vector>

#include "mcdf/crypto/enc_keys.hpp"
#include "mcdf/error.hpp"

namespace mcdf {

class DirectoryContainer;

// Encrypts the named files in place with AES-256-GCM, wraps the content key to
// each recipient via HPKE, writes encryption/policy.yaml, and rebuilds
// manifest.json over the resulting ciphertext.
Result<void> encrypt_container(const DirectoryContainer& dir,
                               const std::vector<std::string>& files,
                               const std::vector<EncPublicKey>& recipients);

// Reverses encrypt_container using a recipient's private key: decrypts the
// files, removes encryption/policy.yaml, and rebuilds manifest.json.
Result<void> decrypt_container(const DirectoryContainer& dir,
                               const EncPrivateKey& key);

}  // namespace mcdf
