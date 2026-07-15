// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "mcdf/error.hpp"

struct evp_pkey_st;

namespace mcdf {

// An X25519 public key used as an HPKE recipient.
class EncPublicKey {
 public:
  static Result<EncPublicKey> from_did_key(std::string_view did);
  static EncPublicKey from_raw(std::string raw32);

  const std::string& raw() const noexcept { return raw_; }
  Result<std::string> did_key() const;

 private:
  explicit EncPublicKey(std::string raw) : raw_(std::move(raw)) {}
  std::string raw_;  // 32 raw X25519 bytes
};

struct HpkeSealed {
  std::string enc;  // encapsulated key (ephemeral public key)
  std::string ct;   // sealed payload
};

// HPKE (RFC 9180) base-mode seal to a recipient, suite
// DHKEM(X25519,HKDF-SHA256) / HKDF-SHA256 / AES-256-GCM.
Result<HpkeSealed> hpke_seal(const EncPublicKey& recipient,
                             std::string_view info, std::string_view plaintext);

// An X25519 private key: an HPKE recipient's decryption key.
class EncPrivateKey {
 public:
  static Result<EncPrivateKey> generate_x25519();
  static Result<EncPrivateKey> from_pem(std::string_view pem);

  Result<std::string> to_pem() const;
  Result<std::string> did_key() const;
  Result<EncPublicKey> public_key() const;

  // HPKE decap + open to recover a sealed payload.
  Result<std::string> hpke_open(std::string_view enc, std::string_view ct,
                                std::string_view info) const;

 private:
  explicit EncPrivateKey(std::shared_ptr<evp_pkey_st> key)
      : pkey_(std::move(key)) {}
  std::shared_ptr<evp_pkey_st> pkey_;
};

}  // namespace mcdf
