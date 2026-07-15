// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/crypto/aead.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <string>

namespace mcdf {
namespace {
constexpr std::size_t kNonceLen = 12;
constexpr std::size_t kTagLen = 16;
constexpr std::size_t kKeyLen = 32;

const unsigned char* uc(std::string_view s) {
  return reinterpret_cast<const unsigned char*>(s.data());
}
}  // namespace

std::string random_bytes(std::size_t n) {
  std::string out(n, '\0');
  RAND_bytes(reinterpret_cast<unsigned char*>(out.data()),
             static_cast<int>(n));
  return out;
}

Result<std::string> aes256gcm_seal(std::string_view key, std::string_view aad,
                                   std::string_view plaintext) {
  if (key.size() != kKeyLen) {
    return fail(ErrorCode::kUnsupported, "AES-256-GCM key must be 32 bytes");
  }
  const std::string nonce = random_bytes(kNonceLen);

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return fail(ErrorCode::kIo, "cipher ctx alloc failed");

  std::string ciphertext(plaintext.size(), '\0');
  std::string tag(kTagLen, '\0');
  Result<std::string> result = fail(ErrorCode::kIo, "encryption failed");

  int len = 0;
  if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1 &&
      EVP_EncryptInit_ex(ctx, nullptr, nullptr, uc(key), uc(nonce)) == 1) {
    int ok = 1;
    if (!aad.empty()) {
      ok = EVP_EncryptUpdate(ctx, nullptr, &len, uc(aad),
                             static_cast<int>(aad.size()));
    }
    if (ok == 1 &&
        EVP_EncryptUpdate(
            ctx, reinterpret_cast<unsigned char*>(ciphertext.data()), &len,
            uc(plaintext), static_cast<int>(plaintext.size())) == 1) {
      int final_len = 0;
      if (EVP_EncryptFinal_ex(
              ctx, reinterpret_cast<unsigned char*>(ciphertext.data()) + len,
              &final_len) == 1 &&
          EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagLen,
                              tag.data()) == 1) {
        result = nonce + ciphertext + tag;
      }
    }
  }
  EVP_CIPHER_CTX_free(ctx);
  return result;
}

Result<std::string> aes256gcm_open(std::string_view key, std::string_view aad,
                                   std::string_view sealed) {
  if (key.size() != kKeyLen) {
    return fail(ErrorCode::kUnsupported, "AES-256-GCM key must be 32 bytes");
  }
  if (sealed.size() < kNonceLen + kTagLen) {
    return fail(ErrorCode::kParse, "ciphertext too short");
  }
  const std::string_view nonce = sealed.substr(0, kNonceLen);
  const std::string_view tag = sealed.substr(sealed.size() - kTagLen);
  const std::string_view ct =
      sealed.substr(kNonceLen, sealed.size() - kNonceLen - kTagLen);

  EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
  if (!ctx) return fail(ErrorCode::kIo, "cipher ctx alloc failed");

  std::string plaintext(ct.size(), '\0');
  Result<std::string> result = fail(ErrorCode::kParse, "decryption failed");

  int len = 0;
  if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) == 1 &&
      EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, kNonceLen, nullptr) == 1 &&
      EVP_DecryptInit_ex(ctx, nullptr, nullptr, uc(key), uc(nonce)) == 1) {
    int ok = 1;
    if (!aad.empty()) {
      ok = EVP_DecryptUpdate(ctx, nullptr, &len, uc(aad),
                             static_cast<int>(aad.size()));
    }
    if (ok == 1 &&
        EVP_DecryptUpdate(
            ctx, reinterpret_cast<unsigned char*>(plaintext.data()), &len,
            uc(ct), static_cast<int>(ct.size())) == 1 &&
        EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagLen,
                            const_cast<char*>(tag.data())) == 1) {
      int final_len = 0;
      if (EVP_DecryptFinal_ex(
              ctx, reinterpret_cast<unsigned char*>(plaintext.data()) + len,
              &final_len) == 1) {
        result = plaintext;
      }
    }
  }
  EVP_CIPHER_CTX_free(ctx);
  return result;
}

}  // namespace mcdf
