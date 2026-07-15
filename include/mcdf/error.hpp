// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#pragma once

#include <expected>
#include <string>
#include <string_view>
#include <utility>

namespace mcdf {

enum class ErrorCode {
  kOk = 0,
  kNotFound,
  kIo,
  kParse,
  kInvalidContainer,
  kPathEscape,
  kUnsupported,
};

struct Error {
  ErrorCode code = ErrorCode::kOk;
  std::string message;
};

// Stable, lowercase identifier for an error code.
std::string_view to_string(ErrorCode code) noexcept;

// The result type used across the library API: a value or an Error.
template <typename T>
using Result = std::expected<T, Error>;

// Convenience for returning a failure from a Result-returning function.
inline std::unexpected<Error> fail(ErrorCode code, std::string message) {
  return std::unexpected(Error{code, std::move(message)});
}

}  // namespace mcdf
