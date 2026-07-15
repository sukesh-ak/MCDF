// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/crypto/encoding.hpp"

#include <cstdint>
#include <vector>

namespace mcdf {
namespace {

constexpr char kB64[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
constexpr char kB58[] =
    "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

int b64_value(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

int b58_value(char c) {
  for (int i = 0; i < 58; ++i)
    if (kB58[i] == c) return i;
  return -1;
}

}  // namespace

std::string base64url_encode(std::string_view data) {
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  std::size_t i = 0;
  const auto byte = [&](std::size_t k) {
    return static_cast<std::uint32_t>(static_cast<unsigned char>(data[k]));
  };
  for (; i + 3 <= data.size(); i += 3) {
    const std::uint32_t n = (byte(i) << 16) | (byte(i + 1) << 8) | byte(i + 2);
    out.push_back(kB64[(n >> 18) & 63]);
    out.push_back(kB64[(n >> 12) & 63]);
    out.push_back(kB64[(n >> 6) & 63]);
    out.push_back(kB64[n & 63]);
  }
  const std::size_t rem = data.size() - i;
  if (rem == 1) {
    const std::uint32_t n = byte(i) << 16;
    out.push_back(kB64[(n >> 18) & 63]);
    out.push_back(kB64[(n >> 12) & 63]);
  } else if (rem == 2) {
    const std::uint32_t n = (byte(i) << 16) | (byte(i + 1) << 8);
    out.push_back(kB64[(n >> 18) & 63]);
    out.push_back(kB64[(n >> 12) & 63]);
    out.push_back(kB64[(n >> 6) & 63]);
  }
  return out;
}

Result<std::string> base64url_decode(std::string_view text) {
  std::string out;
  std::uint32_t buf = 0;
  int bits = 0;
  for (const char c : text) {
    const int v = b64_value(c);
    if (v < 0) return fail(ErrorCode::kParse, "invalid base64url character");
    buf = (buf << 6) | static_cast<std::uint32_t>(v);
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      out.push_back(static_cast<char>((buf >> bits) & 0xFF));
    }
  }
  return out;
}

std::string base58btc_encode(std::string_view data) {
  std::size_t zeros = 0;
  while (zeros < data.size() &&
         static_cast<unsigned char>(data[zeros]) == 0) {
    ++zeros;
  }
  std::vector<std::uint8_t> digits;  // little-endian base-58
  for (std::size_t i = zeros; i < data.size(); ++i) {
    int carry = static_cast<unsigned char>(data[i]);
    for (auto& d : digits) {
      const int x = d * 256 + carry;
      d = static_cast<std::uint8_t>(x % 58);
      carry = x / 58;
    }
    while (carry) {
      digits.push_back(static_cast<std::uint8_t>(carry % 58));
      carry /= 58;
    }
  }
  std::string out(zeros, '1');
  for (auto it = digits.rbegin(); it != digits.rend(); ++it)
    out.push_back(kB58[*it]);
  return out;
}

Result<std::string> base58btc_decode(std::string_view text) {
  std::size_t zeros = 0;
  while (zeros < text.size() && text[zeros] == '1') ++zeros;
  std::vector<std::uint8_t> bytes;  // little-endian base-256
  for (std::size_t i = zeros; i < text.size(); ++i) {
    const int v = b58_value(text[i]);
    if (v < 0) return fail(ErrorCode::kParse, "invalid base58 character");
    int carry = v;
    for (auto& b : bytes) {
      const int x = b * 58 + carry;
      b = static_cast<std::uint8_t>(x % 256);
      carry = x / 256;
    }
    while (carry) {
      bytes.push_back(static_cast<std::uint8_t>(carry % 256));
      carry /= 256;
    }
  }
  std::string out(zeros, '\0');
  for (auto it = bytes.rbegin(); it != bytes.rend(); ++it)
    out.push_back(static_cast<char>(*it));
  return out;
}

}  // namespace mcdf
