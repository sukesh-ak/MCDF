// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/crypto/keys.hpp"

#include <openssl/bn.h>
#include <openssl/core_names.h>
#include <openssl/ec.h>
#include <openssl/evp.h>
#include <openssl/params.h>
#include <openssl/pem.h>

#include <string>

#include "mcdf/crypto/encoding.hpp"

namespace mcdf {
namespace {

// Multicodec prefixes (varint): ed25519-pub = 0xed01, p256-pub = 0x1200.
constexpr unsigned char kEd25519Prefix[] = {0xed, 0x01};
constexpr unsigned char kP256Prefix[] = {0x80, 0x24};

enum class KeyType { kEd25519, kEcP256, kOther };

std::shared_ptr<EVP_PKEY> wrap(EVP_PKEY* p) {
  return std::shared_ptr<EVP_PKEY>(p, EVP_PKEY_free);
}

KeyType key_type(EVP_PKEY* pkey) {
  const int id = EVP_PKEY_get_base_id(pkey);
  if (id == EVP_PKEY_ED25519) return KeyType::kEd25519;
  if (id == EVP_PKEY_EC) {
    char name[80] = {0};
    std::size_t len = 0;
    if (EVP_PKEY_get_utf8_string_param(pkey, OSSL_PKEY_PARAM_GROUP_NAME, name,
                                       sizeof(name), &len) == 1) {
      const std::string group(name, len);
      if (group == "prime256v1" || group == "P-256") return KeyType::kEcP256;
    }
  }
  return KeyType::kOther;
}

std::string jws_alg(EVP_PKEY* pkey) {
  switch (key_type(pkey)) {
    case KeyType::kEd25519: return "EdDSA";
    case KeyType::kEcP256:  return "ES256";
    default:                return "";
  }
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

// The SEC1-compressed (33-byte) P-256 public key, derived by compressing the
// uncompressed point (0x04 || X || Y) via Y's parity.
Result<std::string> ec_compressed_pub(EVP_PKEY* pkey) {
  std::size_t len = 0;
  if (EVP_PKEY_get_octet_string_param(pkey, OSSL_PKEY_PARAM_PUB_KEY, nullptr, 0,
                                      &len) != 1) {
    return fail(ErrorCode::kUnsupported, "cannot read EC public key");
  }
  std::string point(len, '\0');
  if (EVP_PKEY_get_octet_string_param(
          pkey, OSSL_PKEY_PARAM_PUB_KEY,
          reinterpret_cast<unsigned char*>(point.data()), len, &len) != 1) {
    return fail(ErrorCode::kUnsupported, "cannot read EC public key");
  }
  point.resize(len);
  if (point.size() != 65 || static_cast<unsigned char>(point[0]) != 0x04) {
    return fail(ErrorCode::kUnsupported, "unexpected EC point encoding");
  }
  std::string compressed;
  compressed.push_back((static_cast<unsigned char>(point[64]) & 1) ? 0x03 : 0x02);
  compressed.append(point, 1, 32);
  return compressed;
}

Result<std::shared_ptr<EVP_PKEY>> ec_pub_from_compressed(std::string_view comp) {
  OSSL_PARAM params[] = {
      OSSL_PARAM_construct_utf8_string(
          OSSL_PKEY_PARAM_GROUP_NAME, const_cast<char*>("prime256v1"), 0),
      OSSL_PARAM_construct_octet_string(
          OSSL_PKEY_PARAM_PUB_KEY,
          const_cast<char*>(comp.data()), comp.size()),
      OSSL_PARAM_construct_end()};
  EVP_PKEY_CTX* ctx = EVP_PKEY_CTX_new_from_name(nullptr, "EC", nullptr);
  EVP_PKEY* pkey = nullptr;
  const bool ok = ctx && EVP_PKEY_fromdata_init(ctx) == 1 &&
                  EVP_PKEY_fromdata(ctx, &pkey, EVP_PKEY_PUBLIC_KEY, params) == 1;
  EVP_PKEY_CTX_free(ctx);
  if (!ok) return fail(ErrorCode::kParse, "cannot load P-256 public key");
  return wrap(pkey);
}

Result<std::string> did_key_from_pkey(EVP_PKEY* pkey) {
  const KeyType type = key_type(pkey);
  if (type == KeyType::kEd25519) {
    auto raw = raw_public_key(pkey);
    if (!raw) return std::unexpected(raw.error());
    std::string bytes(reinterpret_cast<const char*>(kEd25519Prefix),
                      sizeof(kEd25519Prefix));
    bytes += *raw;
    return "did:key:z" + base58btc_encode(bytes);
  }
  if (type == KeyType::kEcP256) {
    auto comp = ec_compressed_pub(pkey);
    if (!comp) return std::unexpected(comp.error());
    std::string bytes(reinterpret_cast<const char*>(kP256Prefix),
                      sizeof(kP256Prefix));
    bytes += *comp;
    return "did:key:z" + base58btc_encode(bytes);
  }
  return fail(ErrorCode::kUnsupported, "unsupported key type for did:key");
}

// DER ECDSA-Sig-Value -> raw R||S (64 bytes) for JWS ES256.
std::string ecdsa_der_to_raw(std::string_view der) {
  const unsigned char* p = reinterpret_cast<const unsigned char*>(der.data());
  ECDSA_SIG* sig = d2i_ECDSA_SIG(nullptr, &p, static_cast<long>(der.size()));
  if (!sig) return {};
  const BIGNUM* r = nullptr;
  const BIGNUM* s = nullptr;
  ECDSA_SIG_get0(sig, &r, &s);
  std::string raw(64, '\0');
  BN_bn2binpad(r, reinterpret_cast<unsigned char*>(raw.data()), 32);
  BN_bn2binpad(s, reinterpret_cast<unsigned char*>(raw.data()) + 32, 32);
  ECDSA_SIG_free(sig);
  return raw;
}

// Raw R||S (64 bytes) -> DER ECDSA-Sig-Value for OpenSSL verification.
std::string ecdsa_raw_to_der(std::string_view raw) {
  if (raw.size() != 64) return {};
  const auto* bytes = reinterpret_cast<const unsigned char*>(raw.data());
  BIGNUM* r = BN_bin2bn(bytes, 32, nullptr);
  BIGNUM* s = BN_bin2bn(bytes + 32, 32, nullptr);
  ECDSA_SIG* sig = ECDSA_SIG_new();
  ECDSA_SIG_set0(sig, r, s);  // takes ownership of r and s
  int len = i2d_ECDSA_SIG(sig, nullptr);
  std::string der(static_cast<std::size_t>(len), '\0');
  unsigned char* out = reinterpret_cast<unsigned char*>(der.data());
  i2d_ECDSA_SIG(sig, &out);
  ECDSA_SIG_free(sig);
  return der;
}

}  // namespace

Result<PrivateKey> PrivateKey::generate_ed25519() {
  EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "ED25519");
  if (!pkey) return fail(ErrorCode::kUnsupported, "ed25519 keygen failed");
  return PrivateKey(wrap(pkey));
}

Result<PrivateKey> PrivateKey::generate_ecdsa_p256() {
  EVP_PKEY* pkey = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
  if (!pkey) return fail(ErrorCode::kUnsupported, "ecdsa p-256 keygen failed");
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

std::string PrivateKey::jws_algorithm() const { return jws_alg(pkey_.get()); }

Result<std::string> PrivateKey::sign(std::string_view data) const {
  const KeyType type = key_type(pkey_.get());
  const EVP_MD* md = (type == KeyType::kEd25519) ? nullptr : EVP_sha256();

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return fail(ErrorCode::kIo, "md ctx alloc failed");

  Result<std::string> result = fail(ErrorCode::kUnsupported, "sign failed");
  const auto* msg = reinterpret_cast<const unsigned char*>(data.data());
  if (EVP_DigestSignInit(ctx, nullptr, md, nullptr, pkey_.get()) == 1) {
    std::size_t sig_len = 0;
    if (EVP_DigestSign(ctx, nullptr, &sig_len, msg, data.size()) == 1) {
      std::string sig(sig_len, '\0');
      if (EVP_DigestSign(ctx, reinterpret_cast<unsigned char*>(sig.data()),
                         &sig_len, msg, data.size()) == 1) {
        sig.resize(sig_len);
        if (type == KeyType::kEcP256) {
          std::string raw = ecdsa_der_to_raw(sig);
          if (!raw.empty()) result = raw;
        } else {
          result = sig;
        }
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

  if (bytes.size() == sizeof(kEd25519Prefix) + 32 &&
      static_cast<unsigned char>(bytes[0]) == kEd25519Prefix[0] &&
      static_cast<unsigned char>(bytes[1]) == kEd25519Prefix[1]) {
    const auto* raw = reinterpret_cast<const unsigned char*>(
        bytes.data() + sizeof(kEd25519Prefix));
    EVP_PKEY* pkey =
        EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, raw, 32);
    if (!pkey) return fail(ErrorCode::kParse, "cannot load ed25519 public key");
    return PublicKey(wrap(pkey));
  }

  if (bytes.size() == sizeof(kP256Prefix) + 33 &&
      static_cast<unsigned char>(bytes[0]) == kP256Prefix[0] &&
      static_cast<unsigned char>(bytes[1]) == kP256Prefix[1]) {
    auto pkey = ec_pub_from_compressed(
        std::string_view(bytes).substr(sizeof(kP256Prefix)));
    if (!pkey) return std::unexpected(pkey.error());
    return PublicKey(*pkey);
  }

  return fail(ErrorCode::kUnsupported, "unsupported did:key type");
}

Result<std::string> PublicKey::did_key() const {
  return did_key_from_pkey(pkey_.get());
}

std::string PublicKey::jws_algorithm() const { return jws_alg(pkey_.get()); }

Result<bool> PublicKey::verify(std::string_view data,
                               std::string_view signature) const {
  const KeyType type = key_type(pkey_.get());
  const EVP_MD* md = (type == KeyType::kEd25519) ? nullptr : EVP_sha256();

  // ES256 signatures arrive as raw R||S; OpenSSL verifies DER.
  std::string der;
  std::string_view sig = signature;
  if (type == KeyType::kEcP256) {
    der = ecdsa_raw_to_der(signature);
    if (der.empty()) return false;
    sig = der;
  }

  EVP_MD_CTX* ctx = EVP_MD_CTX_new();
  if (!ctx) return fail(ErrorCode::kIo, "md ctx alloc failed");
  bool valid = false;
  if (EVP_DigestVerifyInit(ctx, nullptr, md, nullptr, pkey_.get()) == 1) {
    const int rc = EVP_DigestVerify(
        ctx, reinterpret_cast<const unsigned char*>(sig.data()), sig.size(),
        reinterpret_cast<const unsigned char*>(data.data()), data.size());
    valid = (rc == 1);
  }
  EVP_MD_CTX_free(ctx);
  return valid;
}

}  // namespace mcdf
