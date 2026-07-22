// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
std::string example_path() {
  return std::string(MCDF_TEST_FIXTURES) + "/example.mcdf";
}

bool has_code(const mcdf::ValidationReport& r, std::string_view code) {
  for (const auto& i : r.issues)
    if (i.code == code) return true;
  return false;
}

// A scratch container with content.md + a fresh manifest.json.
struct TempContainer {
  fs::path dir;
  TempContainer(const char* name, std::string_view content) {
    static int n = 0;
    dir = fs::temp_directory_path() / "mcdf-validate-tests" /
          (std::string(name) + "-" + std::to_string(++n));
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    auto d = mcdf::DirectoryContainer::open(dir);
    REQUIRE(d.has_value());
    REQUIRE((*d)->write("content.md", content).has_value());
    rebuild_manifest();
  }
  ~TempContainer() {
    std::error_code ec;
    fs::remove_all(dir, ec);
  }
  void rebuild_manifest() {
    auto d = mcdf::DirectoryContainer::open(dir);
    REQUIRE(d.has_value());
    auto m = mcdf::build_manifest(**d);
    REQUIRE(m.has_value());
    auto j = mcdf::manifest_to_canonical_json(*m);
    REQUIRE(j.has_value());
    REQUIRE((*d)->write("manifest.json", *j).has_value());
  }
  void sign(const mcdf::PrivateKey& key) {
    auto d = mcdf::DirectoryContainer::open(dir);
    REQUIRE(d.has_value());
    auto kid = key.did_key();
    REQUIRE(kid.has_value());
    auto jws = mcdf::sign_container(**d, key, *kid);
    REQUIRE(jws.has_value());
    REQUIRE((*d)->write("signatures/author.sig", *jws).has_value());
  }
  mcdf::ValidationReport validate(mcdf::Profile p) {
    auto d = mcdf::DirectoryContainer::open(dir);
    REQUIRE(d.has_value());
    auto doc = mcdf::load_document(**d);
    REQUIRE(doc.has_value());
    auto r = mcdf::validate(**d, *doc, p);
    REQUIRE(r.has_value());
    return *r;
  }
};
}  // namespace

TEST_CASE("core validation passes for a well-formed container") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());
  auto doc = mcdf::load_document(**c);
  REQUIRE(doc.has_value());

  auto report = mcdf::validate(**c, *doc, mcdf::Profile::kCore);
  REQUIRE(report.has_value());
  CHECK(report->ok);
}

TEST_CASE("integrity validation passes for the valid fixture") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());
  auto doc = mcdf::load_document(**c);
  REQUIRE(doc.has_value());

  auto report = mcdf::validate(**c, *doc, mcdf::Profile::kIntegrity);
  REQUIRE(report.has_value());
  CHECK(report->ok);
}

TEST_CASE("integrity validation catches a tampered manifest") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());
  auto doc = mcdf::load_document(**c);
  REQUIRE(doc.has_value());

  // Corrupt the loaded manifest so it disagrees with the real file bytes.
  doc->manifest.files["content.md"] = std::string(64, '0');

  auto report = mcdf::validate(**c, *doc, mcdf::Profile::kIntegrity);
  REQUIRE(report.has_value());
  CHECK_FALSE(report->ok);
  CHECK(has_code(*report, "E_MANIFEST_HASH_MISMATCH"));
}

TEST_CASE("a required section with no matching heading is reported") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());

  mcdf::Document doc;
  doc.has_content = true;
  doc.headings.push_back({1, "Only Heading", "present"});
  doc.has_schema = true;
  doc.schema.document_type = "contract";
  doc.schema.sections.push_back({"absent", "Missing Section", true});

  auto report = mcdf::validate(**c, doc, mcdf::Profile::kCore);
  REQUIRE(report.has_value());
  CHECK_FALSE(report->ok);
  CHECK(has_code(*report, "E_REQUIRED_SECTION_MISSING"));
}

TEST_CASE("parse_profile round-trips names") {
  auto p = mcdf::parse_profile("integrity");
  REQUIRE(p.has_value());
  CHECK(mcdf::to_string(*p) == "integrity");
  CHECK_FALSE(mcdf::parse_profile("bogus").has_value());
}

TEST_CASE("signed profile requires at least one valid signature") {
  TempContainer t("signed", "# Signed\n");
  auto unsigned_report = t.validate(mcdf::Profile::kSigned);
  CHECK_FALSE(unsigned_report.ok);
  CHECK(has_code(unsigned_report, "E_SIG_MISSING"));

  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  t.sign(*key);
  CHECK(t.validate(mcdf::Profile::kSigned).ok);
}

TEST_CASE("signed profile accepts an ES256 signature") {
  TempContainer t("es256", "# ES256\n");
  auto key = mcdf::PrivateKey::generate_ecdsa_p256();
  REQUIRE(key.has_value());
  t.sign(*key);
  auto report = t.validate(mcdf::Profile::kSigned);
  CHECK(report.ok);
}

TEST_CASE("signed profile flags a signature broken by a manifest rebuild") {
  TempContainer t("stale-sig", "# v1\n");
  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  t.sign(*key);
  REQUIRE(t.validate(mcdf::Profile::kSigned).ok);

  // Edit + rebuild: integrity is restored but the signature is now stale.
  {
    auto d = mcdf::DirectoryContainer::open(t.dir);
    REQUIRE(d.has_value());
    REQUIRE((*d)->write("content.md", "# v2\n").has_value());
  }
  t.rebuild_manifest();
  auto report = t.validate(mcdf::Profile::kSigned);
  CHECK_FALSE(report.ok);
  CHECK(has_code(report, "E_SIG_INVALID"));
}

TEST_CASE("encrypted profile passes for a well-formed encrypted container") {
  TempContainer t("encrypted", "# Secret\n");
  auto ek = mcdf::EncPrivateKey::generate_x25519();
  REQUIRE(ek.has_value());
  auto pub = ek->public_key();
  REQUIRE(pub.has_value());
  {
    auto d = mcdf::DirectoryContainer::open(t.dir);
    REQUIRE(d.has_value());
    REQUIRE(mcdf::encrypt_container(**d, {"content.md"}, {*pub}).has_value());
  }
  // Sign over the ciphertext manifest so the superset chain holds.
  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  t.sign(*key);
  CHECK(t.validate(mcdf::Profile::kEncrypted).ok);
  // An unencrypted container trivially passes the encrypted checks too.
  TempContainer plain("plain", "# Plain\n");
  auto key2 = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key2.has_value());
  plain.sign(*key2);
  CHECK(plain.validate(mcdf::Profile::kEncrypted).ok);
}

TEST_CASE("encrypted profile rejects an unsound policy") {
  TempContainer t("bad-policy", "# Doc\n");
  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());

  mcdf::EncryptionPolicy p;
  p.method = "rot13";  // not on the allow-list
  p.key_management = "hpke";
  p.encrypted_files = {"content.md", "ghost.bin"};  // ghost.bin absent
  p.recipients.push_back({"not-a-did", "enc", "wrapped"});
  {
    auto d = mcdf::DirectoryContainer::open(t.dir);
    REQUIRE(d.has_value());
    REQUIRE((*d)
                ->write("encryption/policy.yaml",
                        mcdf::encryption_policy_to_yaml(p))
                .has_value());
  }
  t.rebuild_manifest();
  t.sign(*key);

  auto report = t.validate(mcdf::Profile::kEncrypted);
  CHECK_FALSE(report.ok);
  CHECK(has_code(report, "E_ALGO_NOT_ALLOWED"));
  CHECK(has_code(report, "E_POLICY_INVALID"));
  CHECK(has_code(report, "E_KID_UNRESOLVABLE"));
}

TEST_CASE("render profile passes the full ladder for a signed container") {
  TempContainer t("render", "# Render me\n\nSome *text*.\n");
  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  t.sign(*key);
  auto report = t.validate(mcdf::Profile::kRender);
  CHECK(report.ok);
}
