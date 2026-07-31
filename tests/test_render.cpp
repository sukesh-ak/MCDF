// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
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

TEST_CASE("markdown_to_html turns a heading anchor into an id") {
  const std::string html = mcdf::markdown_to_html("# Overview {#overview}\n");
  CHECK(contains(html, "<h1 id=\"overview\">Overview</h1>"));
  CHECK_FALSE(contains(html, "{#overview}"));
}

TEST_CASE("markdown_to_html leaves an anchorless heading without an id") {
  // Slugifying the heading text is what most renderers do, and no two agree on
  // how, so the canonical render generates nothing (spec 10.4).
  const std::string html = mcdf::markdown_to_html("# Plain Heading\n");
  CHECK(contains(html, "<h1>Plain Heading</h1>"));
  CHECK_FALSE(contains(html, "id="));
}

// The three cases a line scan gets wrong. Anchors are read from the parse, so a
// heading is a heading wherever it sits, and a '#' that is not one is left alone.
TEST_CASE("markdown_to_html reads anchors from the parse, not from line starts") {
  SUBCASE("a code fence is content, not a heading") {
    const std::string html = mcdf::markdown_to_html("```\n# Fake {#nope}\n```\n");
    CHECK(contains(html, "# Fake {#nope}"));  // untouched
    CHECK_FALSE(contains(html, "id="));
  }
  SUBCASE("a heading inside a blockquote still carries its anchor") {
    CHECK(contains(mcdf::markdown_to_html("> ## Quoted {#q}\n"),
                   "<h2 id=\"q\">Quoted</h2>"));
  }
  SUBCASE("a setext heading still carries its anchor") {
    CHECK(contains(mcdf::markdown_to_html("Title {#sx}\n=====\n"),
                   "<h1 id=\"sx\">Title</h1>"));
  }
}

TEST_CASE("markdown_to_html attaches each anchor to its own heading") {
  const std::string html =
      mcdf::markdown_to_html("# One {#a}\n\n## Two\n\n### Three {#c}\n");
  CHECK(contains(html, "<h1 id=\"a\">One</h1>"));
  CHECK(contains(html, "<h2>Two</h2>"));
  CHECK(contains(html, "<h3 id=\"c\">Three</h3>"));
}

TEST_CASE("markdown_to_html sanitizes raw HTML and scripts") {
  const std::string html =
      mcdf::markdown_to_html("Hello <script>alert(1)</script> world\n");
  CHECK_FALSE(contains(html, "<script>"));       // not passed through
  CHECK(contains(html, "&lt;script&gt;"));       // escaped instead
}

// MCDF carries image sizing/alignment in the image title. Studio already
// implements this convention in its imgui_md fork; the HTML render must agree
// with what a Studio user sees, or the same document lays out differently
// depending on which client opened it.
TEST_CASE("markdown_to_html applies image width and alignment hints") {
  const std::string html =
      mcdf::markdown_to_html("![w1](assets/w1.jpg \"width=600 align=center\")\n");
  CHECK(contains(html, "width=\"600\""));
  CHECK(contains(html, "width:600px"));
  CHECK(contains(html, "height:auto"));
  CHECK(contains(html, "margin-left:auto;margin-right:auto"));
  // Layout instructions are not prose: they must not survive as a tooltip.
  CHECK_FALSE(contains(html, "title="));
}

TEST_CASE("markdown_to_html keeps an image title that carries no hints") {
  const std::string html =
      mcdf::markdown_to_html("![w1](assets/w1.jpg \"A photo of the site\")\n");
  CHECK(contains(html, "title=\"A photo of the site\""));
  CHECK_FALSE(contains(html, "style="));
}

TEST_CASE("markdown_to_html accepts each image hint on its own") {
  const std::string right = mcdf::markdown_to_html("![a](a.png \"align=right\")\n");
  CHECK(contains(right, "margin-left:auto;margin-right:0"));
  CHECK_FALSE(contains(right, "width=\""));

  const std::string sized = mcdf::markdown_to_html("![a](a.png \"width=320\")\n");
  CHECK(contains(sized, "width=\"320\""));
  CHECK_FALSE(contains(sized, "display:block"));
}

TEST_CASE("markdown_to_html caps a hinted image at the container width") {
  CHECK(contains(mcdf::markdown_to_html("![a](a.png \"width=99999\")\n"), "max-width:100%"));
}

TEST_CASE("markdown_to_html ignores nonsensical image hints") {
  for (const char* title : {"width=0", "width=abc", "width=-5", "align=middle"}) {
    const std::string md = std::string("![a](a.png \"") + title + "\")\n";
    const std::string html = mcdf::markdown_to_html(md);
    CHECK_FALSE(contains(html, "style="));
    CHECK(contains(html, "title="));  // left as an ordinary title
  }
}

TEST_CASE("markdown_to_html applies hints to every image in a document") {
  const std::string html = mcdf::markdown_to_html(
      "![a](a.png \"width=100\")\n\n![b](b.png)\n\n![c](c.png \"align=right\")\n");
  CHECK(contains(html, "width=\"100\""));
  CHECK(contains(html, "src=\"b.png\""));
  CHECK(contains(html, "margin-right:0"));
}

// ---- canonical render, spec 10.4 -----------------------------------------
//
// These pin the bytes the conformance kit publishes. Each one is a place where
// md4c's serializer and a spec-compliant CommonMark library were observed to
// disagree, so a regression here is not cosmetic: it is this implementation
// drifting away from every other one.

TEST_CASE("canonical render self-closes void elements") {
  const std::string html = mcdf::markdown_to_html("a  \nb\n\n---\n\n![i](x.png)\n");
  CHECK(contains(html, "<br />"));
  CHECK(contains(html, "<hr />"));
  CHECK(contains(html, "<img src=\"x.png\" alt=\"i\" />"));
}

TEST_CASE("canonical render starts every block on its own line") {
  // Loose item: the paragraph begins a line rather than following <li>.
  CHECK(contains(mcdf::markdown_to_html("- a\n\n- b\n"), "<li>\n<p>a</p>"));
  // Tight item with a nested list: the same rule, one level in.
  CHECK(contains(mcdf::markdown_to_html("- one\n  - nested\n"), "<li>one\n<ul>"));
}

TEST_CASE("canonical render keeps a hinted image self-closing") {
  // The hint rewrite inserts attributes; inserting them after the '/' produced
  // `<img src="x" / width="600">`, which browsers forgive and a byte comparison
  // does not.
  CHECK(contains(mcdf::markdown_to_html("![a](a.png \"width=600\")\n"),
                 "<img src=\"a.png\" alt=\"a\" width=\"600\" "
                 "style=\"width:600px;height:auto;max-width:100%\" />"));
}

TEST_CASE("canonical render treats raw HTML as text, not as a block") {
  // Escaping it while still recognising the block is equally safe and produces
  // different bytes, so 10.4 picks one: those characters are prose.
  CHECK(contains(mcdf::markdown_to_html("<div>x</div>\n"), "<p>&lt;div&gt;x&lt;/div&gt;</p>"));
}

TEST_CASE("canonical render names the format, not this implementation") {
  auto c = mcdf::open_container(example_path());
  REQUIRE(c.has_value());
  auto html = mcdf::render(**c, mcdf::RenderFormat::kHtml);
  REQUIRE(html.has_value());

  // Stamping the library version here would make the published vectors go stale
  // at every release and put byte-parity out of reach of any other implementation.
  CHECK(contains(*html, "<meta name=\"generator\" content=\"mcdf-render/2\">"));
  CHECK_FALSE(contains(*html, "content=\"mcdf/"));
  CHECK_FALSE(contains(*html, std::string(mcdf::version_string())));
}

TEST_CASE("markdown_to_text separates nested list items") {
  // Flushing only when a list item *closed* let a nested list append into its
  // parent's buffer, and this document came out as the single run
  // "onenesteddeep".
  CHECK(mcdf::markdown_to_text("- one\n  - nested\n    - deep\n") == "one\n\nnested\n\ndeep");
}

TEST_CASE("markdown_to_text keeps prose and drops syntax") {
  const std::string text = mcdf::markdown_to_text(
      "[label](https://example.org/page) and <https://example.org/auto>\n");
  CHECK(contains(text, "label"));
  CHECK(contains(text, "https://example.org/auto"));  // an autolink is prose
  CHECK_FALSE(contains(text, "example.org/page"));    // a destination is syntax
  CHECK_FALSE(contains(mcdf::markdown_to_text("```py\nx = 1\n```\n"), "py"));
}

TEST_CASE("markdown_to_text folds soft breaks and keeps hard ones") {
  CHECK(mcdf::markdown_to_text("soft\nbreak\n") == "soft break");
  CHECK(mcdf::markdown_to_text("hard  \nbreak\n") == "hard\nbreak");
}

TEST_CASE("markdown_to_text leaves character references undecoded") {
  // Decoding would oblige every implementation to carry the HTML5 entity table
  // to produce plain text (spec 10.4).
  CHECK(mcdf::markdown_to_text("&copy; 2026\n") == "&copy; 2026");
}

TEST_CASE("markdown_to_text drops anchors wherever the heading sits") {
  // Plain text has nowhere to put an anchor, so it goes — but only from a real
  // heading. A code sample that happens to contain one keeps it.
  CHECK(mcdf::markdown_to_text("> ## Quoted {#q}\n") == "Quoted");
  CHECK(mcdf::markdown_to_text("Title {#sx}\n=====\n") == "Title");
  CHECK(mcdf::markdown_to_text("```\n# Fake {#nope}\n```\n") == "# Fake {#nope}");
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
  // The fixture's heading carries {#overview}, and render 2 carries it through:
  // the rendered page can be linked to the section the schema binds.
  CHECK(contains(*html, "<h1 id=\"overview\">Contract Overview</h1>"));
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
