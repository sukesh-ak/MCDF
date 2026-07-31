// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
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

// The structure attestation (spec 5.2.1): which schema.yaml sections bound to a
// heading at the moment content.md was sealed.
//
// A validator without the key cannot see the headings, so this is what it
// evaluates 4.2 against. It is a statement by the encryptor rather than a proof
// — what makes it worth anything is that the manifest covers it (so a signature
// attributes it) and that decrypt_container re-checks it against the real
// headings, so a false attestation does not survive the first recipient.
struct StructureAttestation {
  // Absent entirely for containers with no readable schema; the distinction
  // matters, because "no sections bound" and "no claim made" are different.
  bool present = false;
  std::vector<std::string> bound_sections;
};

// encryption/policy.yaml: what is encrypted and how the CEK is wrapped.
struct EncryptionPolicy {
  std::string method = "aes-256-gcm";
  std::string key_management = "hpke";
  std::vector<std::string> encrypted_files;
  StructureAttestation structure;
  std::vector<Recipient> recipients;
};

}  // namespace mcdf
