// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

namespace {
fs::path scratch(const char* name) {
  static int n = 0;
  const fs::path dir = fs::temp_directory_path() / "mcdf-create-tests" /
                       (std::string(name) + "-" + std::to_string(++n));
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}
}  // namespace

TEST_CASE("create_document wraps markdown into a valid container") {
  const fs::path root = scratch("wrap");
  auto dir = mcdf::DirectoryContainer::open(root);
  REQUIRE(dir.has_value());
  // A pre-copied asset must be covered by the manifest.
  REQUIRE((*dir)->write("assets/pic.png", "PNGBYTES").has_value());

  mcdf::Metadata meta;
  meta.created_at = "2026-07-22T00:00:00Z";
  auto r = mcdf::create_document(**dir, "# Imported Title\r\n\r\nBody\r\n",
                                 meta);
  REQUIRE(r.has_value());

  // Canonical content (CRLF normalized, single trailing newline).
  auto content = (*dir)->read("content.md");
  REQUIRE(content.has_value());
  CHECK(*content == "# Imported Title\n\nBody\n");

  // Title derived from the first heading.
  auto raw = (*dir)->read("metadata.yaml");
  REQUIRE(raw.has_value());
  auto parsed = mcdf::parse_metadata_yaml(*raw);
  REQUIRE(parsed.has_value());
  CHECK(parsed->title == "Imported Title");
  CHECK(parsed->created_at == "2026-07-22T00:00:00Z");

  // Integrity profile passes and the asset is listed.
  auto doc = mcdf::load_document(**dir);
  REQUIRE(doc.has_value());
  auto report = mcdf::validate(**dir, *doc, mcdf::Profile::kIntegrity);
  REQUIRE(report.has_value());
  CHECK(report->ok);
  CHECK(doc->manifest.files.contains("assets/pic.png"));

  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST_CASE("create_document keeps an explicit title and never overwrites") {
  const fs::path root = scratch("explicit");
  auto dir = mcdf::DirectoryContainer::open(root);
  REQUIRE(dir.has_value());

  mcdf::Metadata meta;
  meta.title = "Chosen Title";
  REQUIRE(mcdf::create_document(**dir, "# Heading\n", meta).has_value());
  auto parsed = mcdf::parse_metadata_yaml(*(*dir)->read("metadata.yaml"));
  REQUIRE(parsed.has_value());
  CHECK(parsed->title == "Chosen Title");

  // Second create must refuse.
  auto again = mcdf::create_document(**dir, "# Other\n", {});
  CHECK_FALSE(again.has_value());

  std::error_code ec;
  fs::remove_all(root, ec);
}
