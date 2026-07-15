// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/serialize/canonical_json.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <string>
#include <vector>

namespace nl = nlohmann;

namespace mcdf {
namespace {

// Decodes UTF-8 into a sequence of UTF-16 code units (RFC 8785 sorts object
// keys by these units, not by UTF-8 bytes).
std::vector<std::uint16_t> to_utf16(std::string_view s) {
  std::vector<std::uint16_t> out;
  std::size_t i = 0;
  while (i < s.size()) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    std::uint32_t cp = c;
    std::size_t len = 1;
    if (c >= 0xF0) { cp = c & 0x07; len = 4; }
    else if (c >= 0xE0) { cp = c & 0x0F; len = 3; }
    else if (c >= 0xC0) { cp = c & 0x1F; len = 2; }
    for (std::size_t k = 1; k < len && i + k < s.size(); ++k)
      cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3F);
    i += len;
    if (cp <= 0xFFFF) {
      out.push_back(static_cast<std::uint16_t>(cp));
    } else {
      cp -= 0x10000;
      out.push_back(static_cast<std::uint16_t>(0xD800 + (cp >> 10)));
      out.push_back(static_cast<std::uint16_t>(0xDC00 + (cp & 0x3FF)));
    }
  }
  return out;
}

bool utf16_less(const std::string& a, const std::string& b) {
  return to_utf16(a) < to_utf16(b);
}

void write_escaped(std::string_view s, std::string& out) {
  out.push_back('"');
  for (const char ch : s) {
    const unsigned char c = static_cast<unsigned char>(ch);
    switch (c) {
      case '"':  out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\b': out += "\\b"; break;
      case '\t': out += "\\t"; break;
      case '\n': out += "\\n"; break;
      case '\f': out += "\\f"; break;
      case '\r': out += "\\r"; break;
      default:
        if (c < 0x20) {
          constexpr char kHex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(kHex[(c >> 4) & 0x0F]);
          out.push_back(kHex[c & 0x0F]);
        } else {
          out.push_back(ch);
        }
    }
  }
  out.push_back('"');
}

void write_canonical(const nl::json& v, std::string& out) {
  switch (v.type()) {
    case nl::json::value_t::null:
      out += "null";
      break;
    case nl::json::value_t::boolean:
      out += v.get<bool>() ? "true" : "false";
      break;
    case nl::json::value_t::string:
      write_escaped(v.get_ref<const nl::json::string_t&>(), out);
      break;
    case nl::json::value_t::number_integer:
    case nl::json::value_t::number_unsigned:
      out += v.dump();  // integers render identically to their JCS form
      break;
    case nl::json::value_t::number_float:
      out += v.dump();  // floats: not JCS-exact yet (manifest has none)
      break;
    case nl::json::value_t::array: {
      out.push_back('[');
      bool first = true;
      for (const auto& e : v) {
        if (!first) out.push_back(',');
        first = false;
        write_canonical(e, out);
      }
      out.push_back(']');
      break;
    }
    case nl::json::value_t::object: {
      std::vector<std::string> keys;
      keys.reserve(v.size());
      for (auto it = v.begin(); it != v.end(); ++it) keys.push_back(it.key());
      std::sort(keys.begin(), keys.end(), utf16_less);
      out.push_back('{');
      bool first = true;
      for (const auto& k : keys) {
        if (!first) out.push_back(',');
        first = false;
        write_escaped(k, out);
        out.push_back(':');
        write_canonical(v.at(k), out);
      }
      out.push_back('}');
      break;
    }
    default:
      break;
  }
}

}  // namespace

Result<std::string> canonicalize_json(std::string_view json_text) {
  try {
    const auto value = nl::json::parse(json_text);
    std::string out;
    write_canonical(value, out);
    return out;
  } catch (const std::exception& e) {
    return fail(ErrorCode::kParse, std::string("canonicalize: ") + e.what());
  }
}

}  // namespace mcdf
