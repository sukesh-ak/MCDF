// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <string>

namespace {
std::string example_path() {
  return std::string(MCDF_TEST_FIXTURES) + "/example.mcdf";
}
}  // namespace

TEST_CASE("sha256_hex matches known vectors") {
  CHECK(mcdf::sha256_hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  CHECK(mcdf::sha256_hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

TEST_CASE("hash_hex rejects unknown algorithms") {
  auto h = mcdf::hash_hex("md5", "abc");
  REQUIRE_FALSE(h.has_value());
  CHECK(h.error().code == mcdf::ErrorCode::kUnsupported);
}

TEST_CASE("build_manifest hashes members and excludes manifest.json") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());

  auto m = mcdf::build_manifest(**c);
  REQUIRE(m.has_value());
  CHECK(m->hash_algorithm == "sha256");
  CHECK(m->files.contains("content.md"));
  CHECK(m->files.contains("schema.yaml"));
  CHECK(m->files.contains("metadata.yaml"));
  CHECK_FALSE(m->files.contains("manifest.json"));  // excluded by spec
  CHECK(m->files.at("content.md").size() == 64);     // hex sha256
}

TEST_CASE("a freshly built manifest verifies against its container") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());
  auto m = mcdf::build_manifest(**c);
  REQUIRE(m.has_value());

  auto v = mcdf::verify_manifest(**c, *m);
  REQUIRE(v.has_value());
  CHECK(v->ok);
  CHECK(v->mismatched.empty());
  CHECK(v->missing.empty());
  CHECK(v->extra.empty());
}

TEST_CASE("verify detects a tampered hash") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());
  auto m = mcdf::build_manifest(**c);
  REQUIRE(m.has_value());

  m->files["content.md"] = std::string(64, '0');  // corrupt one entry
  auto v = mcdf::verify_manifest(**c, *m);
  REQUIRE(v.has_value());
  CHECK_FALSE(v->ok);
  REQUIRE(v->mismatched.size() == 1);
  CHECK(v->mismatched[0] == "content.md");
}

TEST_CASE("canonical manifest orders top-level keys (files first)") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());
  auto m = mcdf::build_manifest(**c);
  REQUIRE(m.has_value());

  auto json = mcdf::manifest_to_canonical_json(*m);
  REQUIRE(json.has_value());
  // JCS orders keys by UTF-16: files < hash_algorithm < mcdf_version.
  CHECK(json->starts_with("{\"files\":{"));
}
