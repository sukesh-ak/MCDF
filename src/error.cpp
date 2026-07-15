// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/error.hpp"

namespace mcdf {

std::string_view to_string(ErrorCode code) noexcept {
  switch (code) {
    case ErrorCode::kOk:               return "ok";
    case ErrorCode::kNotFound:         return "not_found";
    case ErrorCode::kIo:               return "io";
    case ErrorCode::kParse:            return "parse";
    case ErrorCode::kInvalidContainer: return "invalid_container";
    case ErrorCode::kPathEscape:       return "path_escape";
    case ErrorCode::kUnsupported:      return "unsupported";
  }
  return "unknown";
}

}  // namespace mcdf
