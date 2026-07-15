// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/serialize/render.hpp"

#include <md4c-html.h>
#include <md4c.h>

#include <cctype>
#include <string>
#include <string_view>

namespace mcdf {
namespace {

std::string trim(std::string_view s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

// Removes a trailing " {#id}" attribute from a single heading line.
std::string strip_id(const std::string& line) {
  const std::size_t first = line.find_first_not_of(" \t");
  if (first == std::string::npos || line[first] != '#') return line;
  const std::size_t last = line.find_last_not_of(" \t");
  if (last == std::string::npos || line[last] != '}') return line;
  const std::size_t br = line.rfind("{#", last);
  if (br == std::string::npos) return line;

  std::string head = line.substr(0, br);
  const std::size_t he = head.find_last_not_of(" \t");
  head = (he == std::string::npos) ? "" : head.substr(0, he + 1);
  const std::string tail = (last + 1 < line.size()) ? line.substr(last + 1) : "";
  return head + tail;
}

std::string strip_heading_ids(std::string_view md) {
  std::string out;
  std::size_t start = 0;
  while (true) {
    const std::size_t nl = md.find('\n', start);
    const std::string_view line =
        (nl == std::string_view::npos) ? md.substr(start)
                                       : md.substr(start, nl - start);
    out += strip_id(std::string(line));
    if (nl == std::string_view::npos) break;
    out += '\n';
    start = nl + 1;
  }
  return out;
}

void html_sink(const MD_CHAR* text, MD_SIZE size, void* userdata) {
  static_cast<std::string*>(userdata)->append(text, size);
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

int text_enter_block(MD_BLOCKTYPE, void*, void*) { return 0; }

int text_leave_block(MD_BLOCKTYPE type, void*, void* userdata) {
  auto* c = static_cast<TextCtx*>(userdata);
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
  const std::string pre = strip_heading_ids(markdown);
  std::string out;
  md_html(pre.data(), static_cast<MD_SIZE>(pre.size()), html_sink, &out,
          MD_FLAG_NOHTML, 0);
  return out;
}

std::string markdown_to_text(std::string_view markdown) {
  const std::string pre = strip_heading_ids(markdown);
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

  md_parse(pre.data(), static_cast<MD_SIZE>(pre.size()), &parser, &ctx);
  flush(&ctx);
  return ctx.out;
}

}  // namespace mcdf
