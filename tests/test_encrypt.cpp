// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
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

  // Verifiable WITHOUT the key: the manifest hashes ciphertext, and schema
  // binding is evaluated against the structure attestation the encryptor left
  // behind (spec 5.2.1), so an encrypted document is still fully valid.
  {
    auto doc = mcdf::load_document(**container);
    REQUIRE(doc.has_value());
    auto integrity = mcdf::verify_manifest(**container, doc->manifest);
    REQUIRE(integrity.has_value());
    CHECK(integrity->ok);

    auto report = mcdf::validate(**container, *doc, mcdf::Profile::kCore);
    REQUIRE(report.has_value());
    CHECK(report->ok);
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

// --- Structure attestation (spec 5.2.1) -------------------------------------
//
// The gap this closes: before the attestation existed, sealing content.md made
// every schema section unbindable, so a required section was reported missing
// when it was only unreadable. Encrypting a document cost it its conformance.

TEST_CASE("encrypting records which sections bound, so validation still works") {
  const fs::path dir = temp_copy("attest");
  auto container = mcdf::DirectoryContainer::open(dir);
  REQUIRE(container.has_value());

  auto key = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(key.has_value());
  auto pub = key->public_key();
  REQUIRE(pub.has_value());
  REQUIRE(mcdf::encrypt_container(**container, {"content.md"}, {*pub}).has_value());

  auto raw = (*container)->read("encryption/policy.yaml");
  REQUIRE(raw.has_value());
  auto policy = mcdf::parse_encryption_policy_yaml(*raw);
  REQUIRE(policy.has_value());
  CHECK(policy->structure.present);
  // The fixture's schema declares overview and terms, and content.md anchors
  // both; the attestation lists them in schema order.
  CHECK(policy->structure.bound_sections == std::vector<std::string>{"overview", "terms"});

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("a sealed content.md is not parsed as Markdown") {
  const fs::path dir = temp_copy("noparse");
  auto container = mcdf::DirectoryContainer::open(dir);
  REQUIRE(container.has_value());

  auto key = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(key.has_value());
  auto pub = key->public_key();
  REQUIRE(pub.has_value());
  REQUIRE(mcdf::encrypt_container(**container, {"content.md"}, {*pub}).has_value());

  auto doc = mcdf::load_document(**container);
  REQUIRE(doc.has_value());
  CHECK(doc->is_sealed("content.md"));
  CHECK(doc->has_content);          // the member is there...
  CHECK(doc->headings.empty());     // ...but nothing in it is readable

  // Rendering ciphertext would produce confident nonsense rather than an error,
  // so it must refuse outright (spec 6).
  CHECK_FALSE(mcdf::render(**container, mcdf::RenderFormat::kHtml).has_value());

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("a false attestation is caught by the first recipient who opens it") {
  const fs::path dir = temp_copy("liar");
  auto container = mcdf::DirectoryContainer::open(dir);
  REQUIRE(container.has_value());

  auto key = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(key.has_value());
  auto pub = key->public_key();
  REQUIRE(pub.has_value());
  REQUIRE(mcdf::encrypt_container(**container, {"content.md"}, {*pub}).has_value());

  // Rewrite the attestation to claim a section that never bound. While sealed
  // the claim is believed - that is the honest limit of an attestation - but it
  // does not survive decryption.
  auto raw = (*container)->read("encryption/policy.yaml");
  REQUIRE(raw.has_value());
  auto policy = mcdf::parse_encryption_policy_yaml(*raw);
  REQUIRE(policy.has_value());
  policy->structure.bound_sections = {"overview", "terms", "appendix"};
  REQUIRE((*container)
              ->write("encryption/policy.yaml",
                      mcdf::encryption_policy_to_yaml(*policy))
              .has_value());

  auto opened = mcdf::decrypt_container(**container, *key);
  REQUIRE_FALSE(opened.has_value());
  CHECK(opened.error().message.find("E_STRUCTURE_ATTESTATION_INVALID") !=
        std::string::npos);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("an encrypted document with no attestation cannot be structure-checked") {
  const fs::path dir = temp_copy("noattest");
  auto container = mcdf::DirectoryContainer::open(dir);
  REQUIRE(container.has_value());

  auto key = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(key.has_value());
  auto pub = key->public_key();
  REQUIRE(pub.has_value());
  REQUIRE(mcdf::encrypt_container(**container, {"content.md"}, {*pub}).has_value());

  auto raw = (*container)->read("encryption/policy.yaml");
  REQUIRE(raw.has_value());
  auto policy = mcdf::parse_encryption_policy_yaml(*raw);
  REQUIRE(policy.has_value());
  policy->structure.present = false;
  REQUIRE((*container)
              ->write("encryption/policy.yaml",
                      mcdf::encryption_policy_to_yaml(*policy))
              .has_value());

  auto doc = mcdf::load_document(**container);
  REQUIRE(doc.has_value());
  auto report = mcdf::validate(**container, *doc, mcdf::Profile::kCore);
  REQUIRE(report.has_value());
  REQUIRE_FALSE(report->ok);
  // Once, naming the encryptor's omission - not once per section, which would
  // blame the document for something that may well be fine.
  REQUIRE(report->issues.size() == 1);
  CHECK(report->issues[0].code == "E_POLICY_INVALID");

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("sealing schema.yaml suppresses the attestation rather than leaking ids") {
  const fs::path dir = temp_copy("sealedschema");
  auto container = mcdf::DirectoryContainer::open(dir);
  REQUIRE(container.has_value());

  auto key = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(key.has_value());
  auto pub = key->public_key();
  REQUIRE(pub.has_value());
  REQUIRE(mcdf::encrypt_container(**container, {"content.md", "schema.yaml"}, {*pub})
              .has_value());

  auto raw = (*container)->read("encryption/policy.yaml");
  REQUIRE(raw.has_value());
  CHECK(raw->find("structure:") == std::string::npos);
  // The ids are only safe to publish because schema.yaml publishes them.
  CHECK(raw->find("overview") == std::string::npos);

  // A schema nobody can read makes no structural claims, so Core passes.
  auto doc = mcdf::load_document(**container);
  REQUIRE(doc.has_value());
  CHECK_FALSE(doc->has_schema);
  auto report = mcdf::validate(**container, *doc, mcdf::Profile::kCore);
  REQUIRE(report.has_value());
  CHECK(report->ok);

  std::error_code ec;
  fs::remove_all(dir, ec);
}
