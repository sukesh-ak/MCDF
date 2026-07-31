// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace fs = std::filesystem;

namespace {

std::string read_fixture(const char* name) {
  std::ifstream in(std::string(MCDF_TEST_FIXTURES) + "/" + name,
                   std::ios::binary);
  REQUIRE(in.good());
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

fs::path scratch(const char* name) {
  static int n = 0;
  const fs::path dir = fs::temp_directory_path() / "mcdf-epub-tests" /
                       (std::string(name) + "-" + std::to_string(++n));
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

bool contains(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

}  // namespace

TEST_CASE("zip reader lists and extracts entries (deflate + stored)") {
  auto zip = mcdf::ZipArchive::from_bytes(read_fixture("sample.epub"));
  REQUIRE(zip.has_value());
  CHECK(zip->contains("META-INF/container.xml"));
  CHECK(zip->contains("OEBPS/package.opf"));
  CHECK_FALSE(zip->contains("nope.txt"));

  auto mimetype = zip->read("mimetype");
  REQUIRE(mimetype.has_value());
  CHECK(*mimetype == "application/epub+zip");

  auto opf = zip->read("OEBPS/package.opf");
  REQUIRE(opf.has_value());
  CHECK(contains(*opf, "A Sample Book"));

  auto png = zip->read("OEBPS/images/plot.png");
  REQUIRE(png.has_value());
  CHECK(png->size() > 8);
  CHECK(static_cast<unsigned char>((*png)[1]) == 'P');  // PNG signature
}

TEST_CASE("zip reader rejects malformed input") {
  CHECK_FALSE(mcdf::ZipArchive::from_bytes("").has_value());
  CHECK_FALSE(mcdf::ZipArchive::from_bytes("not a zip at all").has_value());
  // A valid archive with its central directory truncated.
  std::string bytes = read_fixture("sample.epub");
  bytes.resize(bytes.size() / 2);
  CHECK_FALSE(mcdf::ZipArchive::from_bytes(bytes).has_value());
}

TEST_CASE("inflate round-trips against the zip fixture and rejects garbage") {
  CHECK_FALSE(mcdf::inflate_raw("\xff\xff\xff\xff", 100).has_value());
  CHECK_FALSE(mcdf::inflate_raw("", 10).has_value());
}

TEST_CASE("epub import produces a conformant document") {
  const fs::path root = scratch("import");
  auto dir = mcdf::DirectoryContainer::open(root);
  REQUIRE(dir.has_value());

  auto report = mcdf::import_epub(**dir, read_fixture("sample.epub"));
  REQUIRE(report.has_value());
  CHECK(report->title == "A Sample Book");
  CHECK(report->chapters == 2);
  CHECK(report->images == 1);
  REQUIRE(report->authors.size() == 2);
  CHECK(report->authors[0] == "Ada Lovelace");
  CHECK_FALSE(report->notes.empty());  // honest about what was dropped

  auto content = (*dir)->read("content.md");
  REQUIRE(content.has_value());
  // Spine order, with schema-bindable ids.
  CHECK(contains(*content, "# The Beginning {#the-beginning}"));
  CHECK(contains(*content, "# The End {#the-end}"));
  CHECK(content->find("The Beginning") < content->find("The End"));
  // Inline conversions.
  CHECK(contains(*content, "*emphasis*"));
  CHECK(contains(*content, "**strength**"));
  CHECK(contains(*content, "[link](https://example.org/)"));
  CHECK(contains(*content, "- first"));
  CHECK(contains(*content, "> Quoted wisdom."));
  CHECK(contains(*content, "```"));
  // Nested headings drop a level so each chapter keeps one H1.
  CHECK(contains(*content, "## A subsection"));
  // Inline runs keep their separating spaces across element boundaries.
  CHECK(contains(*content, "**strength** and a"));
  // The chapter's own <h1> repeating the title is not duplicated.
  CHECK(content->find("The Beginning") == content->rfind("The Beginning"));
  // Image copied into assets/ and the reference rewritten.
  CHECK(contains(*content, "![A plot](assets/plot.png)"));
  CHECK((*dir)->contains("assets/plot.png"));
  // Table became a GFM pipe table.
  CHECK(contains(*content, "| Key | Value |"));
  // Scripts and styles never survive.
  CHECK_FALSE(contains(*content, "alert"));
  CHECK_FALSE(contains(*content, "font-family"));

  // Metadata from the OPF, with provenance.
  auto meta = mcdf::parse_metadata_yaml(*(*dir)->read("metadata.yaml"));
  REQUIRE(meta.has_value());
  CHECK(meta->title == "A Sample Book");
  REQUIRE(meta->authors.size() == 2);
  CHECK(meta->language == "en");
  REQUIRE(meta->generated_by.has_value());
  CHECK(contains(*meta->generated_by, "epub"));

  // Generated schema binds every chapter heading.
  auto doc = mcdf::load_document(**dir);
  REQUIRE(doc.has_value());
  REQUIRE(doc->has_schema);
  CHECK(doc->schema.sections.size() == 2);
  auto report_core = mcdf::validate(**dir, *doc, mcdf::Profile::kCore);
  REQUIRE(report_core.has_value());
  CHECK(report_core->ok);  // every section id resolves to a heading
  auto report_integrity = mcdf::validate(**dir, *doc, mcdf::Profile::kIntegrity);
  REQUIRE(report_integrity.has_value());
  CHECK(report_integrity->ok);  // manifest covers content, schema and assets

  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST_CASE("epub import refuses to overwrite an existing document") {
  const fs::path root = scratch("overwrite");
  auto dir = mcdf::DirectoryContainer::open(root);
  REQUIRE(dir.has_value());
  REQUIRE((*dir)->write("content.md", "# Existing\n").has_value());
  CHECK_FALSE(mcdf::import_epub(**dir, read_fixture("sample.epub")).has_value());

  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST_CASE("epub import rejects non-epub input") {
  const fs::path root = scratch("notepub");
  auto dir = mcdf::DirectoryContainer::open(root);
  REQUIRE(dir.has_value());
  CHECK_FALSE(mcdf::import_epub(**dir, "definitely not an epub").has_value());

  std::error_code ec;
  fs::remove_all(root, ec);
}
