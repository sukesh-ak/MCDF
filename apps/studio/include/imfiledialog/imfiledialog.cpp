// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
// Vendored into MCDF from the YMOVE studio project (imfd). Depends only on
// Dear ImGui + the C++ standard library.
//
// imfiledialog.cpp — implementation of the portable imfd file dialog.
//
// See imfiledialog.h for the design rationale. The look (quick-access rail,
// breadcrumb chips, custom vector glyphs) is intentionally its own; the
// behaviour mirrors a native open/save dialog.

#include "imfiledialog.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <utility>

#if defined(_WIN32)
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  ifndef NOMINMAX
#    define NOMINMAX
#  endif
#  include <windows.h>
#endif

namespace imfd {
namespace {

// ── Custom vector glyphs ─────────────────────────────────────────────────────
// Drawn with ImDrawList so the dialog needs no icon font. Each glyph fills the
// box [tl, tl + size]; `accent` tints small highlights (drive LED, folder tab).

enum Glyph {
    GFolder, GFile, GDrive, GHome, GStar, GStarOutline,
    GChevronR, GArrowUp, GSearch, GPencil, GGridView, GListView,
    GRefresh, GNewFolder, GDesktop, GDownload, GImage, GMusic, GDocuments,
    GEye,
};

ImU32 scale_rgb(ImU32 c, float f) {
    ImVec4 v = ImGui::ColorConvertU32ToFloat4(c);
    v.x *= f; v.y *= f; v.z *= f;
    return ImGui::ColorConvertFloat4ToU32(v);
}

void draw_glyph(ImDrawList* dl, int id, ImVec2 tl, float s, ImU32 col, ImU32 accent) {
    const auto P  = [&](float nx, float ny) { return ImVec2(tl.x + nx * s, tl.y + ny * s); };
    const float th = (std::max)(1.2f, s * 0.085f);
    const ImU32 dk = scale_rgb(col, 0.55f);

    switch (id) {
    case GFolder:
    case GNewFolder: {
        dl->AddRectFilled(P(0.10f, 0.26f), P(0.48f, 0.36f), col, s * 0.06f);  // tab
        dl->AddRectFilled(P(0.08f, 0.32f), P(0.92f, 0.82f), col, s * 0.08f);  // body
        dl->AddRectFilled(P(0.08f, 0.40f), P(0.92f, 0.44f), accent, 0.0f);    // seam
        if (id == GNewFolder) {
            const ImVec2 c = P(0.50f, 0.60f);
            dl->AddLine(ImVec2(c.x - s * 0.13f, c.y), ImVec2(c.x + s * 0.13f, c.y), dk, th);
            dl->AddLine(ImVec2(c.x, c.y - s * 0.13f), ImVec2(c.x, c.y + s * 0.13f), dk, th);
        }
        break;
    }
    case GFile:
    case GDocuments: {
        const ImVec2 pts[] = {P(0.24f, 0.10f), P(0.60f, 0.10f), P(0.78f, 0.28f),
                              P(0.78f, 0.90f), P(0.24f, 0.90f)};
        dl->AddConvexPolyFilled(pts, 5, col);
        const ImVec2 fold[] = {P(0.60f, 0.10f), P(0.78f, 0.28f), P(0.60f, 0.28f)};
        dl->AddConvexPolyFilled(fold, 3, dk);
        dl->AddLine(P(0.34f, 0.50f), P(0.68f, 0.50f), accent, th * 0.8f);
        dl->AddLine(P(0.34f, 0.62f), P(0.68f, 0.62f), accent, th * 0.8f);
        dl->AddLine(P(0.34f, 0.74f), P(0.56f, 0.74f), accent, th * 0.8f);
        break;
    }
    case GDrive: {
        dl->AddRectFilled(P(0.08f, 0.34f), P(0.92f, 0.66f), col, s * 0.08f);
        dl->AddCircleFilled(P(0.79f, 0.50f), s * 0.045f, accent, 12);
        break;
    }
    case GHome: {
        const ImVec2 roof[] = {P(0.50f, 0.12f), P(0.93f, 0.48f), P(0.07f, 0.48f)};
        dl->AddConvexPolyFilled(roof, 3, col);
        dl->AddRectFilled(P(0.20f, 0.48f), P(0.80f, 0.88f), col, s * 0.04f);
        dl->AddRectFilled(P(0.42f, 0.64f), P(0.58f, 0.88f), accent, 0.0f);  // door
        break;
    }
    case GStar:
    case GStarOutline: {
        std::array<ImVec2, 10> pts{};
        const ImVec2 c = P(0.50f, 0.52f);
        for (int i = 0; i < 10; ++i) {
            const double r = (i % 2 == 0) ? 0.44 : 0.18;
            const double a = -1.5707963 + static_cast<double>(i) * 3.14159265 / 5.0;
            pts[static_cast<std::size_t>(i)] =
                ImVec2(c.x + static_cast<float>(std::cos(a) * r) * s,
                       c.y + static_cast<float>(std::sin(a) * r) * s);
        }
        if (id == GStar) dl->AddConvexPolyFilled(pts.data(), 10, col);
        else             dl->AddPolyline(pts.data(), 10, col, th, ImDrawFlags_Closed);
        break;
    }
    case GChevronR:
        dl->AddLine(P(0.42f, 0.26f), P(0.64f, 0.50f), col, th);
        dl->AddLine(P(0.64f, 0.50f), P(0.42f, 0.74f), col, th);
        break;
    case GArrowUp:
        dl->AddLine(P(0.50f, 0.84f), P(0.50f, 0.20f), col, th);
        dl->AddLine(P(0.50f, 0.20f), P(0.30f, 0.42f), col, th);
        dl->AddLine(P(0.50f, 0.20f), P(0.70f, 0.42f), col, th);
        break;
    case GSearch:
        dl->AddCircle(P(0.42f, 0.42f), s * 0.24f, col, 0, th);
        dl->AddLine(P(0.59f, 0.59f), P(0.82f, 0.82f), col, th * 1.25f);
        break;
    case GEye: {
        // Almond outline (two arcs) + pupil — the "show hidden" toggle.
        const ImVec2 c = P(0.50f, 0.50f);
        dl->PathArcTo(ImVec2(c.x, c.y + s * 0.30f), s * 0.42f, -1.05f, -2.09f, 16);
        dl->PathStroke(col, ImDrawFlags_None, th);
        dl->PathArcTo(ImVec2(c.x, c.y - s * 0.30f), s * 0.42f, 1.05f, 2.09f, 16);
        dl->PathStroke(col, ImDrawFlags_None, th);
        dl->AddCircleFilled(c, s * 0.12f, col, 14);
        break;
    }
    case GPencil:
        dl->AddLine(P(0.30f, 0.74f), P(0.74f, 0.30f), col, th * 1.8f);
        dl->AddTriangleFilled(P(0.20f, 0.82f), P(0.34f, 0.78f), P(0.26f, 0.66f), col);
        break;
    case GGridView:
        for (int r = 0; r < 2; ++r)
            for (int cc = 0; cc < 2; ++cc) {
                const float x = 0.16f + static_cast<float>(cc) * 0.40f;
                const float y = 0.16f + static_cast<float>(r) * 0.40f;
                dl->AddRectFilled(P(x, y), P(x + 0.28f, y + 0.28f), col, s * 0.04f);
            }
        break;
    case GListView:
        for (int i = 0; i < 3; ++i) {
            const float y = 0.24f + static_cast<float>(i) * 0.26f;
            dl->AddRectFilled(P(0.12f, y), P(0.24f, y + 0.12f), col, 0.0f);
            dl->AddRectFilled(P(0.32f, y + 0.01f), P(0.86f, y + 0.10f), col, 0.0f);
        }
        break;
    case GRefresh: {
        const ImVec2 c = P(0.50f, 0.50f);
        const float  r = s * 0.30f;
        dl->PathArcTo(c, r, -2.4f, 1.6f, 18);
        dl->PathStroke(col, ImDrawFlags_None, th);
        const ImVec2 e(c.x + std::cos(1.6f) * r, c.y + std::sin(1.6f) * r);
        dl->AddLine(e, ImVec2(e.x - s * 0.12f, e.y - s * 0.10f), col, th);
        dl->AddLine(e, ImVec2(e.x + s * 0.02f, e.y - s * 0.16f), col, th);
        break;
    }
    case GDesktop:
        dl->AddRectFilled(P(0.12f, 0.22f), P(0.88f, 0.64f), col, s * 0.04f);
        dl->AddRectFilled(P(0.16f, 0.26f), P(0.84f, 0.60f), accent, 0.0f);
        dl->AddRectFilled(P(0.40f, 0.64f), P(0.60f, 0.78f), col, 0.0f);  // neck
        dl->AddRectFilled(P(0.30f, 0.78f), P(0.70f, 0.84f), col, s * 0.03f);
        break;
    case GDownload:
        dl->AddLine(P(0.50f, 0.16f), P(0.50f, 0.58f), col, th);
        dl->AddLine(P(0.50f, 0.58f), P(0.34f, 0.42f), col, th);
        dl->AddLine(P(0.50f, 0.58f), P(0.66f, 0.42f), col, th);
        dl->AddLine(P(0.20f, 0.66f), P(0.20f, 0.84f), col, th);
        dl->AddLine(P(0.20f, 0.84f), P(0.80f, 0.84f), col, th);
        dl->AddLine(P(0.80f, 0.84f), P(0.80f, 0.66f), col, th);
        break;
    case GImage:
        dl->AddRect(P(0.10f, 0.20f), P(0.90f, 0.80f), col, s * 0.05f, 0, th);
        dl->AddCircleFilled(P(0.34f, 0.37f), s * 0.07f, col, 12);
        dl->AddTriangleFilled(P(0.18f, 0.76f), P(0.46f, 0.46f), P(0.62f, 0.76f), col);
        dl->AddTriangleFilled(P(0.50f, 0.76f), P(0.66f, 0.54f), P(0.86f, 0.76f), col);
        break;
    case GMusic:
        dl->AddLine(P(0.46f, 0.74f), P(0.46f, 0.20f), col, th);
        dl->AddLine(P(0.46f, 0.20f), P(0.74f, 0.28f), col, th);
        dl->AddLine(P(0.74f, 0.28f), P(0.74f, 0.62f), col, th);
        dl->AddCircleFilled(P(0.37f, 0.76f), s * 0.10f, col, 14);
        dl->AddCircleFilled(P(0.65f, 0.64f), s * 0.10f, col, 14);
        break;
    default:
        dl->AddRectFilled(tl, P(1.0f, 1.0f), col, s * 0.1f);
        break;
    }
}

// ── Platform helpers ─────────────────────────────────────────────────────────

fs::path home_dir() {
#if defined(_WIN32)
    if (const char* p = std::getenv("USERPROFILE")) return fs::path(p);
    const char* d = std::getenv("HOMEDRIVE");
    const char* h = std::getenv("HOMEPATH");
    if (d && h) return fs::path(std::string(d) + h);
#else
    if (const char* p = std::getenv("HOME")) return fs::path(p);
#endif
    std::error_code ec;
    return fs::current_path(ec);
}

std::vector<fs::path> list_roots() {
    std::vector<fs::path> roots;
#if defined(_WIN32)
    const DWORD mask = ::GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
        if (mask & (1u << i)) {
            char letter = static_cast<char>('A' + i);
            roots.emplace_back(std::string(1, letter) + ":\\");
        }
    }
#else
    roots.emplace_back("/");
#endif
    return roots;
}

std::string format_time(fs::file_time_type ft, std::int64_t* out_epoch) {
    using namespace std::chrono;
    const auto sys = time_point_cast<system_clock::duration>(
        ft - fs::file_time_type::clock::now() + system_clock::now());
    const std::time_t t = system_clock::to_time_t(sys);
    if (out_epoch) *out_epoch = static_cast<std::int64_t>(t);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &tm);
    return std::string(buf);
}

bool same_path(const fs::path& a, const fs::path& b) {
    return a.lexically_normal() == b.lexically_normal();
}

void set_buf(char* dst, std::size_t cap, const std::string& s) {
    std::snprintf(dst, cap, "%s", s.c_str());
}

constexpr float kRailWidth = 186.0f;

}  // namespace

// ─────────────────────────────────────────────────────────────────────────────

FileDialog::FileDialog() { popup_id_ = "Select File###imfd_dialog"; }

void FileDialog::Open(const Config& cfg) {
    cfg_ = cfg;
    if (cfg_.filters.empty()) cfg_.filters.push_back({"All Files (*)", {"*"}});
    popup_id_ = cfg_.title + "###imfd_dialog";

    open_            = true;
    want_open_popup_ = true;
    result_          = Result::None;
    show_hidden_     = cfg_.show_hidden;
    active_filter_   = 0;
    grid_view_       = false;
    selected_.clear();
    selection_.clear();
    anchor_          = -1;
    error_.clear();
    editing_path_    = false;
    filter_buf_[0]   = '\0';
    newdir_buf_[0]   = '\0';
    set_buf(name_buf_, sizeof(name_buf_), cfg_.default_name);

    build_quick_access();

    fs::path start;
    if (!cfg_.start_dir.empty()) start = utf8_to_path(cfg_.start_dir);
    std::error_code ec;
    if (start.empty() || !fs::is_directory(start, ec))
        start = cwd_.empty() ? home_dir() : cwd_;
    if (!fs::is_directory(start, ec)) start = home_dir();
    navigate_to(start);
}

void FileDialog::build_quick_access() {
    quick_.clear();
    const fs::path home = home_dir();
    quick_.push_back({"Home", home, GHome, false});

    struct KnownFolder { const char* label; const char* sub; int glyph; };
    static const KnownFolder kKnown[] = {
        {"Desktop",   "Desktop",   GDesktop},
        {"Documents", "Documents", GDocuments},
        {"Downloads", "Downloads", GDownload},
        {"Pictures",  "Pictures",  GImage},
        {"Music",     "Music",     GMusic},
    };
    std::error_code ec;
    for (const auto& kf : kKnown) {
        const fs::path p = home / kf.sub;
        if (fs::is_directory(p, ec)) quick_.push_back({kf.label, p, kf.glyph, false});
    }
    for (const auto& r : list_roots())
        quick_.push_back({path_to_utf8(r), r, GDrive, false});
    for (const auto& b : bookmarks_) {
        const fs::path p = utf8_to_path(b);
        std::string label = path_to_utf8(p.filename());
        if (label.empty()) label = path_to_utf8(p);
        quick_.push_back({label, p, GStar, true});
    }
}

void FileDialog::add_bookmark(const fs::path& dir) {
    const std::string s = path_to_utf8(dir.lexically_normal());
    if (std::find(bookmarks_.begin(), bookmarks_.end(), s) != bookmarks_.end()) return;
    bookmarks_.push_back(s);
    build_quick_access();
}

void FileDialog::remove_bookmark(std::size_t quick_index) {
    if (quick_index >= quick_.size()) return;
    const std::string s = path_to_utf8(quick_[quick_index].path.lexically_normal());
    bookmarks_.erase(std::remove(bookmarks_.begin(), bookmarks_.end(), s),
                     bookmarks_.end());
    build_quick_access();
}

void FileDialog::navigate_to(const fs::path& dir) {
    std::error_code ec;
    fs::path target = fs::weakly_canonical(dir, ec);
    if (ec || target.empty()) target = dir;
    cwd_ = target;
    selection_.clear();
    anchor_ = -1;
    editing_path_ = false;
    set_buf(path_buf_, sizeof(path_buf_), path_to_utf8(cwd_));
    refresh();
}

void FileDialog::refresh() {
    entries_.clear();
    error_.clear();
    std::error_code ec;
    fs::directory_iterator it(cwd_, fs::directory_options::skip_permission_denied, ec);
    if (ec) {
        error_ = "Cannot open folder: " + ec.message();
        rebuild_visible();
        return;
    }
    for (const auto& de : it) {
        Entry e;
        e.path = de.path();
        e.name = path_to_utf8(de.path().filename());
        if (e.name.empty()) continue;
        std::error_code dec;
        e.is_dir = de.is_directory(dec);
        if (!e.is_dir) {
            e.size     = de.file_size(dec);
            if (dec) e.size = 0;
            e.size_str = human_size(e.size);
        }
        std::error_code tec;
        const fs::file_time_type ft = de.last_write_time(tec);
        if (!tec) e.mtime_str = format_time(ft, &e.mtime);
        entries_.push_back(std::move(e));
    }
    rebuild_visible();
}

void FileDialog::rebuild_visible() {
    visible_.clear();
    const bool folder_mode = (cfg_.mode == Mode::PickFolder);
    const std::vector<std::string>& exts =
        cfg_.filters.empty() ? std::vector<std::string>{}
                             : cfg_.filters[static_cast<std::size_t>(active_filter_)].exts;
    const std::string needle = filter_buf_;

    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        const Entry& e = entries_[static_cast<std::size_t>(i)];
        if (!show_hidden_ && !e.name.empty() && e.name.front() == '.') continue;
        if (folder_mode && !e.is_dir) continue;            // only folders
        if (!e.is_dir && !ext_matches(exts, e.path)) continue;
        if (!needle.empty() && !fuzzy_match(needle, e.name)) continue;
        visible_.push_back(i);
    }

    const int   col = sort_col_;
    const bool  asc = sort_asc_;
    std::sort(visible_.begin(), visible_.end(), [&](int ia, int ib) {
        const Entry& a = entries_[static_cast<std::size_t>(ia)];
        const Entry& b = entries_[static_cast<std::size_t>(ib)];
        if (a.is_dir != b.is_dir) return a.is_dir;          // folders always first
        int cmp = 0;
        if (col == 1)      cmp = (a.size < b.size) ? -1 : (a.size > b.size ? 1 : 0);
        else if (col == 2) cmp = (a.mtime < b.mtime) ? -1 : (a.mtime > b.mtime ? 1 : 0);
        if (cmp == 0)      cmp = to_lower(a.name).compare(to_lower(b.name));
        return asc ? (cmp < 0) : (cmp > 0);
    });
}

// ── Selection / acceptance ──────────────────────────────────────────────────

void FileDialog::request_finish(Result r, std::vector<std::string> paths) {
    do_finish_     = true;
    finish_result_ = r;
    finish_paths_  = std::move(paths);
}

void FileDialog::on_entry_clicked(int entry_index, bool ctrl, bool shift) {
    const Entry& e = entries_[static_cast<std::size_t>(entry_index)];
    const bool multi = (cfg_.mode == Mode::OpenFiles);

    if (multi && shift && anchor_ >= 0) {
        selection_.clear();
        // Range across the *visible* order between anchor_ and entry_index.
        int a = -1, b = -1;
        for (int k = 0; k < static_cast<int>(visible_.size()); ++k) {
            if (visible_[static_cast<std::size_t>(k)] == anchor_) a = k;
            if (visible_[static_cast<std::size_t>(k)] == entry_index) b = k;
        }
        if (a > b) std::swap(a, b);
        if (a >= 0 && b >= 0)
            for (int k = a; k <= b; ++k)
                selection_.push_back(visible_[static_cast<std::size_t>(k)]);
    } else if (multi && ctrl) {
        auto pos = std::find(selection_.begin(), selection_.end(), entry_index);
        if (pos == selection_.end()) selection_.push_back(entry_index);
        else                         selection_.erase(pos);
        anchor_ = entry_index;
    } else {
        selection_.assign(1, entry_index);
        anchor_ = entry_index;
    }

    // Reflect a single file selection in the footer name field.
    if (!e.is_dir && selection_.size() == 1)
        set_buf(name_buf_, sizeof(name_buf_), e.name);
}

void FileDialog::on_entry_activated(int entry_index) {
    const Entry& e = entries_[static_cast<std::size_t>(entry_index)];
    if (e.is_dir) {
        if (cfg_.mode == Mode::PickFolder) { selection_.assign(1, entry_index); }
        navigate_to(e.path);
    } else if (cfg_.mode != Mode::PickFolder) {
        request_finish(Result::Picked, {path_to_utf8(e.path)});
    }
}

void FileDialog::accept_current() {
    if (cfg_.mode == Mode::PickFolder) {
        fs::path chosen = cwd_;
        if (selection_.size() == 1) {
            const Entry& e = entries_[static_cast<std::size_t>(selection_.front())];
            if (e.is_dir) chosen = e.path;
        }
        request_finish(Result::Picked, {path_to_utf8(chosen)});
        return;
    }

    if (cfg_.mode == Mode::OpenFiles && !selection_.empty()) {
        std::vector<std::string> paths;
        for (int idx : selection_) {
            const Entry& e = entries_[static_cast<std::size_t>(idx)];
            if (!e.is_dir) paths.push_back(path_to_utf8(e.path));
        }
        if (!paths.empty()) { request_finish(Result::Picked, std::move(paths)); return; }
    }

    // Fall back to the typed name (covers Save, and Open with a typed name).
    const std::string name = name_buf_;
    if (name.empty()) return;
    fs::path target = utf8_to_path(name);
    if (target.is_relative()) target = cwd_ / target;
    target = target.lexically_normal();
    std::error_code ec;

    if (cfg_.mode == Mode::SaveFile) {
        if (cfg_.confirm_overwrite && fs::exists(target, ec)) {
            overwrite_path_ = path_to_utf8(target);
            open_overwrite_ = true;
            return;
        }
        request_finish(Result::Picked, {path_to_utf8(target)});
    } else {  // OpenFile / OpenFiles with a typed name
        if (fs::is_directory(target, ec)) { navigate_to(target); return; }
        if (fs::exists(target, ec))
            request_finish(Result::Picked, {path_to_utf8(target)});
    }
}

// ── Rendering ────────────────────────────────────────────────────────────────

Result FileDialog::Draw() {
    if (!open_) return Result::None;
    result_    = Result::None;
    do_finish_ = false;

    if (want_open_popup_) { ImGui::OpenPopup(popup_id_.c_str()); want_open_popup_ = false; }

    ImGui::SetNextWindowSize(cfg_.size, ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(560.0f, 380.0f),
                                        ImVec2(FLT_MAX, FLT_MAX));
    bool p_open = true;
    if (ImGui::BeginPopupModal(popup_id_.c_str(), &p_open,
                               ImGuiWindowFlags_NoCollapse)) {
        draw_toolbar();
        ImGui::Separator();

        const float footer_h = ImGui::GetFrameHeightWithSpacing() * 2.0f +
                               ImGui::GetStyle().ItemSpacing.y * 2.0f;
        ImVec2 body = ImGui::GetContentRegionAvail();
        body.y -= footer_h;
        if (body.y < 120.0f) body.y = 120.0f;

        draw_rail(kRailWidth, body.y);
        ImGui::SameLine();

        ImGui::BeginChild("##imfd_right", ImVec2(0.0f, body.y), ImGuiChildFlags_None);
        {
            // Filter + view-mode row.
            const float right_w = 4.0f * (ImGui::GetFrameHeight() +
                                          ImGui::GetStyle().ItemSpacing.x);
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - right_w - 70.0f);
            if (focus_filter_) { ImGui::SetKeyboardFocusHere(); focus_filter_ = false; }
            if (ImGui::InputTextWithHint("##imfd_filter", "Filter…  (fuzzy)",
                                         filter_buf_, sizeof(filter_buf_)))
                rebuild_visible();
            ImGui::SameLine();
            ImGui::TextDisabled("%d", static_cast<int>(visible_.size()));

            ImGui::SameLine(0.0f, 12.0f);
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const ImU32  ic = ImGui::GetColorU32(ImGuiCol_Text);
            const float  bh = ImGui::GetFrameHeight();
            const auto icon_button = [&](const char* sid, int glyph, bool active,
                                         const char* tip) -> bool {
                const ImVec2 p0 = ImGui::GetCursorScreenPos();
                const bool clicked = ImGui::Button(sid, ImVec2(bh, bh));
                if (tip) ImGui::SetItemTooltip("%s", tip);
                const float g = bh * 0.62f;
                draw_glyph(dl, glyph, ImVec2(p0.x + (bh - g) * 0.5f, p0.y + (bh - g) * 0.5f),
                           g, active ? ImGui::GetColorU32(ImGuiCol_CheckMark) : ic,
                           ImGui::GetColorU32(ImGuiCol_PopupBg));
                return clicked;
            };
            if (icon_button("##imfd_list", GListView, !grid_view_, "List view"))
                grid_view_ = false;
            ImGui::SameLine();
            if (icon_button("##imfd_grid", GGridView, grid_view_, "Grid view"))
                grid_view_ = true;
            ImGui::SameLine();
            if (icon_button("##imfd_hidden", GEye, show_hidden_,
                            show_hidden_ ? "Hide hidden files" : "Show hidden files")) {
                show_hidden_ = !show_hidden_;
                rebuild_visible();
            }
            ImGui::SameLine();
            if (icon_button("##imfd_refresh", GRefresh, false, "Refresh"))
                refresh();

            if (!error_.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
                ImGui::TextWrapped("%s", error_.c_str());
                ImGui::PopStyleColor();
            }

            const ImVec2 area = ImGui::GetContentRegionAvail();
            if (grid_view_) draw_grid(area);
            else            draw_list(area);
        }
        ImGui::EndChild();

        ImGui::Separator();
        draw_footer();
        draw_modals();

        if (do_finish_) {
            result_   = finish_result_;
            selected_ = std::move(finish_paths_);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (!ImGui::IsPopupOpen(popup_id_.c_str()) && open_) {
        if (result_ == Result::None) result_ = Result::Cancelled;
        open_ = false;
    }
    return result_;
}

void FileDialog::draw_toolbar() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32  ic = ImGui::GetColorU32(ImGuiCol_Text);
    const float  bh = ImGui::GetFrameHeight();
    const ImU32  bg = ImGui::GetColorU32(ImGuiCol_PopupBg);

    const auto tool_button = [&](const char* sid, int glyph, const char* tip) -> bool {
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const bool clicked = ImGui::Button(sid, ImVec2(bh, bh));
        if (tip) ImGui::SetItemTooltip("%s", tip);
        const float g = bh * 0.6f;
        draw_glyph(dl, glyph, ImVec2(p0.x + (bh - g) * 0.5f, p0.y + (bh - g) * 0.5f),
                   g, ic, bg);
        return clicked;
    };

    if (tool_button("##imfd_up", GArrowUp, "Up one level")) {
        if (cwd_.has_parent_path() && cwd_ != cwd_.root_path())
            navigate_to(cwd_.parent_path());
    }
    ImGui::SameLine();
    if (tool_button("##imfd_edit", GPencil, "Edit path")) {
        editing_path_ = !editing_path_;
        if (editing_path_) set_buf(path_buf_, sizeof(path_buf_), path_to_utf8(cwd_));
    }
    ImGui::SameLine();
    if (cfg_.allow_create_dir) {
        if (tool_button("##imfd_newdir", GNewFolder, "New folder")) {
            newdir_buf_[0] = '\0';
            open_newfolder_ = true;
        }
        ImGui::SameLine();
    }

    if (editing_path_) {
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputText("##imfd_path", path_buf_, sizeof(path_buf_),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            const fs::path typed = utf8_to_path(path_buf_);
            std::error_code ec;
            if (fs::is_directory(typed, ec)) navigate_to(typed);
        }
    } else {
        draw_breadcrumbs();
    }
}

void FileDialog::draw_breadcrumbs() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32  chev = ImGui::GetColorU32(ImGuiCol_TextDisabled);

    // Build [root, …segments] with their accumulated paths.
    std::vector<std::pair<std::string, fs::path>> segs;
    const fs::path root = cwd_.root_path();
    segs.emplace_back(root.empty() ? std::string("/") : path_to_utf8(root), root);
    fs::path acc = root;
    for (const auto& part : cwd_.relative_path()) {
        acc /= part;
        segs.emplace_back(path_to_utf8(part), acc);
    }

    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, ImGui::GetStyle().FramePadding.y));
    for (std::size_t i = 0; i < segs.size(); ++i) {
        if (i > 0) {
            ImGui::SameLine(0.0f, 2.0f);
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float  h = ImGui::GetFrameHeight();
            draw_glyph(dl, GChevronR, ImVec2(p.x, p.y + h * 0.22f), h * 0.55f, chev,
                       chev);
            ImGui::Dummy(ImVec2(h * 0.45f, h));
            ImGui::SameLine(0.0f, 2.0f);
        }
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::Button(segs[i].first.c_str())) navigate_to(segs[i].second);
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
}

void FileDialog::draw_rail(float width, float height) {
    ImGui::BeginChild("##imfd_rail", ImVec2(width, height), ImGuiChildFlags_Borders);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32  ic = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32  ac = ImGui::GetColorU32(ImGuiCol_CheckMark);

    const auto rail_row = [&](const QuickItem& q, int uid) -> bool {
        const float h = ImGui::GetTextLineHeight() + ImGui::GetStyle().FramePadding.y * 2.0f;
        const ImVec2 cur = ImGui::GetCursorScreenPos();
        const bool sel = same_path(q.path, cwd_);
        ImGui::PushID(uid);
        const bool clicked = ImGui::Selectable("##q", sel, ImGuiSelectableFlags_None,
                                               ImVec2(0.0f, h));
        ImGui::PopID();
        const float g = ImGui::GetTextLineHeight() * 0.95f;
        draw_glyph(dl, q.glyph, ImVec2(cur.x + 4.0f, cur.y + (h - g) * 0.5f), g,
                   q.bookmark ? ac : ic, ImGui::GetColorU32(ImGuiCol_PopupBg));
        dl->AddText(ImVec2(cur.x + g + 10.0f, cur.y + ImGui::GetStyle().FramePadding.y),
                    ic, q.label.c_str());
        return clicked;
    };

    int section_drawn = -1;  // 0 places, 1 drives, 2 bookmarks
    for (std::size_t i = 0; i < quick_.size(); ++i) {
        const QuickItem& q = quick_[i];
        const int section = q.bookmark ? 2 : (q.glyph == GDrive ? 1 : 0);
        if (section != section_drawn) {
            if (section_drawn >= 0) ImGui::Spacing();
            const char* hdr = (section == 0) ? "Places"
                            : (section == 1) ? "Drives" : "Bookmarks";
            ImGui::TextDisabled("%s", hdr);
            section_drawn = section;
        }
        if (rail_row(q, static_cast<int>(i))) navigate_to(q.path);
        if (q.bookmark) {
            ImGui::PushID(static_cast<int>(i) + 5000);
            if (ImGui::BeginPopupContextItem("##bm")) {
                if (ImGui::MenuItem("Remove bookmark")) remove_bookmark(i);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::SmallButton("+ Bookmark folder")) add_bookmark(cwd_);
    ImGui::EndChild();
}

void FileDialog::draw_list(const ImVec2& size) {
    const ImGuiTableFlags fl = ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY |
                               ImGuiTableFlags_Sortable | ImGuiTableFlags_BordersInnerV |
                               ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable;
    if (!ImGui::BeginTable("##imfd_files", 3, fl, size)) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch |
                                        ImGuiTableColumnFlags_DefaultSort);
    ImGui::TableSetupColumn("Size", ImGuiTableColumnFlags_WidthFixed, 92.0f);
    ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 142.0f);
    ImGui::TableHeadersRow();

    if (ImGuiTableSortSpecs* ss = ImGui::TableGetSortSpecs(); ss && ss->SpecsDirty) {
        if (ss->SpecsCount > 0) {
            sort_col_ = ss->Specs[0].ColumnIndex;
            sort_asc_ = (ss->Specs[0].SortDirection == ImGuiSortDirection_Ascending);
        }
        rebuild_visible();
        ss->SpecsDirty = false;
    }

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32  ic = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32  bg = ImGui::GetColorU32(ImGuiCol_PopupBg);
    const ImU32  ac = ImGui::GetColorU32(ImGuiCol_CheckMark);
    int activated = -1, clicked_idx = -1;
    bool click_ctrl = false, click_shift = false;

    ImGuiListClipper clip;
    clip.Begin(static_cast<int>(visible_.size()));
    while (clip.Step()) {
        for (int row = clip.DisplayStart; row < clip.DisplayEnd; ++row) {
            const int ei = visible_[static_cast<std::size_t>(row)];
            const Entry& e = entries_[static_cast<std::size_t>(ei)];
            const bool sel = std::find(selection_.begin(), selection_.end(), ei) !=
                             selection_.end();
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            const ImVec2 cur = ImGui::GetCursorScreenPos();
            const float  w0  = ImGui::GetContentRegionAvail().x;
            const float  lh  = ImGui::GetTextLineHeight();

            ImGui::PushID(ei);
            const bool hit = ImGui::Selectable(
                "##row", sel,
                ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick,
                ImVec2(0.0f, lh));
            ImGui::PopID();
            if (hit) {
                if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) activated = ei;
                else {
                    clicked_idx = ei;
                    click_ctrl  = ImGui::GetIO().KeyCtrl;
                    click_shift = ImGui::GetIO().KeyShift;
                }
            }

            const float g = lh * 0.92f;
            draw_glyph(dl, e.is_dir ? GFolder : GFile,
                       ImVec2(cur.x + 1.0f, cur.y + (lh - g) * 0.5f), g,
                       e.is_dir ? ac : ic, bg);
            dl->PushClipRect(cur, ImVec2(cur.x + w0, cur.y + lh + 4.0f), true);
            dl->AddText(ImVec2(cur.x + g + 7.0f, cur.y), ic, e.name.c_str());
            dl->PopClipRect();

            ImGui::TableSetColumnIndex(1);
            if (e.is_dir) ImGui::TextDisabled("—");
            else          ImGui::TextUnformatted(e.size_str.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(e.mtime_str.c_str());
        }
    }
    ImGui::EndTable();

    if (clicked_idx >= 0) on_entry_clicked(clicked_idx, click_ctrl, click_shift);
    if (activated   >= 0) on_entry_activated(activated);
}

void FileDialog::draw_grid(const ImVec2& size) {
    ImGui::BeginChild("##imfd_grid", size, ImGuiChildFlags_None);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImU32  ic = ImGui::GetColorU32(ImGuiCol_Text);
    const ImU32  bg = ImGui::GetColorU32(ImGuiCol_PopupBg);
    const ImU32  ac = ImGui::GetColorU32(ImGuiCol_CheckMark);
    const ImU32  hl = ImGui::GetColorU32(ImGuiCol_Header);

    const float cell = 96.0f;
    const float availw = ImGui::GetContentRegionAvail().x;
    const int   cols = (std::max)(1, static_cast<int>(availw / cell));
    int activated = -1, clicked_idx = -1;
    bool click_ctrl = false, click_shift = false;

    for (std::size_t k = 0; k < visible_.size(); ++k) {
        const int ei = visible_[k];
        const Entry& e = entries_[static_cast<std::size_t>(ei)];
        const bool sel = std::find(selection_.begin(), selection_.end(), ei) !=
                         selection_.end();
        if (static_cast<int>(k) % cols != 0) ImGui::SameLine();

        ImGui::PushID(ei);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        const ImVec2 csz(cell - 8.0f, cell - 6.0f);
        const bool hit = ImGui::InvisibleButton("##cell", csz);
        ImGui::PopID();
        if (hit) {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) activated = ei;
            else {
                clicked_idx = ei;
                click_ctrl  = ImGui::GetIO().KeyCtrl;
                click_shift = ImGui::GetIO().KeyShift;
            }
        }
        if (sel || ImGui::IsItemHovered())
            dl->AddRectFilled(p0, ImVec2(p0.x + csz.x, p0.y + csz.y),
                              sel ? ac : hl, 5.0f);
        const float g = 40.0f;
        draw_glyph(dl, e.is_dir ? GFolder : GFile,
                   ImVec2(p0.x + (csz.x - g) * 0.5f, p0.y + 10.0f), g, ic, bg);
        const ImVec2 ts = ImGui::CalcTextSize(e.name.c_str());
        const float  tx = p0.x + (csz.x - (std::min)(ts.x, csz.x - 6.0f)) * 0.5f;
        dl->PushClipRect(ImVec2(p0.x + 3.0f, p0.y), ImVec2(p0.x + csz.x - 3.0f, p0.y + csz.y), true);
        dl->AddText(ImVec2(tx, p0.y + 58.0f), ic, e.name.c_str());
        dl->PopClipRect();
    }
    ImGui::EndChild();

    if (clicked_idx >= 0) on_entry_clicked(clicked_idx, click_ctrl, click_shift);
    if (activated   >= 0) on_entry_activated(activated);
}

void FileDialog::draw_footer() {
    const bool folder_mode = (cfg_.mode == Mode::PickFolder);

    // Row 1 — name / selection field + format filter.
    if (folder_mode) {
        std::string shown = path_to_utf8(cwd_);
        if (selection_.size() == 1) {
            const Entry& e = entries_[static_cast<std::size_t>(selection_.front())];
            if (e.is_dir) shown = path_to_utf8(e.path);
        }
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Folder:");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", shown.c_str());
    } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(cfg_.mode == Mode::SaveFile ? "Save as:" : "File:");
        ImGui::SameLine();
        const bool multi = (cfg_.mode == Mode::OpenFiles && selection_.size() > 1);
        if (multi) {
            ImGui::TextDisabled("%d files selected", static_cast<int>(selection_.size()));
        } else {
            ImGui::SetNextItemWidth(-220.0f);
            if (ImGui::InputText("##imfd_name", name_buf_, sizeof(name_buf_),
                                 ImGuiInputTextFlags_EnterReturnsTrue))
                accept_current();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(200.0f);
        const std::string cur_label =
            cfg_.filters[static_cast<std::size_t>(active_filter_)].name;
        if (ImGui::BeginCombo("##imfd_format", cur_label.c_str())) {
            for (int i = 0; i < static_cast<int>(cfg_.filters.size()); ++i) {
                const bool s = (i == active_filter_);
                if (ImGui::Selectable(cfg_.filters[static_cast<std::size_t>(i)].name.c_str(), s)) {
                    active_filter_ = i;
                    rebuild_visible();
                }
                if (s) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
    }

    // Row 2 — action buttons, right aligned.
    const char* ok_label = (cfg_.mode == Mode::SaveFile)   ? "Save"
                         : (cfg_.mode == Mode::PickFolder) ? "Select Folder"
                                                           : "Open";
    const float ok_w = (std::max)(96.0f, ImGui::CalcTextSize(ok_label).x + 28.0f);
    const float cancel_w = 96.0f;
    const float total = ok_w + cancel_w + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() +
                         (std::max)(0.0f, ImGui::GetContentRegionAvail().x - total));
    if (ImGui::Button(ok_label, ImVec2(ok_w, 0.0f))) accept_current();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(cancel_w, 0.0f))) ImGui::CloseCurrentPopup();
}

void FileDialog::draw_modals() {
    if (open_overwrite_) { ImGui::OpenPopup("Replace file?##imfd_ow"); open_overwrite_ = false; }
    if (open_newfolder_) { ImGui::OpenPopup("New folder##imfd_nf"); open_newfolder_ = false; }

    const ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Replace file?##imfd_ow", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("\"%s\" already exists.\nReplace it?",
                           overwrite_path_.c_str());
        ImGui::Spacing();
        if (ImGui::Button("Replace", ImVec2(110.0f, 0.0f))) {
            request_finish(Result::Picked, {overwrite_path_});
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("New folder##imfd_nf", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Folder name:");
        ImGui::SetNextItemWidth(280.0f);
        const bool enter = ImGui::InputText("##imfd_nfname", newdir_buf_,
                                            sizeof(newdir_buf_),
                                            ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::Spacing();
        const bool create = ImGui::Button("Create", ImVec2(110.0f, 0.0f)) || enter;
        ImGui::SameLine();
        const bool cancel = ImGui::Button("Cancel", ImVec2(110.0f, 0.0f));
        if (create) {
            const std::string nm = newdir_buf_;
            if (!nm.empty()) {
                std::error_code ec;
                const fs::path target = cwd_ / utf8_to_path(nm);
                if (fs::create_directory(target, ec) && !ec) {
                    navigate_to(target);
                    ImGui::CloseCurrentPopup();
                } else {
                    error_ = "Could not create folder: " +
                             (ec ? ec.message() : std::string("already exists"));
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        if (cancel) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

}  // namespace imfd
