// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <memory>
#include <string>
#include <string_view>

#include "mcdf/error.hpp"

// OpenSSL's EVP_PKEY, kept opaque so consumers of this header need no OpenSSL.
struct evp_pkey_st;

namespace mcdf {

// An Ed25519 private (signing) key.
class PrivateKey {
 public:
  static Result<PrivateKey> generate_ed25519();
  static Result<PrivateKey> from_pem(std::string_view pem);

  Result<std::string> to_pem() const;         // PKCS#8 private-key PEM
  Result<std::string> did_key() const;        // did:key of the public half
  Result<std::string> sign(std::string_view data) const;  // raw signature bytes

 private:
  explicit PrivateKey(std::shared_ptr<evp_pkey_st> key) : pkey_(std::move(key)) {}
  std::shared_ptr<evp_pkey_st> pkey_;
};

// An Ed25519 public (verification) key.
class PublicKey {
 public:
  static Result<PublicKey> from_did_key(std::string_view did);

  Result<std::string> did_key() const;
  // Returns true iff signature is valid for data under this key.
  Result<bool> verify(std::string_view data, std::string_view signature) const;

 private:
  explicit PublicKey(std::shared_ptr<evp_pkey_st> key) : pkey_(std::move(key)) {}
  std::shared_ptr<evp_pkey_st> pkey_;
};

}  // namespace mcdf
