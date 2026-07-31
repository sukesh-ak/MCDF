// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include "mcdf/serialize/render.hpp"

#include <md4c-html.h>
#include <md4c.h>

#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "mcdf/serialize/markdown.hpp"

namespace mcdf {
namespace {

std::string trim(std::string_view s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

// Removes a trailing " {#id}" anchor from already-rendered heading text.
//
// This replaced a pass that rewrote the *source* line by line, before parsing.
// A line scan cannot tell a heading from anything else that starts with '#', so
// it removed the anchor from `# Fake heading {#nope}` inside a fenced code block
// — the renderer quietly altering a code sample — while missing every heading
// that does not begin its line, which is any heading in a blockquote or list,
// and every setext heading. Working on the parsed heading instead fixes all
// three, and it is only possible now that the ids come from the parser.
std::string strip_trailing_anchor(std::string text, std::string_view id) {
  const std::string anchor = "{#" + std::string(id) + "}";
  if (text.size() < anchor.size()) return text;
  if (text.compare(text.size() - anchor.size(), anchor.size(), anchor) != 0) {
    return text;
  }
  text.resize(text.size() - anchor.size());
  const auto end = text.find_last_not_of(" \t");
  text.resize(end == std::string::npos ? 0 : end + 1);
  return text;
}

void html_sink(const MD_CHAR* text, MD_SIZE size, void* userdata) {
  static_cast<std::string*>(userdata)->append(text, size);
}

// ---- image layout hints --------------------------------------------------
//
// MCDF carries image sizing/alignment in the image title:
//
//   ![alt](assets/x.png "width=600 align=center")
//
// The title slot is used because it is the only place CommonMark already lets an
// image carry extra text, so a document with hints still renders everywhere.
// MCDF Studio implements the same convention in its imgui_md fork; this keeps
// the HTML render in step with what a Studio user sees.

struct ImageHints {
  double width = 0.0;   // 0 = unspecified
  std::string align;    // "", "left", "center" or "right"

  bool any() const { return width > 0.0 || !align.empty(); }
};

std::string to_lower(std::string_view s) {
  std::string out(s);
  for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  return out;
}

// Matches Studio's parsing exactly: a literal "width=" / "align=" substring with
// no spaces around '=', so the two implementations accept the same documents.
ImageHints parse_image_hints(std::string_view title) {
  ImageHints h;
  const std::string lc = to_lower(title);

  if (const auto p = lc.find("width="); p != std::string::npos) {
    const double w = std::atof(lc.c_str() + p + 6);
    if (w > 0.0) h.width = w;
  }
  if (const auto p = lc.find("align="); p != std::string::npos) {
    const std::string_view rest(lc.data() + p + 6, lc.size() - p - 6);
    if (rest.starts_with("center")) h.align = "center";
    else if (rest.starts_with("right")) h.align = "right";
    else if (rest.starts_with("left")) h.align = "left";
  }
  return h;
}

std::string format_px(double v) {
  std::ostringstream out;
  if (v == static_cast<double>(static_cast<long long>(v))) {
    out << static_cast<long long>(v);
  } else {
    out << std::fixed << std::setprecision(2) << v;
  }
  return out.str();
}

std::string image_hint_style(const ImageHints& h) {
  std::string s;
  if (h.width > 0.0) {
    // Studio shrinks an over-wide image to the content region; max-width is the
    // CSS equivalent, and height:auto preserves the aspect ratio either way.
    s += "width:" + format_px(h.width) + "px;height:auto;";
  }
  s += "max-width:100%";
  if (h.align == "center") s += ";display:block;margin-left:auto;margin-right:auto";
  else if (h.align == "right") s += ";display:block;margin-left:auto;margin-right:0";
  else if (h.align == "left") s += ";display:block;margin-left:0;margin-right:auto";
  return s;
}

// Rewrites one complete "<img ... />" tag.
std::string rewrite_img(const std::string& tag) {
  const auto tp = tag.find(" title=\"");
  if (tp == std::string::npos) return tag;
  const auto vs = tp + 8;
  const auto ve = tag.find('"', vs);
  if (ve == std::string::npos) return tag;

  const ImageHints h = parse_image_hints(tag.substr(vs, ve - vs));
  if (!h.any()) return tag;

  // The hints are layout instructions, not prose; keeping them as a title would
  // show them as a tooltip. Studio suppresses the tooltip for the same reason.
  std::string body = tag.substr(0, tp) + tag.substr(ve + 1);
  // The canonical render self-closes void elements (10.4), so the tag ends
  // " />" and the attributes have to go before the slash, not merely before the
  // '>' — inserting there yields `<img src="x" / width="600">`, which browsers
  // forgive and a byte comparison does not.
  auto close = body.rfind('>');
  if (close == std::string::npos) return tag;
  while (close > 0 && (body[close - 1] == '/' || body[close - 1] == ' ')) --close;

  std::string extra;
  if (h.width > 0.0) extra += " width=\"" + format_px(h.width) + "\"";
  extra += " style=\"" + image_hint_style(h) + "\"";
  body.insert(close, extra);
  return body;
}

// md4c escapes '&', '<', '>' and '"' inside attribute values, so no attribute
// can contain '>' and scanning to the next '>' cannot over-match. This rewrites
// our own renderer's output and is not a general-purpose HTML rewriter.
std::string apply_image_hints(const std::string& html) {
  std::string out;
  std::size_t pos = 0;
  while (true) {
    const auto start = html.find("<img ", pos);
    if (start == std::string::npos) {
      out.append(html, pos, std::string::npos);
      return out;
    }
    const auto end = html.find('>', start);
    if (end == std::string::npos) {
      out.append(html, pos, std::string::npos);
      return out;
    }
    out.append(html, pos, start - pos);
    out += rewrite_img(html.substr(start, end - start + 1));
    pos = end + 1;
  }
}

// ---- block-start normalization -------------------------------------------
//
// md4c opens a block immediately after the enclosing `<li>` (`<li><p>a</p>`) and
// immediately after a list item's inline text (`<li>one<ul>`). The CommonMark
// reference serialization — what cmark and micromark produce, and therefore what
// any implementation reaching for a spec-compliant library gets without doing
// anything — starts every block on a line of its own. Spec 10.4 states that as
// the rule rather than as the two cases where md4c happens to differ, so this
// pass is a normalization and not a patch.
//
// A literal '<' can only begin one of our own tags: raw HTML in the source is
// text (MD_FLAG_NOHTML), code spans and attribute values are escaped, so no
// document content can reach the output as an unescaped '<'.

bool is_block_tag(std::string_view name) {
  static constexpr std::string_view kBlocks[] = {
      "p",  "ul", "ol", "li", "blockquote", "pre", "hr",
      "h1", "h2", "h3", "h4", "h5", "h6",   "table"};
  for (const auto& b : kBlocks) {
    if (name == b) return true;
  }
  return false;
}

std::string normalize_block_starts(const std::string& html) {
  std::string out;
  out.reserve(html.size() + 16);
  for (std::size_t i = 0; i < html.size(); ++i) {
    if (html[i] == '<' && i + 1 < html.size() &&
        std::isalpha(static_cast<unsigned char>(html[i + 1]))) {
      std::size_t e = i + 1;
      while (e < html.size() &&
             std::isalnum(static_cast<unsigned char>(html[e]))) {
        ++e;
      }
      if (is_block_tag(std::string_view(html).substr(i + 1, e - i - 1)) &&
          !out.empty() && out.back() != '\n') {
        out += '\n';
      }
    }
    out += html[i];
  }
  return out;
}

// ---- heading anchors -----------------------------------------------------
//
// `# Overview {#overview}` renders as `<h1 id="overview">Overview</h1>`. Canonical
// render 1 dropped the anchor entirely, which left a rendered document unable to
// express its own structure: the anchors are what bind schema sections to
// headings (4.2), so a reader could not link to the section a validator had just
// checked.
//
// The ids come from `parse_headings` rather than from scanning the source for
// lines beginning with '#'. A line scan cannot tell a heading from a line inside
// a fenced code block, and attaching by position would then shift every id onto
// the wrong heading. The parser reports headings in document order, which is the
// order `<hN>` tags appear in the output.

std::string attr_escape(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (const char c : s) {
    switch (c) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      default: out.push_back(c);
    }
  }
  return out;
}

std::string apply_heading_ids(const std::string& html,
                              const std::vector<Heading>& headings) {
  std::string out;
  out.reserve(html.size() + headings.size() * 16);
  std::size_t pos = 0, next = 0;

  while (true) {
    const auto open = html.find("<h", pos);
    if (open == std::string::npos || open + 3 >= html.size()) break;
    const char digit = html[open + 2];
    if (digit < '1' || digit > '6' || html[open + 3] != '>') {
      out.append(html, pos, open + 2 - pos);
      pos = open + 2;
      continue;
    }
    const std::string close = std::string("</h") + digit + ">";
    const auto inner_end = html.find(close, open + 4);
    if (inner_end == std::string::npos) break;

    const std::string id =
        next < headings.size() ? headings[next].id : std::string();
    ++next;

    out.append(html, pos, open + 3 - pos);  // "<hN"
    if (!id.empty()) out += " id=\"" + attr_escape(id) + "\"";
    out += '>';
    // The anchor reached the output as ordinary text; it is structure, not
    // prose, and it has just become the id attribute.
    out += id.empty()
               ? html.substr(open + 4, inner_end - open - 4)
               : strip_trailing_anchor(html.substr(open + 4, inner_end - open - 4),
                                       attr_escape(id));
    pos = inner_end;
  }
  out.append(html, pos, std::string::npos);
  return out;
}

struct TextCtx {
  std::string out;
  std::string block;
  bool have = false;
};

void flush(TextCtx* c) {
  const std::string s = trim(c->block);
  if (!s.empty()) {
    if (c->have) c->out += "\n\n";
    c->out += s;
    c->have = true;
  }
  c->block.clear();
}

// A block beginning ends the one before it. Flushing only on *leave* meant a
// nested list appended into its parent item's buffer, and `- one / - nested /
// - deep` came out as the single run "onenesteddeep": the outer item never
// closed before the inner one opened. Nothing detected it until the same
// document had to render identically in two implementations.
int text_enter_block(MD_BLOCKTYPE, void*, void* userdata) {
  flush(static_cast<TextCtx*>(userdata));
  return 0;
}

int text_leave_block(MD_BLOCKTYPE type, void*, void* userdata) {
  auto* c = static_cast<TextCtx*>(userdata);
  if (type == MD_BLOCK_H) {
    // The anchor is structure and never prose, in either rendering. The HTML
    // turns it into an id; plain text has nowhere to put it, so it goes.
    Heading h;
    split_heading_id(c->block, h);
    if (!h.id.empty()) c->block = h.text;
  }
  if (type == MD_BLOCK_P || type == MD_BLOCK_H || type == MD_BLOCK_LI ||
      type == MD_BLOCK_CODE) {
    flush(c);
  }
  return 0;
}

int text_span(MD_SPANTYPE, void*, void*) { return 0; }

int text_cb(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size,
            void* userdata) {
  auto* c = static_cast<TextCtx*>(userdata);
  switch (type) {
    case MD_TEXT_NORMAL:
    case MD_TEXT_CODE:
    case MD_TEXT_ENTITY:
      c->block.append(text, size);
      break;
    case MD_TEXT_SOFTBR:
      c->block += ' ';
      break;
    case MD_TEXT_BR:
      c->block += '\n';
      break;
    default:
      break;  // raw HTML / latex ignored
  }
  return 0;
}

}  // namespace

std::string markdown_to_html(std::string_view markdown) {
  std::string out;
  // MD_HTML_FLAG_XHTML self-closes void elements (`<br />`, `<hr />`,
  // `<img ... />`). That is the CommonMark reference serialization and what
  // every other implementation emits by default; md4c's unclosed form is the
  // outlier, and the canonical render (spec 10.4) has to pick one.
  md_html(markdown.data(), static_cast<MD_SIZE>(markdown.size()), html_sink,
          &out, MD_FLAG_NOHTML, MD_HTML_FLAG_XHTML);

  std::vector<Heading> headings;
  if (auto parsed = parse_headings(markdown)) headings = std::move(*parsed);

  return apply_heading_ids(apply_image_hints(normalize_block_starts(out)),
                           headings);
}

std::string markdown_to_text(std::string_view markdown) {
  TextCtx ctx;

  MD_PARSER parser{};
  parser.abi_version = 0;
  parser.flags = MD_FLAG_NOHTML;
  parser.enter_block = text_enter_block;
  parser.leave_block = text_leave_block;
  parser.enter_span = text_span;
  parser.leave_span = text_span;
  parser.text = text_cb;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  md_parse(markdown.data(), static_cast<MD_SIZE>(markdown.size()), &parser,
           &ctx);
  flush(&ctx);
  return ctx.out;
}

}  // namespace mcdf
