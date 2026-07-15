// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <string>
#include <string_view>

#include "mcdf/error.hpp"

namespace mcdf {

// Serializes JSON text into RFC 8785 (JSON Canonicalization Scheme) form:
// object keys sorted by UTF-16 code unit, no insignificant whitespace, and
// minimal string escaping. This is the byte-deterministic form that the
// manifest is hashed and signed over.
//
// Note: numbers are limited to integers (the manifest contains none); full
// ECMA-262 float canonicalization is deferred until a value type needs it.
Result<std::string> canonicalize_json(std::string_view json_text);

}  // namespace mcdf
