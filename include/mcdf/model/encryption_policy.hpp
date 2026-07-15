// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <vector>

namespace mcdf {

// A per-recipient wrapped content-encryption key.
struct Recipient {
  std::string id;           // recipient did:key (X25519)
  std::string enc;          // base64url HPKE encapsulated key
  std::string wrapped_key;  // base64url HPKE-sealed CEK
};

// encryption/policy.yaml: what is encrypted and how the CEK is wrapped.
struct EncryptionPolicy {
  std::string method = "aes-256-gcm";
  std::string key_management = "hpke";
  std::vector<std::string> encrypted_files;
  std::vector<Recipient> recipients;
};

}  // namespace mcdf
