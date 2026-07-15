// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {
fs::path example_path() { return fs::path(MCDF_TEST_FIXTURES) / "example.mcdf"; }

fs::path temp_copy(const std::string& tag) {
  const fs::path dst = fs::temp_directory_path() / ("mcdf_enc_" + tag);
  std::error_code ec;
  fs::remove_all(dst, ec);
  fs::create_directories(dst, ec);
  fs::copy(example_path(), dst, fs::copy_options::recursive, ec);
  return dst;
}
}  // namespace

TEST_CASE("AES-256-GCM seals and opens; tampering fails") {
  const std::string key = mcdf::random_bytes(32);
  auto sealed = mcdf::aes256gcm_seal(key, "aad", "secret message");
  REQUIRE(sealed.has_value());

  auto opened = mcdf::aes256gcm_open(key, "aad", *sealed);
  REQUIRE(opened.has_value());
  CHECK(*opened == "secret message");

  CHECK_FALSE(mcdf::aes256gcm_open(key, "different-aad", *sealed).has_value());
  std::string bad = *sealed;
  bad.back() ^= 0xFF;
  CHECK_FALSE(mcdf::aes256gcm_open(key, "aad", bad).has_value());
}

TEST_CASE("x25519 did:key round-trips and HPKE seals to it") {
  auto sk = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(sk.has_value());
  auto did = sk->did_key();
  REQUIRE(did.has_value());
  CHECK(did->starts_with("did:key:z6LS"));  // x25519 multicodec prefix

  auto pk = mcdf::EncPublicKey::from_did_key(*did);
  REQUIRE(pk.has_value());

  auto sealed = mcdf::hpke_seal(*pk, "info", "content key");
  REQUIRE(sealed.has_value());
  auto opened = sk->hpke_open(sealed->enc, sealed->ct, "info");
  REQUIRE(opened.has_value());
  CHECK(*opened == "content key");
}

TEST_CASE("HPKE open fails with the wrong key") {
  auto a = mcdf::EncPrivateKey::generate_x25519();
  auto b = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  auto pk = a->public_key();
  REQUIRE(pk.has_value());
  auto sealed = mcdf::hpke_seal(*pk, "info", "msg");
  REQUIRE(sealed.has_value());
  CHECK_FALSE(b->hpke_open(sealed->enc, sealed->ct, "info").has_value());
}

TEST_CASE("encrypt then decrypt round-trips; integrity holds without the key") {
  const fs::path dir = temp_copy("roundtrip");
  auto container = mcdf::DirectoryContainer::open(dir);
  REQUIRE(container.has_value());

  const std::string plaintext = *(*container)->read("content.md");

  auto key = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(key.has_value());
  auto pub = key->public_key();
  REQUIRE(pub.has_value());

  REQUIRE(mcdf::encrypt_container(**container, {"content.md"}, {*pub}).has_value());

  // content.md is now ciphertext, and the policy file exists.
  CHECK(*(*container)->read("content.md") != plaintext);
  CHECK((*container)->contains("encryption/policy.yaml"));

  // Manifest integrity is verifiable WITHOUT the key (it hashes ciphertext).
  // Full schema-binding validation needs plaintext, so we check the manifest
  // layer directly - which is exactly what `mcdf verify` does.
  {
    auto doc = mcdf::load_document(**container);
    REQUIRE(doc.has_value());
    auto integrity = mcdf::verify_manifest(**container, doc->manifest);
    REQUIRE(integrity.has_value());
    CHECK(integrity->ok);
  }

  // Decrypt restores the plaintext and removes the policy.
  REQUIRE(mcdf::decrypt_container(**container, *key).has_value());
  CHECK(*(*container)->read("content.md") == plaintext);
  CHECK_FALSE((*container)->contains("encryption/policy.yaml"));

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("decrypt fails for a non-recipient key") {
  const fs::path dir = temp_copy("wrongkey");
  auto container = mcdf::DirectoryContainer::open(dir);
  REQUIRE(container.has_value());

  auto recipient = mcdf::EncPrivateKey::generate_x25519();
  auto other = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(recipient.has_value());
  REQUIRE(other.has_value());
  auto pub = recipient->public_key();
  REQUIRE(pub.has_value());

  REQUIRE(mcdf::encrypt_container(**container, {"content.md"}, {*pub}).has_value());
  CHECK_FALSE(mcdf::decrypt_container(**container, *other).has_value());

  std::error_code ec;
  fs::remove_all(dir, ec);
}
