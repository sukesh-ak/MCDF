// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>

#include "mcdf/crypto/keys.hpp"
#include "mcdf/error.hpp"

namespace mcdf {

// Produces a detached JWS (RFC 7515) in compact form "header..signature": the
// payload segment is empty because the payload (the canonical manifest) is
// stored separately in the container. Header is {"alg":"EdDSA","kid":<did:key>}.
Result<std::string> jws_sign_detached(const PrivateKey& key,
                                      std::string_view payload,
                                      std::string_view kid);

struct JwsVerifyResult {
  bool valid = false;
  std::string alg;
  std::string kid;
};

// Verifies a detached JWS over payload. The verifying key is resolved from the
// header's `kid` (a did:key). The alg allow-list is "EdDSA" and "ES256", and
// the resolved key's type must match the header alg (confusion guard).
Result<JwsVerifyResult> jws_verify_detached(std::string_view compact_jws,
                                            std::string_view payload);

}  // namespace mcdf
