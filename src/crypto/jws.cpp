// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/crypto/jws.hpp"

#include <nlohmann/json.hpp>

#include <exception>
#include <string>
#include <vector>

#include "mcdf/crypto/encoding.hpp"

namespace nl = nlohmann;

namespace mcdf {
namespace {

constexpr std::string_view kAlg = "EdDSA";

std::vector<std::string_view> split_dots(std::string_view s) {
  std::vector<std::string_view> parts;
  std::size_t start = 0;
  while (true) {
    const std::size_t dot = s.find('.', start);
    if (dot == std::string_view::npos) {
      parts.push_back(s.substr(start));
      break;
    }
    parts.push_back(s.substr(start, dot - start));
    start = dot + 1;
  }
  return parts;
}

}  // namespace

Result<std::string> jws_sign_detached(const PrivateKey& key,
                                      std::string_view payload,
                                      std::string_view kid) {
  nl::json header;
  header["alg"] = std::string(kAlg);
  header["kid"] = std::string(kid);

  const std::string header_b64 = base64url_encode(header.dump());
  const std::string signing_input =
      header_b64 + "." + base64url_encode(payload);

  auto signature = key.sign(signing_input);
  if (!signature) return std::unexpected(signature.error());

  // Detached compact serialization: the payload segment is left empty.
  return header_b64 + ".." + base64url_encode(*signature);
}

Result<JwsVerifyResult> jws_verify_detached(std::string_view compact_jws,
                                            std::string_view payload) {
  const auto parts = split_dots(compact_jws);
  if (parts.size() != 3 || !parts[1].empty()) {
    return fail(ErrorCode::kParse, "malformed detached JWS");
  }
  const std::string_view header_b64 = parts[0];
  const std::string_view sig_b64 = parts[2];

  auto header_json = base64url_decode(header_b64);
  if (!header_json) return std::unexpected(header_json.error());

  std::string alg;
  std::string kid;
  try {
    const auto header = nl::json::parse(*header_json);
    alg = header.value("alg", std::string{});
    kid = header.value("kid", std::string{});
  } catch (const std::exception& e) {
    return fail(ErrorCode::kParse, std::string("JWS header: ") + e.what());
  }

  if (alg != kAlg) {
    return fail(ErrorCode::kUnsupported, "unsupported JWS alg: " + alg);
  }

  auto key = PublicKey::from_did_key(kid);
  if (!key) return std::unexpected(key.error());

  auto signature = base64url_decode(sig_b64);
  if (!signature) return std::unexpected(signature.error());

  const std::string signing_input =
      std::string(header_b64) + "." + base64url_encode(payload);
  auto valid = key->verify(signing_input, *signature);
  if (!valid) return std::unexpected(valid.error());

  return JwsVerifyResult{*valid, alg, kid};
}

}  // namespace mcdf
