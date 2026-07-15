// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <filesystem>
#include <map>
#include <string>
#include <system_error>

namespace fs = std::filesystem;

namespace {
fs::path example_path() { return fs::path(MCDF_TEST_FIXTURES) / "example.mcdf"; }
}  // namespace

TEST_CASE("tar round-trips members including binary and nested paths") {
  std::map<std::string, std::string> members;
  members["a.txt"] = "hello";
  members["dir/b.bin"] = std::string("\x00\x01\x02\xff", 4);

  auto tar = mcdf::tar_write(members);
  REQUIRE(tar.has_value());
  CHECK(tar->size() % 512 == 0);

  auto back = mcdf::tar_read(*tar);
  REQUIRE(back.has_value());
  const std::map<std::string, std::string> got(back->begin(), back->end());
  CHECK(got.at("a.txt") == "hello");
  CHECK(got.at("dir/b.bin") == std::string("\x00\x01\x02\xff", 4));
}

TEST_CASE("tar output is byte-deterministic") {
  const std::map<std::string, std::string> members{{"a", "1"}, {"b", "22"}};
  auto t1 = mcdf::tar_write(members);
  auto t2 = mcdf::tar_write(members);
  REQUIRE(t1.has_value());
  REQUIRE(t2.has_value());
  CHECK(*t1 == *t2);
}

TEST_CASE("tar_read rejects a corrupted header checksum") {
  const std::map<std::string, std::string> members{{"a", "1"}};
  auto tar = mcdf::tar_write(members);
  REQUIRE(tar.has_value());
  (*tar)[0] = static_cast<char>((*tar)[0] ^ 0xFF);  // corrupt the name/header
  auto back = mcdf::tar_read(*tar);
  CHECK_FALSE(back.has_value());
}

TEST_CASE("pack a directory, read it back as a TarContainer, integrity holds") {
  auto dir = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(dir.has_value());
  auto archive = mcdf::pack_container(**dir);
  REQUIRE(archive.has_value());

  auto tar = mcdf::TarContainer::from_bytes(*archive);
  REQUIRE(tar.has_value());

  auto doc = mcdf::load_document(**tar);
  REQUIRE(doc.has_value());
  CHECK(doc->metadata.title == "Master Service Agreement");

  auto report = mcdf::validate(**tar, *doc, mcdf::Profile::kIntegrity);
  REQUIRE(report.has_value());
  CHECK(report->ok);  // packing preserves the container faithfully
}

TEST_CASE("packing a container is deterministic") {
  auto dir = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(dir.has_value());
  auto a = mcdf::pack_container(**dir);
  auto b = mcdf::pack_container(**dir);
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  CHECK(*a == *b);
}

TEST_CASE("unpack writes files that match the source") {
  auto dir = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(dir.has_value());
  auto archive = mcdf::pack_container(**dir);
  REQUIRE(archive.has_value());

  const fs::path out = fs::temp_directory_path() / "mcdf_unpack_test";
  std::error_code ec;
  fs::remove_all(out, ec);
  REQUIRE(mcdf::unpack_archive(*archive, out).has_value());

  auto rt = mcdf::DirectoryContainer::open(out);
  REQUIRE(rt.has_value());
  auto orig = (*dir)->read("content.md");
  auto copy = (*rt)->read("content.md");
  REQUIRE(orig.has_value());
  REQUIRE(copy.has_value());
  CHECK(*orig == *copy);

  fs::remove_all(out, ec);
}

TEST_CASE("unpack rejects path escapes") {
  std::map<std::string, std::string> members;
  members["../evil"] = "x";
  auto tar = mcdf::tar_write(members);
  REQUIRE(tar.has_value());
  const fs::path out = fs::temp_directory_path() / "mcdf_escape_test";
  auto r = mcdf::unpack_archive(*tar, out);
  REQUIRE_FALSE(r.has_value());
  CHECK(r.error().code == mcdf::ErrorCode::kPathEscape);
}
