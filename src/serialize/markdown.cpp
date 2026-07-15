// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/serialize/markdown.hpp"

#include <md4c.h>

#include <cctype>

namespace mcdf {
namespace {

struct HeadingCtx {
  std::vector<Heading> headings;
  bool in_heading = false;
  int level = 0;
  std::string buf;
};

std::string trim(std::string_view s) {
  std::size_t b = 0, e = s.size();
  while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
  while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
  return std::string(s.substr(b, e - b));
}

// Splits "Title {#id}" into text + id (id empty if not present).
void split_id(std::string_view raw, Heading& h) {
  std::string text = trim(raw);
  if (!text.empty() && text.back() == '}') {
    const auto pos = text.rfind("{#");
    if (pos != std::string::npos) {
      h.id = text.substr(pos + 2, text.size() - pos - 3);
      text = trim(text.substr(0, pos));
    }
  }
  h.text = std::move(text);
}

int on_enter_block(MD_BLOCKTYPE type, void* detail, void* userdata) {
  auto* c = static_cast<HeadingCtx*>(userdata);
  if (type == MD_BLOCK_H) {
    c->in_heading = true;
    c->level = static_cast<int>(static_cast<MD_BLOCK_H_DETAIL*>(detail)->level);
    c->buf.clear();
  }
  return 0;
}

int on_leave_block(MD_BLOCKTYPE type, void* /*detail*/, void* userdata) {
  auto* c = static_cast<HeadingCtx*>(userdata);
  if (type == MD_BLOCK_H) {
    c->in_heading = false;
    Heading h;
    h.level = c->level;
    split_id(c->buf, h);
    c->headings.push_back(std::move(h));
  }
  return 0;
}

int on_span(MD_SPANTYPE /*type*/, void* /*detail*/, void* /*userdata*/) {
  return 0;
}

int on_text(MD_TEXTTYPE /*type*/, const MD_CHAR* text, MD_SIZE size,
            void* userdata) {
  auto* c = static_cast<HeadingCtx*>(userdata);
  if (c->in_heading) c->buf.append(text, size);
  return 0;
}

}  // namespace

Result<std::vector<Heading>> parse_headings(std::string_view markdown) {
  HeadingCtx ctx;

  MD_PARSER parser{};
  parser.abi_version = 0;
  parser.flags = MD_DIALECT_COMMONMARK;
  parser.enter_block = on_enter_block;
  parser.leave_block = on_leave_block;
  parser.enter_span = on_span;
  parser.leave_span = on_span;
  parser.text = on_text;
  parser.debug_log = nullptr;
  parser.syntax = nullptr;

  const int rc = md_parse(markdown.data(),
                          static_cast<MD_SIZE>(markdown.size()), &parser, &ctx);
  if (rc != 0) {
    return fail(ErrorCode::kParse, "failed to parse markdown content");
  }
  return ctx.headings;
}

}  // namespace mcdf
