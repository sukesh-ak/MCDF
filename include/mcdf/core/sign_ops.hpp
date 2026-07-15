// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mcdf/crypto/keys.hpp"
#include "mcdf/error.hpp"

namespace mcdf {

class Container;

// The canonical bytes that signatures cover: the container's manifest.json
// re-serialized in RFC 8785 form.
Result<std::string> canonical_manifest_bytes(const Container& container);

// Signs a container's canonical manifest, returning a detached JWS. The caller
// writes it to signatures/<name>.sig.
Result<std::string> sign_container(const Container& container,
                                   const PrivateKey& key, std::string_view kid);

struct SignatureCheck {
  std::string file;   // e.g. signatures/author.sig
  bool valid = false;
  std::string kid;    // signer's did:key
  std::string alg;    // e.g. EdDSA
  std::string error;  // set when the signature could not be verified
};

// Verifies every signatures/*.sig against the canonical manifest.
Result<std::vector<SignatureCheck>> verify_container(const Container& container);

}  // namespace mcdf
