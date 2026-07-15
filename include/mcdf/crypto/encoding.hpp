// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>

#include "mcdf/error.hpp"

namespace mcdf {

// base64url without padding (RFC 4648 §5) - used by JWS.
std::string base64url_encode(std::string_view data);
Result<std::string> base64url_decode(std::string_view text);

// base58btc (Bitcoin alphabet) - used by did:key multibase encoding.
std::string base58btc_encode(std::string_view data);
Result<std::string> base58btc_decode(std::string_view text);

}  // namespace mcdf
