// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/serialize/writers.hpp"

#include <cctype>
#include <cstdio>
#include <sstream>
#include <string_view>

namespace mcdf {
namespace {

// Words a bare scalar would re-type (YAML 1.1 booleans/null); quoted to stay strings.
bool is_reserved_word(std::string_view s) {
  std::string lower;
  lower.reserve(s.size());
  for (char ch : s)
    lower += static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  constexpr std::string_view kWords[] = {"true", "false", "yes", "no",
                                         "on",   "off",   "null", "~"};
  for (std::string_view w : kWords)
    if (lower == w) return true;
  return false;
}

// True if s is safe to emit as a plain (unquoted) scalar. Deliberately
// conservative: anything numeric-looking, date-like, or containing YAML
// indicator characters gets double-quoted instead.
bool is_plain_safe(std::string_view s) {
  if (s.empty()) return false;
  const auto space = [](char c) {
    return std::isspace(static_cast<unsigned char>(c)) != 0;
  };
  if (space(s.front()) || space(s.back())) return false;
  constexpr std::string_view kIndicators = "-?:,[]{}#&*!|>'\"%@`";
  if (kIndicators.find(s.front()) != std::string_view::npos) return false;
  if (std::isdigit(static_cast<unsigned char>(s.front()))) return false;
  if ((s.front() == '+' || s.front() == '.') && s.size() > 1 &&
      std::isdigit(static_cast<unsigned char>(s[1])))
    return false;
  for (char ch : s) {
    if (static_cast<unsigned char>(ch) < 0x20) return false;
    if (ch == ':' || ch == '#' || ch == '"' || ch == '\\') return false;
  }
  return !is_reserved_word(s);
}

std::string double_quoted(std::string_view s) {
  std::string out = "\"";
  for (char ch : s) {
    switch (ch) {
      case '\\': out += "\\\\"; break;
      case '"': out += "\\\""; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof buf, "\\x%02X",
                        static_cast<unsigned char>(ch));
          out += buf;
        } else {
          out += ch;
        }
    }
  }
  out += '"';
  return out;
}

std::string scalar(std::string_view s) {
  return is_plain_safe(s) ? std::string(s) : double_quoted(s);
}

}  // namespace

std::string metadata_to_yaml(const Metadata& metadata) {
  std::ostringstream out;
  out << "title: " << scalar(metadata.title) << "\n";
  out << "version: " << scalar(metadata.version) << "\n";
  if (metadata.authors.empty()) {
    out << "authors: []\n";
  } else {
    out << "authors:\n";
    for (const auto& a : metadata.authors) {
      out << "  - name: " << scalar(a.name) << "\n";
      out << "    id: " << scalar(a.id) << "\n";
    }
  }
  out << "created_at: " << scalar(metadata.created_at) << "\n";
  out << "classification: " << scalar(metadata.classification) << "\n";
  out << "language: " << scalar(metadata.language) << "\n";
  if (metadata.generated_by)
    out << "generated_by: " << scalar(*metadata.generated_by) << "\n";
  return out.str();
}

std::string canonicalize_content(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 1);
  for (std::size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\r') {
      out += '\n';
      if (i + 1 < text.size() && text[i + 1] == '\n') ++i;  // CRLF -> LF
    } else {
      out += text[i];
    }
  }
  while (out.size() >= 2 && out[out.size() - 1] == '\n' &&
         out[out.size() - 2] == '\n')
    out.pop_back();  // collapse trailing blank lines to one newline
  if (!out.empty() && out.back() != '\n') out += '\n';
  return out;
}

std::string schema_to_yaml(const Schema& schema) {
  std::ostringstream out;
  out << "document_type: " << scalar(schema.document_type) << "\n";
  if (schema.sections.empty()) {
    out << "sections: []\n";
  } else {
    out << "sections:\n";
    for (const auto& s : schema.sections) {
      out << "  - id: " << scalar(s.id) << "\n";
      out << "    title: " << scalar(s.title) << "\n";
      out << "    required: " << (s.required ? "true" : "false") << "\n";
    }
  }
  return out.str();
}

}  // namespace mcdf
