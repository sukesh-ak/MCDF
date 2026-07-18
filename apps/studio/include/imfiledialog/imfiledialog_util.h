// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
// Vendored into MCDF from the YMOVE studio project (imfd). Pure C++ std +
// <filesystem>; no ImGui dependency.
//
// imfiledialog_util.h — pure, UI-free helpers for the imfd file dialog.
//
// This header has NO ImGui dependency: it is plain C++ (std + <filesystem>)
// so the navigation/format/match logic can be unit-tested headlessly and
// reused on its own. The interactive widget lives in imfiledialog.h/.cpp.
//
// Portable: C++17 minimum (uses <filesystem>; the char8_t paths below also
// compile under C++20/23 where path::u8string() returns std::u8string).

#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace imfd {

namespace fs = std::filesystem;

// ── Encoding ────────────────────────────────────────────────────────────────
// ImGui speaks UTF-8. std::filesystem::path::string() is the *native narrow*
// encoding (not UTF-8 on Windows), so we round-trip through u8string() which is
// guaranteed UTF-8 on every platform. The element-wise copy keeps this clean
// under both C++17 (char) and C++20+ (char8_t) without tripping -Wconversion.

inline std::string path_to_utf8(const fs::path& p) {
    const auto u8 = p.u8string();
    std::string out(u8.size(), '\0');
    for (std::size_t i = 0; i < u8.size(); ++i)
        out[i] = static_cast<char>(u8[i]);
    return out;
}

inline fs::path utf8_to_path(std::string_view s) {
#if defined(__cpp_lib_char8_t)
    std::u8string tmp(s.size(), u8'\0');
    for (std::size_t i = 0; i < s.size(); ++i)
        tmp[i] = static_cast<char8_t>(static_cast<unsigned char>(s[i]));
    return fs::path(tmp);
#else
    return fs::u8path(s);
#endif
}

// ── Text ────────────────────────────────────────────────────────────────────

inline std::string to_lower(std::string_view s) {
    std::string r(s);
    for (char& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}

// Human-readable byte count: "0 B", "2.1 MB", "13.0 GB".
inline std::string human_size(std::uintmax_t bytes) {
    static const char* kUnits[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double v = static_cast<double>(bytes);
    int u = 0;
    while (v >= 1024.0 && u < 5) { v /= 1024.0; ++u; }
    char buf[32];
    if (u == 0)
        std::snprintf(buf, sizeof(buf), "%llu B",
                      static_cast<unsigned long long>(bytes));
    else
        std::snprintf(buf, sizeof(buf), "%.1f %s", v, kUnits[u]);
    return std::string(buf);
}

// ── Matching ────────────────────────────────────────────────────────────────

// Case-insensitive extension test. `exts` entries may be ".png" or "png"; an
// empty list (or a "*"/".*" wildcard) matches everything.
inline bool ext_matches(const std::vector<std::string>& exts, const fs::path& p) {
    if (exts.empty()) return true;
    const std::string e = to_lower(path_to_utf8(p.extension()));
    for (const auto& want : exts) {
        if (want == "*" || want == ".*" || want == "*.*") return true;
        std::string w = to_lower(want);
        if (!w.empty() && w.front() != '.') w.insert(w.begin(), '.');
        if (e == w) return true;
    }
    return false;
}

// Forgiving fuzzy filter: every char of `needle` must appear in `hay` in order
// (case-insensitive subsequence). Optionally returns a relevance score that
// rewards matches at the start and runs of adjacent characters, so callers may
// rank as well as filter. Score is -1 (and the result false) on no match; an
// empty needle always matches with score 0.
inline bool fuzzy_match(std::string_view needle, std::string_view hay,
                        int* out_score = nullptr) {
    if (needle.empty()) { if (out_score) *out_score = 0; return true; }
    int score = 0;
    int run = 0;
    bool prev = false;
    std::size_t ni = 0;
    for (std::size_t hi = 0; hi < hay.size() && ni < needle.size(); ++hi) {
        const char nc =
            static_cast<char>(std::tolower(static_cast<unsigned char>(needle[ni])));
        const char hc =
            static_cast<char>(std::tolower(static_cast<unsigned char>(hay[hi])));
        if (nc == hc) {
            ++ni;
            score += 1;
            if (hi == 0) score += 8;                 // matches at the very start
            if (prev) { ++run; score += run * 4; }   // adjacency bonus
            else run = 0;
            prev = true;
        } else {
            prev = false;
            run = 0;
        }
    }
    const bool matched = (ni == needle.size());
    if (out_score) *out_score = matched ? score : -1;
    return matched;
}

}  // namespace imfd
