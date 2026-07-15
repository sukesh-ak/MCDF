// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <doctest/doctest.h>

#include <mcdf/mcdf.hpp>

#include <string>

namespace {
std::string example_path() {
  return std::string(MCDF_TEST_FIXTURES) + "/example.mcdf";
}
bool contains(const std::string& hay, const std::string& needle) {
  return hay.find(needle) != std::string::npos;
}
}  // namespace

TEST_CASE("markdown_to_html renders inline formatting") {
  const std::string html = mcdf::markdown_to_html("# Title\n\nA **bold** word.\n");
  CHECK(contains(html, "<h1>Title</h1>"));
  CHECK(contains(html, "<strong>bold</strong>"));
}

TEST_CASE("markdown_to_html strips heading ids") {
  const std::string html = mcdf::markdown_to_html("# Overview {#overview}\n");
  CHECK(contains(html, "<h1>Overview</h1>"));
  CHECK_FALSE(contains(html, "{#overview}"));
}

TEST_CASE("markdown_to_html sanitizes raw HTML and scripts") {
  const std::string html =
      mcdf::markdown_to_html("Hello <script>alert(1)</script> world\n");
  CHECK_FALSE(contains(html, "<script>"));       // not passed through
  CHECK(contains(html, "&lt;script&gt;"));       // escaped instead
}

TEST_CASE("markdown_to_text extracts readable text") {
  const std::string text =
      mcdf::markdown_to_text("# Title {#t}\n\nHello **world**.\n");
  CHECK(contains(text, "Title"));
  CHECK(contains(text, "Hello world."));
  CHECK_FALSE(contains(text, "**"));
  CHECK_FALSE(contains(text, "{#t}"));
}

TEST_CASE("render html produces a self-contained, stamped document") {
  auto c = mcdf::open_container(example_path());
  REQUIRE(c.has_value());
  auto html = mcdf::render(**c, mcdf::RenderFormat::kHtml);
  REQUIRE(html.has_value());

  CHECK(contains(*html, "<!DOCTYPE html>"));
  CHECK(contains(*html, "Content-Security-Policy"));
  CHECK(contains(*html, "default-src 'none'"));
  CHECK(contains(*html, "<title>Master Service Agreement</title>"));  // from metadata
  CHECK(contains(*html, "name=\"mcdf-source\" content=\"sha256:"));   // provenance
  CHECK(contains(*html, "<h1>Contract Overview</h1>"));
}

TEST_CASE("rendering is deterministic") {
  auto c = mcdf::open_container(example_path());
  REQUIRE(c.has_value());
  auto a = mcdf::render(**c, mcdf::RenderFormat::kHtml);
  auto b = mcdf::render(**c, mcdf::RenderFormat::kHtml);
  REQUIRE(a.has_value());
  REQUIRE(b.has_value());
  CHECK(*a == *b);
}

TEST_CASE("deferred formats return a clear error, not a silent no-op") {
  CHECK_FALSE(mcdf::parse_render_format("pdf").has_value());
  CHECK_FALSE(mcdf::parse_render_format("docx").has_value());
  auto p = mcdf::parse_render_format("html");
  REQUIRE(p.has_value());
  CHECK(mcdf::to_string(*p) == "html");
}
