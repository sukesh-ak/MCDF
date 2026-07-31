// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Internal (not installed): turns real-world HTML into well-formed XML so the
// shared XHTML->Markdown converter can parse it with pugixml.
//
// This is deliberately NOT a conforming HTML5 tree builder - MCDF only needs
// enough fidelity to recover prose: it closes implicitly-ended elements
// (<p>, <li>, <tr>, ...), self-closes void elements, drops raw-text elements
// (script/style) and comments, normalizes attribute quoting, resolves the
// common named entities, and escapes stray markup characters. Anything it
// cannot make sense of is dropped rather than guessed at.
#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace mcdf::internal {

// HTML elements that never have children or a closing tag.
inline bool is_void_element(std::string_view name) {
  static constexpr std::string_view kVoid[] = {
      "area", "base",  "br",   "col",   "embed",  "hr",    "img",
      "input", "link", "meta", "param", "source", "track", "wbr"};
  return std::find(std::begin(kVoid), std::end(kVoid), name) != std::end(kVoid);
}

// Elements whose content is raw text (never markup) - dropped wholesale.
inline bool is_raw_text_element(std::string_view name) {
  return name == "script" || name == "style";
}

// True when opening `opening` implicitly closes an open `open` element.
inline bool closes_implicitly(std::string_view open, std::string_view opening) {
  if (open == "p")
    return opening == "p" || opening == "div" || opening == "ul" ||
           opening == "ol" || opening == "table" || opening == "blockquote" ||
           opening == "pre" || opening == "hr" || opening == "section" ||
           opening == "article" || opening == "h1" || opening == "h2" ||
           opening == "h3" || opening == "h4" || opening == "h5" ||
           opening == "h6";
  if (open == "li") return opening == "li";
  if (open == "dt" || open == "dd") return opening == "dt" || opening == "dd";
  if (open == "td" || open == "th")
    return opening == "td" || opening == "th" || opening == "tr";
  if (open == "tr") return opening == "tr";
  if (open == "option") return opening == "option";
  return false;
}

// The named entities worth resolving; everything else unknown has its '&'
// escaped so the XML parser stays happy.
inline bool named_entity(std::string_view name, std::string& utf8_out) {
  struct Entry {
    std::string_view name;
    std::string_view utf8;
  };
  static constexpr Entry kEntities[] = {
      {"nbsp", " "},        {"amp", "&amp;"},    {"lt", "&lt;"},
      {"gt", "&gt;"},       {"quot", "\""},      {"apos", "'"},
      {"mdash", "\xE2\x80\x94"}, {"ndash", "\xE2\x80\x93"},
      {"hellip", "\xE2\x80\xA6"}, {"lsquo", "\xE2\x80\x98"},
      {"rsquo", "\xE2\x80\x99"}, {"ldquo", "\xE2\x80\x9C"},
      {"rdquo", "\xE2\x80\x9D"}, {"copy", "\xC2\xA9"},
      {"reg", "\xC2\xAE"},  {"trade", "\xE2\x84\xA2"},
      {"deg", "\xC2\xB0"},  {"times", "\xC3\x97"},
      {"middot", "\xC2\xB7"}, {"laquo", "\xC2\xAB"},
      {"raquo", "\xC2\xBB"}, {"bull", "\xE2\x80\xA2"},
      {"eacute", "\xC3\xA9"}, {"egrave", "\xC3\xA8"},
      {"agrave", "\xC3\xA0"}, {"uuml", "\xC3\xBC"},
      {"ouml", "\xC3\xB6"}, {"auml", "\xC3\xA4"},
      {"szlig", "\xC3\x9F"}, {"ccedil", "\xC3\xA7"},
      {"ntilde", "\xC3\xB1"}, {"euro", "\xE2\x82\xAC"},
      {"pound", "\xC2\xA3"}, {"sect", "\xC2\xA7"},
      {"para", "\xC2\xB6"}, {"dagger", "\xE2\x80\xA0"},
      {"permil", "\xE2\x80\xB0"}, {"prime", "\xE2\x80\xB2"},
      {"frac12", "\xC2\xBD"}, {"frac14", "\xC2\xBC"},
      {"sup2", "\xC2\xB2"}, {"sup3", "\xC2\xB3"},
      {"larr", "\xE2\x86\x90"}, {"rarr", "\xE2\x86\x92"},
      {"harr", "\xE2\x86\x94"}, {"shy", ""},
  };
  for (const Entry& e : kEntities) {
    if (e.name == name) {
      utf8_out = std::string(e.utf8);
      return true;
    }
  }
  return false;
}

// Escapes text content for XML, resolving entities as it goes.
inline void append_escaped_text(std::string& out, std::string_view text) {
  for (std::size_t i = 0; i < text.size(); ++i) {
    const char ch = text[i];
    if (ch == '<') {
      out += "&lt;";
    } else if (ch == '>') {
      out += "&gt;";
    } else if (ch == '&') {
      const std::size_t semi = text.find(';', i + 1);
      if (semi != std::string_view::npos && semi - i <= 12) {
        const std::string_view name = text.substr(i + 1, semi - i - 1);
        if (!name.empty() && name.front() == '#') {  // numeric: keep as-is
          out += "&";
          out += name;
          out += ";";
          i = semi;
          continue;
        }
        std::string utf8;
        if (named_entity(name, utf8)) {
          out += utf8;
          i = semi;
          continue;
        }
      }
      out += "&amp;";
    } else {
      out += ch;
    }
  }
}

// Rewrites HTML as well-formed XML. Never fails: unparseable constructs are
// dropped, and any elements left open at the end are closed.
inline std::string normalize_html(std::string_view html) {
  std::string out;
  out.reserve(html.size() + html.size() / 4);
  out += "<html>";
  std::vector<std::string> open_stack;
  std::size_t i = 0;

  const auto close_element = [&](const std::string& name) {
    out += "</";
    out += name;
    out += ">";
  };

  while (i < html.size()) {
    if (html[i] != '<') {  // text run
      const std::size_t next = html.find('<', i);
      const std::size_t end = next == std::string_view::npos ? html.size() : next;
      append_escaped_text(out, html.substr(i, end - i));
      i = end;
      continue;
    }

    // Comments, doctype, processing instructions, CDATA: skip entirely.
    if (html.compare(i, 4, "<!--") == 0) {
      const std::size_t end = html.find("-->", i + 4);
      i = end == std::string_view::npos ? html.size() : end + 3;
      continue;
    }
    if (html.compare(i, 2, "<!") == 0 || html.compare(i, 2, "<?") == 0) {
      const std::size_t end = html.find('>', i);
      i = end == std::string_view::npos ? html.size() : end + 1;
      continue;
    }

    const std::size_t gt = html.find('>', i);
    if (gt == std::string_view::npos) {  // stray '<' with no tag
      append_escaped_text(out, html.substr(i));
      break;
    }
    std::string_view tag = html.substr(i + 1, gt - i - 1);
    i = gt + 1;
    if (tag.empty()) continue;

    // Closing tag.
    if (tag.front() == '/') {
      std::string name(tag.substr(1));
      name.erase(std::remove_if(name.begin(), name.end(),
                                [](unsigned char c) { return std::isspace(c); }),
                 name.end());
      std::transform(name.begin(), name.end(), name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      if (name.empty() || is_void_element(name)) continue;
      // Close down to the matching element, if it is open at all.
      const auto it = std::find(open_stack.rbegin(), open_stack.rend(), name);
      if (it == open_stack.rend()) continue;  // unmatched closer: ignore
      for (;;) {
        const std::string top = open_stack.back();
        open_stack.pop_back();
        close_element(top);
        if (top == name) break;
      }
      continue;
    }

    // Opening tag: name, then attributes.
    const bool self_closing = !tag.empty() && tag.back() == '/';
    if (self_closing) tag.remove_suffix(1);
    std::size_t p = 0;
    while (p < tag.size() && !std::isspace(static_cast<unsigned char>(tag[p]))) ++p;
    std::string name(tag.substr(0, p));
    std::transform(name.begin(), name.end(), name.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    if (name.empty() ||
        !(std::isalpha(static_cast<unsigned char>(name[0])))) continue;

    if (is_raw_text_element(name)) {  // drop the element and its content
      const std::string closing = "</" + name;
      const std::size_t end = html.find(closing, i);
      if (end == std::string_view::npos) break;
      const std::size_t after = html.find('>', end);
      i = after == std::string_view::npos ? html.size() : after + 1;
      continue;
    }

    // Close anything this tag implicitly ends.
    while (!open_stack.empty() && closes_implicitly(open_stack.back(), name)) {
      close_element(open_stack.back());
      open_stack.pop_back();
    }

    out += "<";
    out += name;
    // Attributes: name[=value] with any (or no) quoting.
    std::string_view attrs = tag.substr(p);
    std::size_t a = 0;
    while (a < attrs.size()) {
      while (a < attrs.size() && (std::isspace(static_cast<unsigned char>(attrs[a])) ||
                                  attrs[a] == '/'))
        ++a;
      if (a >= attrs.size()) break;
      const std::size_t name_start = a;
      while (a < attrs.size() && !std::isspace(static_cast<unsigned char>(attrs[a])) &&
             attrs[a] != '=' && attrs[a] != '/')
        ++a;
      std::string attr_name(attrs.substr(name_start, a - name_start));
      std::transform(attr_name.begin(), attr_name.end(), attr_name.begin(),
                     [](unsigned char c) { return std::tolower(c); });
      // Only keep attribute names XML accepts.
      const bool valid_name =
          !attr_name.empty() && (std::isalpha(static_cast<unsigned char>(attr_name[0])) ||
                                 attr_name[0] == '_') &&
          std::all_of(attr_name.begin(), attr_name.end(), [](unsigned char c) {
            return std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == ':';
          });

      std::string value;
      while (a < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[a]))) ++a;
      if (a < attrs.size() && attrs[a] == '=') {
        ++a;
        while (a < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[a]))) ++a;
        if (a < attrs.size() && (attrs[a] == '"' || attrs[a] == '\'')) {
          const char quote = attrs[a++];
          const std::size_t start = a;
          while (a < attrs.size() && attrs[a] != quote) ++a;
          value = std::string(attrs.substr(start, a - start));
          if (a < attrs.size()) ++a;
        } else {
          const std::size_t start = a;
          while (a < attrs.size() && !std::isspace(static_cast<unsigned char>(attrs[a])))
            ++a;
          value = std::string(attrs.substr(start, a - start));
        }
      }
      if (!valid_name) continue;
      out += " ";
      out += attr_name;
      out += "=\"";
      for (char ch : value) {  // attribute values: escape XML metacharacters
        if (ch == '&') out += "&amp;";
        else if (ch == '<') out += "&lt;";
        else if (ch == '>') out += "&gt;";
        else if (ch == '"') out += "&quot;";
        else out += ch;
      }
      out += "\"";
    }

    if (self_closing || is_void_element(name)) {
      out += "/>";
    } else {
      out += ">";
      open_stack.push_back(name);
    }
  }

  while (!open_stack.empty()) {  // close whatever the document left open
    close_element(open_stack.back());
    open_stack.pop_back();
  }
  out += "</html>";
  return out;
}

}  // namespace mcdf::internal
