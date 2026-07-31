// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Internal (not installed): the XHTML -> Markdown conversion shared by the
// EPUB and HTML importers, plus the href/slug helpers around it. Input is a
// parsed, well-formed document - HTML tag soup is normalized first (see
// html_normalize.hpp).
#pragma once

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mcdf::internal {

// An image the converter met: where it came from, and where it now lives in
// the container (assets/...).
using ImageRefs = std::vector<std::pair<std::string, std::string>>;

inline std::string dir_of(std::string_view path) {
  const auto slash = path.rfind('/');
  return slash == std::string_view::npos ? std::string{}
                                         : std::string(path.substr(0, slash + 1));
}

// Resolves `href` against `base_dir`, collapsing "." and "..", and returns
// empty for anything escaping the root.
inline std::string resolve(std::string_view base_dir, std::string_view href) {
  std::string joined;
  if (!href.empty() && href.front() == '/') joined = std::string(href.substr(1));
  else joined = std::string(base_dir) + std::string(href);

  std::vector<std::string> parts;
  std::size_t start = 0;
  while (start <= joined.size()) {
    const std::size_t slash = joined.find('/', start);
    const std::string part =
        joined.substr(start, slash == std::string::npos ? std::string::npos
                                                       : slash - start);
    if (part == "..") {
      if (parts.empty()) return {};
      parts.pop_back();
    } else if (!part.empty() && part != ".") {
      parts.push_back(part);
    }
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  std::string out;
  for (std::size_t i = 0; i < parts.size(); ++i) {
    if (i) out += '/';
    out += parts[i];
  }
  return out;
}

inline std::string strip_fragment(std::string_view href) {
  const auto hash = href.find('#');
  return std::string(hash == std::string_view::npos ? href : href.substr(0, hash));
}

inline bool is_external(std::string_view href) {
  return href.find("://") != std::string_view::npos ||
         href.rfind("data:", 0) == 0 || href.rfind("mailto:", 0) == 0;
}

// Slug for heading ids / asset names.
inline std::string slugify(std::string_view text, std::string_view fallback) {
  std::string out;
  bool dash = false;
  for (char ch : text) {
    const unsigned char c = static_cast<unsigned char>(ch);
    if (std::isalnum(c)) {
      out += static_cast<char>(std::tolower(c));
      dash = false;
    } else if (!out.empty() && !dash) {
      out += '-';
      dash = true;
    }
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  if (out.empty()) return std::string(fallback);
  if (out.size() > 48) out.resize(48);
  while (!out.empty() && out.back() == '-') out.pop_back();
  return out;
}

// Lowercased element name with any XML namespace prefix removed.
inline std::string tag_name(const pugi::xml_node& node) {
  std::string name = node.name();
  if (const auto colon = name.find(':'); colon != std::string::npos)
    name = name.substr(colon + 1);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return name;
}

struct Converter {
  std::string out;
  ImageRefs* images = nullptr;  // accumulated across documents
  std::string base_dir;         // for resolving relative hrefs
  bool dropped_tables = false;
  bool dropped_math = false;

  static std::string trim(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return std::string(s.substr(b, e - b));
  }

  // Collapses runs of whitespace: source line breaks are not content. Edge
  // whitespace becomes a single space because it separates this text node
  // from its siblings; block-level callers trim the result.
  static std::string collapse(std::string_view s) {
    std::string r;
    bool space = false;
    for (char ch : s) {
      if (std::isspace(static_cast<unsigned char>(ch))) {
        space = true;
      } else {
        if (space) r += ' ';
        space = false;
        r += ch;
      }
    }
    if (space) r += ' ';
    return r;
  }

  static void append_text(std::string& into, std::string text) {
    if (text.empty()) return;
    if (!into.empty() && into.back() == ' ' && text.front() == ' ')
      text.erase(0, 1);
    if (into.empty() && text.front() == ' ') text.erase(0, 1);
    into += text;
  }

  static std::string escape(std::string_view s) {
    std::string r;
    for (char ch : s) {
      if (ch == '\\' || ch == '`' || ch == '*' || ch == '_' || ch == '[' ||
          ch == ']')
        r += '\\';
      r += ch;
    }
    return r;
  }

  void blank_line() {
    while (!out.empty() && out.back() == ' ') out.pop_back();
    if (out.empty()) return;
    if (out.size() >= 2 && out.compare(out.size() - 2, 2, "\n\n") == 0) return;
    if (out.back() != '\n') out += '\n';
    out += '\n';
  }

  // Registers an image and returns its container-relative path.
  std::string register_image(const std::string& abs) {
    for (const auto& [from, to] : *images)
      if (from == abs) return to;
    const auto slash = abs.rfind('/');
    const std::string file = slash == std::string::npos ? abs : abs.substr(slash + 1);
    const auto dot = file.rfind('.');
    const std::string stem = dot == std::string::npos ? file : file.substr(0, dot);
    const std::string ext = dot == std::string::npos ? "" : file.substr(dot);
    std::string rel = "assets/" + slugify(stem, "image") + ext;
    int n = 1;
    for (bool clash = true; clash;) {
      clash = false;
      for (const auto& [_, existing] : *images)
        if (existing == rel) clash = true;
      if (clash) rel = "assets/" + slugify(stem, "image") + "-" +
                       std::to_string(++n) + ext;
    }
    images->emplace_back(abs, rel);
    return rel;
  }

  // Markdown for a single inline element (the element itself, not just its
  // children) - so a block-level <img> or <a> converts correctly too.
  std::string inline_element(const pugi::xml_node& child) {
    const std::string name = tag_name(child);
    if (name == "script" || name == "style") return {};
    if (name == "math") {
      dropped_math = true;
      return {};
    }
    if (name == "br") return "  \n";
    if (name == "em" || name == "i" || name == "cite") {
      const std::string inner = trim(inline_of(child));
      return inner.empty() ? std::string{} : "*" + inner + "*";
    }
    if (name == "strong" || name == "b") {
      const std::string inner = trim(inline_of(child));
      return inner.empty() ? std::string{} : "**" + inner + "**";
    }
    if (name == "del" || name == "s" || name == "strike") {
      const std::string inner = trim(inline_of(child));
      return inner.empty() ? std::string{} : "~~" + inner + "~~";
    }
    if (name == "code" || name == "kbd" || name == "samp") {
      const std::string inner = trim(collapse(child.text().get()));
      return inner.empty() ? std::string{} : "`" + inner + "`";
    }
    if (name == "a") {
      const std::string inner = trim(inline_of(child));
      const std::string href = child.attribute("href").value();
      if (inner.empty()) return {};
      if (href.empty() || href.front() == '#') return inner;
      return "[" + inner + "](" + href + ")";
    }
    if (name == "img" || name == "image") {
      std::string src = child.attribute("src").value();
      if (src.empty()) src = child.attribute("xlink:href").value();
      const std::string alt = child.attribute("alt").value();
      if (src.empty()) return {};
      if (is_external(src))
        return "[" + std::string(alt.empty() ? "image" : alt) + "](" + src + ")";
      const std::string abs = resolve(base_dir, strip_fragment(src));
      if (abs.empty()) return {};
      return "![" + escape(alt) + "](" + register_image(abs) + ")";
    }
    return inline_of(child);  // unwrap unknown inline elements
  }

  std::string inline_of(const pugi::xml_node& node) {
    std::string r;
    for (pugi::xml_node child : node) {
      if (child.type() == pugi::node_pcdata || child.type() == pugi::node_cdata) {
        append_text(r, escape(collapse(child.value())));
        continue;
      }
      if (child.type() != pugi::node_element) continue;
      r += inline_element(child);
    }
    return r;
  }

  void block(const pugi::xml_node& node, int list_depth = 0) {
    for (pugi::xml_node child : node) {
      if (child.type() == pugi::node_pcdata) {
        const std::string text = trim(collapse(child.value()));
        if (!text.empty()) out += escape(text);
        continue;
      }
      if (child.type() != pugi::node_element) continue;
      const std::string name = tag_name(child);

      if (name == "script" || name == "style" || name == "head") continue;

      if (name.size() == 2 && name[0] == 'h' && name[1] >= '1' && name[1] <= '6') {
        const std::string text = trim(inline_of(child));
        if (text.empty()) continue;
        blank_line();
        // Documents keep a single H1 (the title the caller emits), so nested
        // headings drop one level.
        const int level = std::min(6, (name[1] - '0') + 1);
        out += std::string(static_cast<std::size_t>(level), '#') + " " + text;
        blank_line();
      } else if (name == "p" || name == "div" || name == "section" ||
                 name == "article" || name == "body" || name == "html" ||
                 name == "aside" || name == "nav" || name == "figure" ||
                 name == "figcaption" || name == "header" || name == "footer" ||
                 name == "main") {
        const bool para = (name == "p" || name == "figcaption");
        if (para) {
          const std::string text = trim(inline_of(child));
          if (!text.empty()) {
            blank_line();
            out += text;
            blank_line();
          }
        } else {
          block(child, list_depth);
        }
      } else if (name == "ul" || name == "ol") {
        blank_line();
        int index = 1;
        for (pugi::xml_node li : child.children()) {
          if (li.type() != pugi::node_element) continue;
          if (tag_name(li) != "li") continue;
          const std::string text = trim(inline_of(li));
          out += std::string(static_cast<std::size_t>(list_depth) * 2, ' ');
          out += (name == "ol") ? (std::to_string(index++) + ". ") : "- ";
          out += text;
          out += '\n';
          for (pugi::xml_node sub : li.children()) {
            const std::string sub_name = tag_name(sub);
            if (sub_name == "ul" || sub_name == "ol") {
              Converter nested{"", images, base_dir};
              nested.block(li, list_depth + 1);
              out += nested.out;
              dropped_tables |= nested.dropped_tables;
              dropped_math |= nested.dropped_math;
              break;
            }
          }
        }
        blank_line();
      } else if (name == "blockquote") {
        Converter inner{"", images, base_dir};
        inner.block(child, list_depth);
        dropped_tables |= inner.dropped_tables;
        dropped_math |= inner.dropped_math;
        blank_line();
        std::size_t start = 0;
        const std::string& text = inner.out;
        while (start < text.size()) {
          const std::size_t nl = text.find('\n', start);
          const std::string line =
              text.substr(start, nl == std::string::npos ? std::string::npos
                                                         : nl - start);
          if (!line.empty()) out += "> " + line;
          out += '\n';
          if (nl == std::string::npos) break;
          start = nl + 1;
        }
        blank_line();
      } else if (name == "pre") {
        blank_line();
        out += "```\n";
        std::string text = child.text().get();
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
          text.pop_back();
        out += text;
        out += "\n```";
        blank_line();
      } else if (name == "hr") {
        blank_line();
        out += "---";
        blank_line();
      } else if (name == "table") {
        std::vector<std::vector<std::string>> rows;
        bool complex_table = false;
        for (const pugi::xpath_node& tr_node : child.select_nodes(".//tr")) {
          const pugi::xml_node tr = tr_node.node();
          std::vector<std::string> cells;
          for (pugi::xml_node cell : tr) {
            if (cell.type() != pugi::node_element) continue;
            const std::string cname = tag_name(cell);
            if (cname != "td" && cname != "th") continue;
            if (cell.attribute("colspan") || cell.attribute("rowspan"))
              complex_table = true;
            cells.push_back(trim(inline_of(cell)));
          }
          if (!cells.empty()) rows.push_back(std::move(cells));
        }
        blank_line();
        if (!rows.empty() && !complex_table) {
          const std::size_t cols = rows[0].size();
          for (std::size_t r = 0; r < rows.size(); ++r) {
            out += "|";
            for (std::size_t c = 0; c < cols; ++c)
              out += " " + (c < rows[r].size() ? rows[r][c] : std::string()) + " |";
            out += '\n';
            if (r == 0) {
              out += "|";
              for (std::size_t c = 0; c < cols; ++c) out += " --- |";
              out += '\n';
            }
          }
        } else {
          dropped_tables = true;
          for (const auto& row : rows)
            for (const auto& cell : row)
              if (!cell.empty()) out += cell + "  \n";
        }
        blank_line();
      } else if (name == "br") {
        out += "  \n";
      } else {
        // Inline element sitting at block level (e.g. a bare <img> or <a>).
        const std::string text = inline_element(child);
        if (trim(text).empty()) continue;
        const bool standalone = (name == "img" || name == "image");
        if (standalone) blank_line();
        out += text;
        if (standalone) blank_line();
      }
    }
  }
};

// Converts a parsed document to Markdown. `title_out` receives <title> or the
// first heading; a leading heading repeating that title is dropped, since the
// caller emits the title as the document's H1.
inline std::string document_to_markdown(pugi::xml_document& doc,
                                        const std::string& base_dir,
                                        ImageRefs& images, std::string& title_out,
                                        bool& tables, bool& math) {
  pugi::xml_node title_node = doc.select_node("//title").node();
  if (title_node)
    title_out = Converter::trim(Converter::collapse(title_node.text().get()));

  pugi::xml_node body = doc.select_node("//body").node();
  if (!body) body = doc.document_element();
  if (!body) return {};

  if (title_out.empty()) {
    for (const char* h : {"//h1", "//h2", "//h3"}) {
      pugi::xml_node n = doc.select_node(h).node();
      if (n) {
        title_out = Converter::trim(Converter::collapse(n.text().get()));
        if (!title_out.empty()) break;
      }
    }
  }
  if (!title_out.empty()) {
    for (const char* h : {"//h1", "//h2"}) {
      pugi::xml_node n = doc.select_node(h).node();
      if (n && Converter::trim(Converter::collapse(n.text().get())) == title_out) {
        n.parent().remove_child(n);
        break;
      }
    }
  }

  Converter conv{"", &images, base_dir};
  conv.block(body);
  tables |= conv.dropped_tables;
  math |= conv.dropped_math;
  return conv.out;
}

}  // namespace mcdf::internal
