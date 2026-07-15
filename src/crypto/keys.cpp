// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/crypto/keys.hpp"

#include <openssl/evp.h>
#include <openssl/pem.h>

#include <string>
#include <vector>

#include "mcdf/crypto/encoding.hpp"

namespace mcdf {
namespace {

// Multicodec prefix for an ed25519 public key (varint 0xed01).
constexpr unsigned char kEd25519Prefix[] = {0xed, 0x01};

std::shared_ptr<EVP_PKEY> wrap(EVP_PKEY* p) {
  return std::shared_ptr<EVP_PKEY>(p, EVP_PKEY_free);
}

Result<std::string> raw_public_key(EVP_PKEY* pkey) {
  std::size_t len = 0;
  if (EVP_PKEY_get_raw_public_key(pkey, nullptr, &len) != 1) {
    return fail(ErrorCode::kUnsupported, "cannot read raw public key");
  }
  std::string raw(len, '\0');
  if (EVP_PKEY_get_raw_public_key(
          pkey, reinterpret_cast<unsigned char*>(raw.data()), &len) != 1) {
    return fail(ErrorCode::kUnsupported, "cannot read raw public key");
  }
  raw.resize(len);
  return raw;
}

Result<std::string> did_key_from_pkey(EVP_PKEY* pkey) {
  auto raw = raw_public_key(pkey);
  if (!raw) return std::unexpected(raw.error());
  std::string bytes(reinterpret_cast<const char*>(kEd25519Prefix),
                    sizeof(kEd25519Prefix));
  bytes += *raw;
  return "did:key:z" + base58btc_encode(bytes);
}

}  // namespace

Result<PrivateKey> PrivateKey::generate_ed25519() {
  EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
  if (!pkey) return fail(ErrorCode::kUnsupported, "ed25519 keygen failed");
  return PrivateKey(wrap(pkey));
}

Result<PrivateKey> PrivateKey::from_pem(std::string_view pem) {
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (!bio) return fail(ErrorCode::kIo, "bio alloc failed");
  EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!pkey) return fail(ErrorCode::kParse, "invalid private key PEM");
  return PrivateKey(wrap(pkey));
}

Result<std::string> PrivateKey::to_pem() const {
  BIO* bio = BIO_new(BIO_s_mem());
  if (!bio) return fail(ErrorCode::kIo, "bio alloc failed");
  if (PEM_write_bio_PrivateKey(bio, pkey_.get(), nullptr, nullptr, 0, nullptr,
                               nullptr) != 1) {
    BIO_free(bio);
    return fail(ErrorCode::kIo, "cannot serialize private key");
  }
  char* data = nullptr;
  const long len = BIO_get_mem_data(bio, &data);
  std::string pem(data, static_cast<std::size_t>(len));
  BIO_free(bio);
  return pem;
}

Result<std::string> PrivateKey::did_key() const {
  return did_key_from_pkey(pkey_.get());
}

Result<std::string> PrivateKey::sign(std::string_view data) const {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return fail(ErrorCode::kIo, "md ctx alloc failed");
  Result<std::string> result = fail(ErrorCode::kUnsupported, "sign failed");
  if (EVP_DigestSignInit(ctx, nullptr, nullptr, nullptr, pkey_.get()) == 1) {
    std::size_t sig_len = 0;
    const auto* msg = reinterpret_cast<const unsigned char*>(data.data());
    if (EVP_DigestSign(ctx, nullptr, &sig_len, msg, data.size()) == 1) {
      std::string sig(sig_len, '\0');
      if (EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()),
                         &sig_len, msg, data.size()) == 1) {
        sig.resize(sig_len);
        result = sig;
      }
    }
  }
  EVP_MD_CTX_free(ctx);
  return result;
}

Result<PublicKey> PublicKey::from_did_key(std::string_view did) {
  constexpr std::string_view kPrefix = "did:key:z";
  if (!did.starts_with(kPrefix)) {
    return fail(ErrorCode::kParse, "not a did:key: " + std::string(did));
  }
  auto decoded = base58btc_decode(did.substr(kPrefix.size()));
  if (!decoded) return std::unexpected(decoded.error());
  const std::string& bytes = *decoded;
  if (bytes.size() != sizeof(kEd25519Prefix) + 32 ||
      static_cast<unsigned char>(bytes[0]) != kEd25519Prefix[0] ||
      static_cast<unsigned char>(bytes[1]) != kEd25519Prefix[1]) {
    return fail(ErrorCode::kUnsupported,
                "did:key is not an ed25519 key");
  }
  const auto* raw =
      reinterpret_cast<const unsigned char*>(bytes.data() + sizeof(kEd25519Prefix));
  EVP_PKEY* pkey =
      EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, raw, 32);
  if (!pkey) return fail(ErrorCode::kParse, "cannot load ed25519 public key");
  return PublicKey(wrap(pkey));
}

Result<std::string> PublicKey::did_key() const {
  return did_key_from_pkey(pkey_.get());
}

Result<bool> PublicKey::verify(std::string_view data,
                               std::string_view signature) const {
  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return fail(ErrorCode::kIo, "md ctx alloc failed");
  bool valid = false;
  if (EVP_DigestVerifyInit(ctx, nullptr, nullptr, nullptr, pkey_.get()) == 1) {
    const int rc = EVP_DigestVerify(
        ctx, reinterpret_cast<const unsigned char*>(signature.data()),
        signature.size(), reinterpret_cast<const unsigned char*>(data.data()),
        data.size());
    valid = (rc == 1);
  }
  EVP_MD_CTX_free(ctx);
  return valid;
}

}  // namespace mcdf
