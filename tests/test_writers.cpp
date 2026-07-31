// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/serialize/parsers.hpp>
#include <mcdf/serialize/writers.hpp>

TEST_CASE("metadata yaml round-trips through the parser") {
  mcdf::Metadata m;
  m.title = "MCDF: A \"Container\" Format";  // colon + quotes force quoting
  m.version = "1.0";                          // numeric-looking -> quoted
  m.authors.push_back({"Ada Lovelace", "did:key:z6MkTest"});
  m.authors.push_back({"Grace Hopper", ""});
  m.created_at = "2026-07-21T10:00:00Z";
  m.classification = "internal";
  m.language = "en";
  m.generated_by = "mcdf-studio 0.3.0";

  const std::string yaml = mcdf::metadata_to_yaml(m);
  auto parsed = mcdf::parse_metadata_yaml(yaml);
  REQUIRE(parsed.has_value());
  CHECK(parsed->title == m.title);
  CHECK(parsed->version == m.version);
  REQUIRE(parsed->authors.size() == 2);
  CHECK(parsed->authors[0].name == "Ada Lovelace");
  CHECK(parsed->authors[0].id == "did:key:z6MkTest");
  CHECK(parsed->authors[1].name == "Grace Hopper");
  CHECK(parsed->authors[1].id.empty());
  CHECK(parsed->created_at == m.created_at);
  CHECK(parsed->classification == m.classification);
  CHECK(parsed->language == m.language);
  REQUIRE(parsed->generated_by.has_value());
  CHECK(*parsed->generated_by == *m.generated_by);

  // Emission is deterministic: emit(parse(emit(x))) == emit(x).
  CHECK(mcdf::metadata_to_yaml(*parsed) == yaml);
}

TEST_CASE("metadata yaml handles empty fields and no generated_by") {
  const mcdf::Metadata m;  // everything empty
  const std::string yaml = mcdf::metadata_to_yaml(m);
  CHECK(yaml.find("authors: []") != std::string::npos);
  CHECK(yaml.find("generated_by") == std::string::npos);

  auto parsed = mcdf::parse_metadata_yaml(yaml);
  REQUIRE(parsed.has_value());
  CHECK(parsed->title.empty());
  CHECK(parsed->authors.empty());
  CHECK_FALSE(parsed->generated_by.has_value());
}

TEST_CASE("metadata yaml quotes values yaml would re-type") {
  mcdf::Metadata m;
  m.title = "true";       // YAML 1.1 boolean word
  m.version = "3.14";     // number
  m.language = "no";      // Norwegian, also a YAML boolean word
  m.classification = "-secret";  // leading indicator

  auto parsed = mcdf::parse_metadata_yaml(mcdf::metadata_to_yaml(m));
  REQUIRE(parsed.has_value());
  CHECK(parsed->title == "true");
  CHECK(parsed->version == "3.14");
  CHECK(parsed->language == "no");
  CHECK(parsed->classification == "-secret");
}

TEST_CASE("metadata yaml escapes control characters and unicode survives") {
  mcdf::Metadata m;
  m.title = "line one\nline two\ttabbed";
  m.classification = "wide \xE2\x80\x94 dash";  // UTF-8 em dash

  auto parsed = mcdf::parse_metadata_yaml(mcdf::metadata_to_yaml(m));
  REQUIRE(parsed.has_value());
  CHECK(parsed->title == m.title);
  CHECK(parsed->classification == m.classification);
}

TEST_CASE("schema yaml round-trips through the parser") {
  mcdf::Schema s;
  s.document_type = "design-doc";
  s.sections.push_back({"intro", "Introduction", true});
  s.sections.push_back({"appendix", "Appendix: extras", false});

  const std::string yaml = mcdf::schema_to_yaml(s);
  auto parsed = mcdf::parse_schema_yaml(yaml);
  REQUIRE(parsed.has_value());
  CHECK(parsed->document_type == "design-doc");
  REQUIRE(parsed->sections.size() == 2);
  CHECK(parsed->sections[0].id == "intro");
  CHECK(parsed->sections[0].title == "Introduction");
  CHECK(parsed->sections[0].required);
  CHECK(parsed->sections[1].id == "appendix");
  CHECK(parsed->sections[1].title == "Appendix: extras");
  CHECK_FALSE(parsed->sections[1].required);

  CHECK(mcdf::schema_to_yaml(*parsed) == yaml);
}

TEST_CASE("content canonicalization: LF endings, one trailing newline") {
  CHECK(mcdf::canonicalize_content("a\r\nb\rc\n") == "a\nb\nc\n");
  CHECK(mcdf::canonicalize_content("no trailing newline") ==
        "no trailing newline\n");
  CHECK(mcdf::canonicalize_content("many\n\n\n\n") == "many\n");
  CHECK(mcdf::canonicalize_content("") == "");
  // Trailing intra-line whitespace is a Markdown hard break - preserved.
  CHECK(mcdf::canonicalize_content("hard break  \nnext\n") ==
        "hard break  \nnext\n");
  // Idempotent.
  const std::string once = mcdf::canonicalize_content("x\r\ny\r\n\r\n");
  CHECK(mcdf::canonicalize_content(once) == once);
  CHECK(once == "x\ny\n");
}

TEST_CASE("schema yaml handles an empty schema") {
  const mcdf::Schema s;
  const std::string yaml = mcdf::schema_to_yaml(s);
  CHECK(yaml.find("sections: []") != std::string::npos);

  auto parsed = mcdf::parse_schema_yaml(yaml);
  REQUIRE(parsed.has_value());
  CHECK(parsed->document_type.empty());
  CHECK(parsed->sections.empty());
}

// document_type is optional (spec §4.2). Absence is how a document declines to
// state a type; `document_type: ""` would be a third state meaning the same
// thing, and it fails schema.schema.json's minLength. The writer must therefore
// omit the key rather than emit it empty - otherwise round-tripping a schema
// through this writer produces output that fails the conformance kit's own
// JSON Schema.
TEST_CASE("schema yaml omits an empty document_type rather than writing \"\"") {
  const mcdf::Schema s;
  // Exact bytes, not just "contains": mcdf-ts asserts this identical string, so
  // a divergence in either writer breaks one of the two tests. The manifest
  // hashes what is on disk, which makes emission a byte-level contract between
  // implementations rather than a formatting preference.
  CHECK(mcdf::schema_to_yaml(s) == "sections: []\n");
}

TEST_CASE("schema yaml round-trips a document with no document_type") {
  mcdf::Schema s;
  s.sections.push_back({"intro", "Introduction", true});

  const std::string yaml = mcdf::schema_to_yaml(s);
  CHECK(yaml.find("document_type") == std::string::npos);

  auto parsed = mcdf::parse_schema_yaml(yaml);
  REQUIRE(parsed.has_value());
  CHECK(parsed->document_type.empty());
  REQUIRE(parsed->sections.size() == 1);
  CHECK(parsed->sections[0].id == "intro");
  // Stable across a second pass: the omission must not drift.
  CHECK(mcdf::schema_to_yaml(*parsed) == yaml);
}
