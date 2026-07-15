// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/crypto/hash.hpp"

#include <openssl/evp.h>

#include <string>

namespace mcdf {
namespace {

constexpr char kHex[] = "0123456789abcdef";

std::string to_hex(const unsigned char* bytes, unsigned int len) {
  std::string out;
  out.reserve(static_cast<std::size_t>(len) * 2);
  for (unsigned int i = 0; i < len; ++i) {
    out.push_back(kHex[bytes[i] >> 4]);
    out.push_back(kHex[bytes[i] & 0x0F]);
  }
  return out;
}

}  // namespace

std::string sha256_hex(std::string_view data) {
  unsigned char md[EVP_MAX_MD_SIZE];
  unsigned int md_len = 0;
  EVP_Digest(data.data(), data.size(), md, &md_len, EVP_sha256(), nullptr);
  return to_hex(md, md_len);
}

Result<std::string> hash_hex(std::string_view algorithm, std::string_view data) {
  if (algorithm == "sha256") return sha256_hex(data);
  return fail(ErrorCode::kUnsupported,
              "unsupported hash algorithm: " + std::string(algorithm));
}

}  // namespace mcdf
