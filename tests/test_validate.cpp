// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <string>

namespace {
std::string example_path() {
  return std::string(MCDF_TEST_FIXTURES) + "/example.mcdf";
}

bool has_code(const mcdf::ValidationReport& r, std::string_view code) {
  for (const auto& i : r.issues)
    if (i.code == code) return true;
  return false;
}
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
