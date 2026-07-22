// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// Headless tests over Studio's document engine (plan 04 §10): the same
// open -> edit -> rebuild -> sign -> verify flows the panels drive, asserted
// against direct libmcdf calls so Studio and the CLI can never disagree.
#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include <filesystem>
#include <string>
#include <vector>

#include "core/studio_core.hpp"

namespace fs = std::filesystem;

namespace {

// A keystore in a throwaway directory with one Ed25519 + one X25519 key.
studio::Keystore test_keystore() {
  studio::Keystore ks;
  ks.dir = studio::make_workdir();
  studio::generate_signing_key(ks);
  studio::generate_enc_key(ks);
  return ks;
}

void drop(studio::Keystore& ks) {
  std::error_code ec;
  fs::remove_all(ks.dir, ec);
}

void drop(studio::Doc& d) { studio::cleanup(d); }

}  // namespace

TEST_CASE("blank -> edit -> save as -> reopen round-trips content") {
  studio::Doc d;
  studio::init_blank(d);
  REQUIRE(d.has_content);
  CHECK(d.path.empty());
  CHECK(d.has_manifest);  // seeded so the first save is well-formed

  const std::string text = "# Title\n\nbody text\n";
  CHECK(studio::save(d, text) == studio::SaveResult::kNeedsPath);  // untitled

  const fs::path out = studio::make_workdir() / "doc.mcdf";
  REQUIRE(studio::save_as(d, text, out.string()));
  CHECK(d.is_archive);
  CHECK(d.archive_path == out.string());
  CHECK(d.content == text);  // reloaded from the new archive

  studio::Doc e;
  studio::open_archive_into(e, out.string());
  CHECK(e.has_content);
  CHECK(e.content == text);
  CHECK(e.has_manifest);

  drop(d);
  drop(e);
  std::error_code ec;
  fs::remove_all(out.parent_path(), ec);
}

TEST_CASE("saved archive bytes are engine-identical (determinism)") {
  studio::Doc d;
  studio::init_blank(d);
  const std::string text = "# Deterministic\n";
  const fs::path out = studio::make_workdir() / "det.mcdf";
  REQUIRE(studio::save_as(d, text, out.string()));

  bool ok = false;
  const std::string studio_bytes = studio::read_file_bytes(out.string(), ok);
  REQUIRE(ok);

  auto c = mcdf::open_container(d.workdir);
  REQUIRE(c.has_value());
  auto engine_bytes = mcdf::pack_container(**c);
  REQUIRE(engine_bytes.has_value());
  CHECK(studio_bytes == *engine_bytes);

  drop(d);
  std::error_code ec;
  fs::remove_all(out.parent_path(), ec);
}

TEST_CASE("drift -> rebuild -> sign state machine matches the engine") {
  studio::Keystore ks = test_keystore();
  REQUIRE(ks.keys.size() == 1);
  const studio::SigningKey& key = ks.keys[0];

  studio::Doc d;
  studio::init_blank(d);
  const std::string v1 = "# v1\n";
  REQUIRE(studio::flush_working_copy(d, v1, /*ensure_manifest=*/true));

  // Sign: green (crypto-valid, no drift).
  REQUIRE(studio::sign_document(d, v1, key, "author"));
  studio::refresh_signatures(d);
  REQUIRE(d.sigs.size() == 1);
  CHECK(d.sigs[0].valid);
  CHECK(d.sigs[0].kid == key.did);
  CHECK(d.sigs[0].alg == "EdDSA");
  studio::refresh_disk_hashes(d);
  studio::update_live_content_hash(d, v1);
  CHECK_FALSE(studio::manifest_drift(d).any());

  // Type one character: drift (the amber dot / red trust chip)...
  const std::string v2 = "# v2\n";
  studio::update_live_content_hash(d, v2);
  CHECK(studio::manifest_drift(d).modified == 1);
  // ...while the signature over the stored manifest is still crypto-valid.
  studio::refresh_signatures(d);
  CHECK(d.sigs[0].valid);

  // Rebuild: drift clears, but the manifest changed under the signature.
  REQUIRE(studio::rebuild_manifest(d, v2));
  studio::refresh_disk_hashes(d);
  studio::update_live_content_hash(d, v2);
  CHECK_FALSE(studio::manifest_drift(d).any());
  studio::refresh_signatures(d);
  REQUIRE(d.sigs.size() == 1);
  CHECK_FALSE(d.sigs[0].valid);

  // Re-sign: green again - and the engine agrees byte-for-byte.
  REQUIRE(studio::sign_document(d, v2, key, "author"));
  studio::refresh_signatures(d);
  CHECK(d.sigs[0].valid);
  auto c = mcdf::open_container(d.workdir);
  REQUIRE(c.has_value());
  auto engine = mcdf::verify_container(**c);
  REQUIRE(engine.has_value());
  REQUIRE(engine->size() == d.sigs.size());
  CHECK((*engine)[0].valid == d.sigs[0].valid);
  CHECK((*engine)[0].kid == d.sigs[0].kid);

  drop(d);
  drop(ks);
}

TEST_CASE("metadata/schema forms write parser-identical members") {
  studio::Doc d;
  studio::init_blank(d);
  d.meta.title = "Studio: A \"Test\"";
  d.meta.authors.push_back({"Ada", "did:key:z6MkTest"});
  d.write_meta = true;
  d.schema.document_type = "note";
  d.schema.sections.push_back({"intro", "Introduction", true});
  d.write_schema = true;

  REQUIRE(studio::flush_working_copy(d, "# Body\n", true));
  auto c = mcdf::open_container(d.workdir);
  REQUIRE(c.has_value());
  auto meta_raw = (*c)->read("metadata.yaml");
  REQUIRE(meta_raw.has_value());
  auto meta = mcdf::parse_metadata_yaml(*meta_raw);
  REQUIRE(meta.has_value());
  CHECK(meta->title == d.meta.title);
  REQUIRE(meta->authors.size() == 1);
  CHECK(meta->authors[0].id == "did:key:z6MkTest");
  auto schema_raw = (*c)->read("schema.yaml");
  REQUIRE(schema_raw.has_value());
  auto schema = mcdf::parse_schema_yaml(*schema_raw);
  REQUIRE(schema.has_value());
  CHECK(schema->document_type == "note");
  REQUIRE(schema->sections.size() == 1);
  CHECK(schema->sections[0].required);
  // The manifest covers the new members.
  CHECK(d.manifest.files.contains("metadata.yaml"));
  CHECK(d.manifest.files.contains("schema.yaml"));

  drop(d);
}

TEST_CASE("encrypt locks content and the save path never clobbers ciphertext") {
  studio::Keystore ks = test_keystore();
  REQUIRE(ks.enc_keys.size() == 1);
  const studio::EncKeyEntry& ek = ks.enc_keys[0];

  studio::Doc d;
  studio::init_blank(d);
  const std::string secret = "# Secret\n\ntop secret body\n";
  REQUIRE(studio::flush_working_copy(d, secret, true));

  REQUIRE(studio::encrypt_document(d, secret, {ek.did}, {"content.md"}));
  REQUIRE(d.policy.has_value());
  CHECK(d.content_encrypted);
  CHECK(d.content != secret);  // ciphertext on disk now
  REQUIRE(d.policy->recipients.size() == 1);
  CHECK(d.policy->recipients[0].id == ek.did);

  // A stale UI flush must not overwrite the ciphertext.
  const std::string ciphertext = d.content;
  REQUIRE(studio::flush_working_copy(d, "TAMPER", false));
  auto c = mcdf::open_container(d.workdir);
  REQUIRE(c.has_value());
  auto on_disk = (*c)->read("content.md");
  REQUIRE(on_disk.has_value());
  CHECK(*on_disk == ciphertext);

  REQUIRE(studio::decrypt_document(d, ek));
  CHECK_FALSE(d.content_encrypted);
  CHECK(d.content == secret);

  drop(d);
  drop(ks);
}

TEST_CASE("audit chain appends, verifies and checkpoints") {
  studio::Keystore ks = test_keystore();
  studio::Doc d;
  studio::init_blank(d);
  REQUIRE(studio::flush_working_copy(d, "# Audited\n", true));

  studio::audit_append_entry(d, "CREATED", "tester");
  studio::audit_append_entry(d, "EDITED", ks.keys[0].did);
  studio::refresh_audit(d);
  REQUIRE(d.audit.size() == 2);
  CHECK(d.audit[0].action == "CREATED");
  CHECK(d.audit[1].actor == ks.keys[0].did);
  CHECK(d.audit_chain.ok);
  CHECK(d.audit_chain.entries == 2);
  CHECK_FALSE(d.audit_cp.present);

  studio::audit_make_checkpoint(d, ks.keys[0]);
  studio::refresh_audit(d);
  CHECK(d.audit_cp.present);
  CHECK(d.audit_cp.valid);
  CHECK(d.audit_cp.kid == ks.keys[0].did);

  drop(d);
  drop(ks);
}

TEST_CASE("conformance reports mirror mcdf validate") {
  studio::Keystore ks = test_keystore();
  studio::Doc d;
  studio::init_blank(d);
  REQUIRE(studio::flush_working_copy(d, "# Conform\n", true));
  REQUIRE(studio::sign_document(d, "# Conform\n", ks.keys[0], "author"));

  studio::refresh_conformance(d);
  REQUIRE(d.conf_reports.size() == 5);
  for (const auto& r : d.conf_reports) {
    // Cross-check each profile against a direct engine run.
    auto c = mcdf::open_container(d.workdir);
    REQUIRE(c.has_value());
    auto doc = mcdf::load_document(**c);
    REQUIRE(doc.has_value());
    auto direct = mcdf::validate(**c, *doc, r.profile);
    REQUIRE(direct.has_value());
    CHECK(direct->ok == r.ok);
    CHECK(direct->issues.size() == r.issues.size());
  }
  // Every profile passes for a signed, in-sync, unencrypted document.
  for (const auto& r : d.conf_reports) CHECK(r.ok);

  drop(d);
  drop(ks);
}
