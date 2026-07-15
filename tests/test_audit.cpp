// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <filesystem>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {
fs::path fresh_dir(const std::string& tag) {
  const fs::path dir = fs::temp_directory_path() / ("mcdf_audit_" + tag);
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}
}  // namespace

TEST_CASE("empty audit log verifies") {
  const fs::path dir = fresh_dir("empty");
  auto c = mcdf::DirectoryContainer::open(dir);
  REQUIRE(c.has_value());
  auto v = mcdf::audit_verify(**c);
  REQUIRE(v.has_value());
  CHECK(v->ok);
  CHECK(v->entries == 0);
  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("appended entries form a valid chain") {
  const fs::path dir = fresh_dir("chain");
  auto c = mcdf::DirectoryContainer::open(dir);
  REQUIRE(c.has_value());

  REQUIRE(mcdf::audit_append(**c, "CREATED", "Alice", "2025-01-01T00:00:00Z").has_value());
  REQUIRE(mcdf::audit_append(**c, "SIGNED", "Bob", "2025-01-02T00:00:00Z").has_value());
  REQUIRE(mcdf::audit_append(**c, "ENCRYPTED", "Carol", "2025-01-03T00:00:00Z").has_value());

  auto v = mcdf::audit_verify(**c);
  REQUIRE(v.has_value());
  CHECK(v->ok);
  CHECK(v->entries == 3);

  auto entries = mcdf::read_audit_log(**c);
  REQUIRE(entries.has_value());
  CHECK((*entries)[0].prev_hash == std::string(64, '0'));  // genesis
  CHECK((*entries)[1].prev_hash != std::string(64, '0'));

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("editing an entry breaks the chain") {
  const fs::path dir = fresh_dir("tamper");
  auto c = mcdf::DirectoryContainer::open(dir);
  REQUIRE(c.has_value());
  REQUIRE(mcdf::audit_append(**c, "CREATED", "Alice", "2025-01-01T00:00:00Z").has_value());
  REQUIRE(mcdf::audit_append(**c, "SIGNED", "Bobby", "2025-01-02T00:00:00Z").has_value());

  auto raw = (*c)->read("audit.log");
  REQUIRE(raw.has_value());
  std::string tampered = *raw;
  const auto pos = tampered.find("Alice");
  REQUIRE(pos != std::string::npos);
  tampered.replace(pos, 5, "Evilx");  // same length, changes entry 0's hash
  REQUIRE((*c)->write("audit.log", tampered).has_value());

  auto v = mcdf::audit_verify(**c);
  REQUIRE(v.has_value());
  CHECK_FALSE(v->ok);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("checkpoint signs the head and verifies") {
  const fs::path dir = fresh_dir("checkpoint");
  auto c = mcdf::DirectoryContainer::open(dir);
  REQUIRE(c.has_value());
  REQUIRE(mcdf::audit_append(**c, "CREATED", "Alice", "2025-01-01T00:00:00Z").has_value());

  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  auto kid = key->did_key();
  REQUIRE(kid.has_value());

  REQUIRE(mcdf::audit_checkpoint(**c, *key, *kid).has_value());
  auto cp = mcdf::audit_verify_checkpoint(**c);
  REQUIRE(cp.has_value());
  CHECK(cp->present);
  CHECK(cp->valid);
  CHECK(cp->kid == *kid);

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("tampering before the head invalidates the checkpoint") {
  const fs::path dir = fresh_dir("checkpoint_tamper");
  auto c = mcdf::DirectoryContainer::open(dir);
  REQUIRE(c.has_value());
  REQUIRE(mcdf::audit_append(**c, "CREATED", "Alice", "2025-01-01T00:00:00Z").has_value());
  REQUIRE(mcdf::audit_append(**c, "SIGNED", "Carol", "2025-01-02T00:00:00Z").has_value());

  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  auto kid = key->did_key();
  REQUIRE(kid.has_value());
  REQUIRE(mcdf::audit_checkpoint(**c, *key, *kid).has_value());

  // Edit the FIRST entry (not the head). The head entry is untouched, but the
  // chain leading to it is now broken.
  auto raw = (*c)->read("audit.log");
  REQUIRE(raw.has_value());
  std::string tampered = *raw;
  const auto pos = tampered.find("Alice");
  REQUIRE(pos != std::string::npos);
  tampered.replace(pos, 5, "Evilx");
  REQUIRE((*c)->write("audit.log", tampered).has_value());

  auto cp = mcdf::audit_verify_checkpoint(**c);
  REQUIRE(cp.has_value());
  CHECK(cp->present);
  CHECK_FALSE(cp->valid);  // chain to the head is broken

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("audit.log and audit.checkpoint are excluded from the manifest") {
  CHECK(mcdf::is_manifest_excluded("audit.log"));
  CHECK(mcdf::is_manifest_excluded("audit.checkpoint"));
  CHECK(mcdf::is_manifest_excluded("signatures/author.sig"));
  CHECK_FALSE(mcdf::is_manifest_excluded("content.md"));
}
