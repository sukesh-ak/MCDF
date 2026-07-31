// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <filesystem>
#include <map>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path scratch(const char* name) {
  static int n = 0;
  const fs::path dir = fs::temp_directory_path() / "mcdf-html-tests" /
                       (std::string(name) + "-" + std::to_string(++n));
  std::error_code ec;
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

bool has(std::string_view haystack, std::string_view needle) {
  return haystack.find(needle) != std::string_view::npos;
}

// Serves assets from an in-memory table, like a directory would.
mcdf::AssetResolver table_resolver(std::map<std::string, std::string> files) {
  return [files = std::move(files)](
             std::string_view path) -> std::optional<std::string> {
    const auto it = files.find(std::string(path));
    if (it == files.end()) return std::nullopt;
    return it->second;
  };
}

}  // namespace

TEST_CASE("html import handles real-world tag soup") {
  const fs::path root = scratch("soup");
  auto dir = mcdf::DirectoryContainer::open(root);
  REQUIRE(dir.has_value());

  // Deliberately messy: uppercase tags, unclosed <p>/<li>, unquoted
  // attributes, a comment, a doctype, entities, script and style.
  const std::string html = R"(<!DOCTYPE html>
<html><head><TITLE>Messy &amp; Real</TITLE>
<style>body { color: red; }</style></head>
<body>
  <!-- a comment -->
  <h1>Messy &amp; Real</h1>
  <P>First paragraph with <B>bold</B> &mdash; and an entity: caf&eacute;.
  <p>Second paragraph &copy; 2026 with a <a href=https://example.org/ target=_blank>link</a>.
  <ul>
    <li>one
    <li>two
  </ul>
  <img src="pics/shot.png" alt="Screenshot">
  <br>
  <table><tr><th>K</th><th>V</th></tr><tr><td>a</td><td>1</td></tr></table>
  <script>evil();</script>
</body></html>)";

  auto report = mcdf::import_html(
      **dir, html, table_resolver({{"pics/shot.png", "PNGDATA"}}));
  REQUIRE(report.has_value());
  CHECK(report->title == "Messy & Real");
  CHECK(report->images == 1);

  auto content = (*dir)->read("content.md");
  REQUIRE(content.has_value());
  // Title emitted once as H1 (the body's duplicate <h1> is dropped).
  CHECK(has(*content, "# Messy & Real"));
  CHECK(content->find("Messy & Real") == content->rfind("Messy & Real"));
  // Unclosed <p> and <li> still produce separate blocks/items.
  CHECK(has(*content, "First paragraph with **bold**"));
  CHECK(has(*content, "Second paragraph"));
  CHECK(has(*content, "- one"));
  CHECK(has(*content, "- two"));
  // Entities resolved to real characters.
  CHECK(has(*content, "caf\xC3\xA9"));            // café
  CHECK(has(*content, "\xE2\x80\x94"));           // em dash
  CHECK(has(*content, "\xC2\xA9"));               // (c)
  // Unquoted attribute value survived.
  CHECK(has(*content, "[link](https://example.org/)"));
  // Image copied and rewritten.
  CHECK(has(*content, "![Screenshot](assets/shot.png)"));
  CHECK((*dir)->contains("assets/shot.png"));
  // Table became a pipe table.
  CHECK(has(*content, "| K | V |"));
  // Scripts and styles never survive.
  CHECK_FALSE(has(*content, "evil"));
  CHECK_FALSE(has(*content, "color: red"));

  // The result is a conformant container.
  auto doc = mcdf::load_document(**dir);
  REQUIRE(doc.has_value());
  auto integrity = mcdf::validate(**dir, *doc, mcdf::Profile::kIntegrity);
  REQUIRE(integrity.has_value());
  CHECK(integrity->ok);
  REQUIRE(doc->has_metadata);
  REQUIRE(doc->metadata.generated_by.has_value());
  CHECK(has(*doc->metadata.generated_by, "html"));

  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST_CASE("html import reports unreadable images instead of failing") {
  const fs::path root = scratch("missing-img");
  auto dir = mcdf::DirectoryContainer::open(root);
  REQUIRE(dir.has_value());

  auto report = mcdf::import_html(
      **dir, "<h1>T</h1><p>x</p><img src='gone.png' alt='g'>", {});
  REQUIRE(report.has_value());
  CHECK(report->images == 0);
  bool mentioned = false;
  for (const auto& note : report->notes)
    if (has(note, "image")) mentioned = true;
  CHECK(mentioned);

  std::error_code ec;
  fs::remove_all(root, ec);
}

TEST_CASE("html import rejects empty and duplicate documents") {
  const fs::path root = scratch("empty");
  auto dir = mcdf::DirectoryContainer::open(root);
  REQUIRE(dir.has_value());
  CHECK_FALSE(mcdf::import_html(**dir, "<html><body></body></html>", {}).has_value());
  CHECK_FALSE(mcdf::import_html(**dir, "", {}).has_value());

  REQUIRE(mcdf::import_html(**dir, "<h1>Ok</h1><p>body</p>", {}).has_value());
  CHECK_FALSE(mcdf::import_html(**dir, "<h1>Again</h1>", {}).has_value());

  std::error_code ec;
  fs::remove_all(root, ec);
}
