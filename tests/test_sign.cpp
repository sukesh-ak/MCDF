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

// Copies the fixture into a fresh temp directory so signatures can be written.
fs::path make_temp_copy(const std::string& tag) {
  const fs::path dst = fs::temp_directory_path() / ("mcdf_test_" + tag);
  std::error_code ec;
  fs::remove_all(dst, ec);
  fs::create_directories(dst, ec);
  fs::copy(example_path(), dst, fs::copy_options::recursive, ec);
  return dst;
}

}  // namespace

TEST_CASE("sign then verify a container") {
  const fs::path dir = make_temp_copy("sign_verify");
  auto container = mcdf::DirectoryContainer::open(dir);
  REQUIRE(container.has_value());

  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  auto kid = key->did_key();
  REQUIRE(kid.has_value());

  auto jws = mcdf::sign_container(**container, *key, *kid);
  REQUIRE(jws.has_value());
  REQUIRE((*container)->write("signatures/author.sig", *jws).has_value());

  auto checks = mcdf::verify_container(**container);
  REQUIRE(checks.has_value());
  REQUIRE(checks->size() == 1);
  CHECK((*checks)[0].valid);
  CHECK((*checks)[0].kid == *kid);
  CHECK((*checks)[0].alg == "EdDSA");

  std::error_code ec;
  fs::remove_all(dir, ec);
}

TEST_CASE("verify fails after the manifest is altered") {
  const fs::path dir = make_temp_copy("tamper");
  auto container = mcdf::DirectoryContainer::open(dir);
  REQUIRE(container.has_value());

  auto key = mcdf::PrivateKey::generate_ed25519();
  REQUIRE(key.has_value());
  auto kid = key->did_key();
  REQUIRE(kid.has_value());
  auto jws = mcdf::sign_container(**container, *key, *kid);
  REQUIRE(jws.has_value());
  REQUIRE((*container)->write("signatures/author.sig", *jws).has_value());

  // Change a hash value so the manifest's canonical bytes differ from what was
  // signed (whitespace-only edits would NOT break it, by design).
  auto raw = (*container)->read("manifest.json");
  REQUIRE(raw.has_value());
  std::string altered = *raw;
  const auto key_pos = altered.find("content.md");
  REQUIRE(key_pos != std::string::npos);
  const auto value_quote = altered.find('"', altered.find(':', key_pos));
  REQUIRE(value_quote != std::string::npos);
  const auto first = value_quote + 1;
  altered[first] = (altered[first] == 'a') ? 'b' : 'a';
  REQUIRE((*container)->write("manifest.json", altered).has_value());

  auto checks = mcdf::verify_container(**container);
  REQUIRE(checks.has_value());
  REQUIRE(checks->size() == 1);
  CHECK_FALSE((*checks)[0].valid);

  std::error_code ec;
  fs::remove_all(dir, ec);
}
