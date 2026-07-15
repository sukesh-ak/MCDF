// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/crypto/enc_keys.hpp"

#include <openssl/evp.h>
#include <openssl/hpke.h>
#include <openssl/pem.h>

#include <string>

#include "mcdf/crypto/encoding.hpp"

namespace mcdf {
namespace {

// Multicodec prefix for an x25519 public key (varint 0xec01).
constexpr unsigned char kX25519Prefix[] = {0xec, 0x01};

OSSL_HPKE_SUITE hpke_suite() {
  return OSSL_HPKE_SUITE{OSSL_HPKE_KEM_ID_X25519, OSSL_HPKE_KDF_ID_HKDF_SHA256,
                         OSSL_HPKE_AEAD_ID_AES_GCM_256};
}

std::shared_ptr<EVP_PKEY> wrap(EVP_PKEY* p) {
  return std::shared_ptr<EVP_PKEY>(p, EVP_PKEY_free);
}

const unsigned char* uc(std::string_view s) {
  return reinterpret_cast<const unsigned char*>(s.data());
}

std::string did_from_raw(std::string_view raw) {
  std::string bytes(reinterpret_cast<const char*>(kX25519Prefix),
                    sizeof(kX25519Prefix));
  bytes.append(raw);
  return "did:key:z" + base58btc_encode(bytes);
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

}  // namespace

Result<EncPublicKey> EncPublicKey::from_did_key(std::string_view did) {
  constexpr std::string_view kPrefix = "did:key:z";
  if (!did.starts_with(kPrefix)) {
    return fail(ErrorCode::kParse, "not a did:key: " + std::string(did));
  }
  auto decoded = base58btc_decode(did.substr(kPrefix.size()));
  if (!decoded) return std::unexpected(decoded.error());
  const std::string& bytes = *decoded;
  if (bytes.size() != sizeof(kX25519Prefix) + 32 ||
      static_cast<unsigned char>(bytes[0]) != kX25519Prefix[0] ||
      static_cast<unsigned char>(bytes[1]) != kX25519Prefix[1]) {
    return fail(ErrorCode::kUnsupported, "did:key is not an x25519 key");
  }
  return EncPublicKey(bytes.substr(sizeof(kX25519Prefix)));
}

EncPublicKey EncPublicKey::from_raw(std::string raw32) {
  return EncPublicKey(std::move(raw32));
}

Result<std::string> EncPublicKey::did_key() const { return did_from_raw(raw_); }

Result<HpkeSealed> hpke_seal(const EncPublicKey& recipient,
                             std::string_view info, std::string_view plaintext) {
  OSSL_HPKE_SUITE suite = hpke_suite();
  OSSL_HPKE_CTX* ctx = OSSL_HPKE_CTX_new(OSSL_HPKE_MODE_BASE, suite,
                                         OSSL_HPKE_ROLE_SENDER, nullptr, nullptr);
  if (!ctx) return fail(ErrorCode::kIo, "hpke ctx alloc failed");

  Result<HpkeSealed> result = fail(ErrorCode::kIo, "hpke seal failed");
  std::size_t enclen = OSSL_HPKE_get_public_encap_size(suite);
  std::size_t ctlen = OSSL_HPKE_get_ciphertext_size(suite, plaintext.size());
  std::string enc(enclen, '\0');
  std::string ct(ctlen, '\0');

  if (OSSL_HPKE_encap(ctx, reinterpret_cast<unsigned char*>(enc.data()), &enclen,
                      uc(recipient.raw()), recipient.raw().size(), uc(info),
                      info.size()) == 1 &&
      OSSL_HPKE_seal(ctx, reinterpret_cast<unsigned char*>(ct.data()), &ctlen,
                     nullptr, 0, uc(plaintext), plaintext.size()) == 1) {
    enc.resize(enclen);
    ct.resize(ctlen);
    result = HpkeSealed{enc, ct};
  }
  OSSL_HPKE_CTX_free(ctx);
  return result;
}

Result<EncPrivateKey> EncPrivateKey::generate_x25519() {
  EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "X25519");
  if (!pkey) return fail(ErrorCode::kUnsupported, "x25519 keygen failed");
  return EncPrivateKey(wrap(pkey));
}

Result<EncPrivateKey> EncPrivateKey::from_pem(std::string_view pem) {
  BIO* bio = BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size()));
  if (!bio) return fail(ErrorCode::kIo, "bio alloc failed");
  EVP_PKEY* pkey = PEM_read_bio_PrivateKey(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  if (!pkey) return fail(ErrorCode::kParse, "invalid private key PEM");
  return EncPrivateKey(wrap(pkey));
}

Result<std::string> EncPrivateKey::to_pem() const {
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

Result<std::string> EncPrivateKey::did_key() const {
  auto raw = raw_public_key(pkey_.get());
  if (!raw) return std::unexpected(raw.error());
  return did_from_raw(*raw);
}

Result<EncPublicKey> EncPrivateKey::public_key() const {
  auto raw = raw_public_key(pkey_.get());
  if (!raw) return std::unexpected(raw.error());
  return EncPublicKey::from_raw(*raw);
}

Result<std::string> EncPrivateKey::hpke_open(std::string_view enc,
                                             std::string_view ct,
                                             std::string_view info) const {
  OSSL_HPKE_SUITE suite = hpke_suite();
  OSSL_HPKE_CTX* ctx = OSSL_HPKE_CTX_new(
      OSSL_HPKE_MODE_BASE, suite, OSSL_HPKE_ROLE_RECEIVER, nullptr, nullptr);
  if (!ctx) return fail(ErrorCode::kIo, "hpke ctx alloc failed");

  Result<std::string> result = fail(ErrorCode::kParse, "hpke open failed");
  std::string pt(ct.size(), '\0');
  std::size_t ptlen = pt.size();

  if (OSSL_HPKE_decap(ctx, uc(enc), enc.size(), pkey_.get(), uc(info),
                      info.size()) == 1 &&
      OSSL_HPKE_open(ctx, reinterpret_cast<unsigned char*>(pt.data()), &ptlen,
                     nullptr, 0, uc(ct), ct.size()) == 1) {
    pt.resize(ptlen);
    result = pt;
  }
  OSSL_HPKE_CTX_free(ctx);
  return result;
}

}  // namespace mcdf
