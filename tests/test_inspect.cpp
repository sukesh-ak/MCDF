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

TEST_CASE("parse_headings extracts level, text, and {#id}") {
  auto h = mcdf::parse_headings(
      "# Contract Overview {#overview}\n\nbody\n\n## Terms {#terms}\n");
  REQUIRE(h.has_value());
  REQUIRE(h->size() == 2);
  CHECK((*h)[0].level == 1);
  CHECK((*h)[0].text == "Contract Overview");
  CHECK((*h)[0].id == "overview");
  CHECK((*h)[1].level == 2);
  CHECK((*h)[1].id == "terms");
}

TEST_CASE("heading without an id has empty id") {
  auto h = mcdf::parse_headings("# Plain Heading\n");
  REQUIRE(h.has_value());
  REQUIRE(h->size() == 1);
  CHECK((*h)[0].text == "Plain Heading");
  CHECK((*h)[0].id.empty());
}

TEST_CASE("DirectoryContainer rejects path escapes") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());
  auto escaped = (*c)->read("../secret");
  REQUIRE_FALSE(escaped.has_value());
  CHECK(escaped.error().code == mcdf::ErrorCode::kPathEscape);
}

TEST_CASE("open missing container fails with not_found") {
  auto c = mcdf::DirectoryContainer::open(std::string(MCDF_TEST_FIXTURES) +
                                          "/does-not-exist");
  REQUIRE_FALSE(c.has_value());
  CHECK(c.error().code == mcdf::ErrorCode::kNotFound);
}

TEST_CASE("load_document reads the example container") {
  auto c = mcdf::DirectoryContainer::open(example_path());
  REQUIRE(c.has_value());

  auto doc = mcdf::load_document(**c);
  REQUIRE(doc.has_value());

  CHECK(doc->has_metadata);
  CHECK(doc->metadata.title == "Master Service Agreement");
  CHECK(doc->metadata.version == "1.2.0");
  REQUIRE(doc->metadata.authors.size() == 1);
  CHECK(doc->metadata.authors[0].name == "Alice");

  CHECK(doc->has_schema);
  CHECK(doc->schema.document_type == "contract");
  REQUIRE(doc->schema.sections.size() == 2);
  CHECK(doc->schema.sections[1].required == true);

  CHECK(doc->has_manifest);
  CHECK(doc->manifest.hash_algorithm == "sha256");

  CHECK(doc->has_content);
  bool found_overview = false;
  for (const auto& h : doc->headings)
    if (h.id == "overview") found_overview = true;
  CHECK(found_overview);
}
