// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
// Vendored into MCDF from the YMOVE studio project (imfd). Depends only on
// Dear ImGui + the C++ standard library.
//
// imfiledialog.h — a portable, self-contained ImGui file open/save dialog.
//
// Goals
//   • Works identically on Linux, macOS and Windows — one code path, no native
//     OS picker. The look is deliberately its own (a quick-access rail, clickable
//     breadcrumbs, a sortable detail list, custom vector-drawn icons) rather than
//     a clone of Explorer / Finder / GTK, but the *functionality* matches them.
//   • Drop-in reusable: depends only on Dear ImGui (>= 1.89) and the C++17
//     standard library. No FontAwesome, no extra fonts — every glyph is drawn
//     with ImDrawList, so it renders the same in any host that has a font atlas.
//   • Theme-aware: colors are derived from ImGui::GetStyle(), so it inherits
//     whatever theme the host app sets.
//
// Usage (immediate-mode; call Draw() every frame while the dialog is open):
//
//     imfd::FileDialog dlg;
//     // ...on a button press:
//     imfd::Config cfg;
//     cfg.title   = "Import telemetry";
//     cfg.mode    = imfd::Mode::OpenFile;
//     cfg.filters = { {"AiM telemetry", {".xrk"}}, {"All files", {"*"}} };
//     dlg.Open(cfg);
//     // ...every frame:
//     if (dlg.Draw() == imfd::Result::Picked)
//         load(dlg.SelectedPath());   // UTF-8 absolute path
//
// The widget renders itself as a modal popup, so it must be called from inside
// an ImGui frame. Bookmarks and the last-visited directory live on the
// FileDialog instance; serialize Bookmarks()/LastDirectory() to persist them.

#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <imgui.h>

#include "imfiledialog_util.h"

namespace imfd {

enum class Mode {
    OpenFile,    // pick one existing file
    OpenFiles,   // pick one or more existing files (Ctrl/Shift multi-select)
    SaveFile,    // type/confirm a path to write (overwrite-guarded)
    PickFolder,  // pick a directory
};

// One entry of the format dropdown. `exts` may be {".png", "jpg"} (leading dot
// optional); an empty list or a "*" wildcard means "all files".
struct Filter {
    std::string              name;  // shown in the dropdown ("Images")
    std::vector<std::string> exts;  // {".png", ".jpg"}; empty / "*" = all
};

struct Config {
    std::string         title            = "Select File";
    Mode                mode             = Mode::OpenFile;
    std::string         start_dir;                 // UTF-8; empty = last/home
    std::string         default_name;              // SaveFile: prefilled name
    std::vector<Filter> filters;                   // empty = single "All Files (*)"
    bool                show_hidden      = false;   // reveal dotfiles by default
    bool                confirm_overwrite = true;   // SaveFile overwrite guard
    bool                allow_create_dir = true;    // expose the "New Folder" action
    ImVec2              size             = ImVec2(780.0f, 500.0f);  // initial size
};

enum class Result { None, Picked, Cancelled };

class FileDialog {
public:
    FileDialog();

    // Begin a dialog session. Resets transient state (selection, filter text);
    // bookmarks and the last directory are preserved across sessions.
    void Open(const Config& cfg);

    bool IsOpen() const { return open_; }

    // Render one frame of the dialog. Returns Picked or Cancelled on the single
    // frame the dialog closes; None otherwise. Safe to call when not open.
    Result Draw();

    // Valid after Draw() returns Picked. Always absolute, UTF-8 paths.
    const std::vector<std::string>& Selected() const { return selected_; }
    std::string SelectedPath() const {
        return selected_.empty() ? std::string() : selected_.front();
    }

    // Quick-access bookmarks (UTF-8 paths) + last directory. Host may persist
    // these between runs; mutate Bookmarks() directly to pre-seed them.
    std::vector<std::string>&       Bookmarks()       { return bookmarks_; }
    const std::vector<std::string>& Bookmarks() const { return bookmarks_; }
    std::string LastDirectory() const { return path_to_utf8(cwd_); }

private:
    // A scanned directory entry.
    struct Entry {
        fs::path       path;
        std::string    name;       // UTF-8 file name
        bool           is_dir = false;
        std::uintmax_t size   = 0;
        std::string    size_str;   // "" for directories
        std::string    mtime_str;  // "YYYY-MM-DD HH:MM"
        std::int64_t   mtime  = 0; // seconds since epoch, for sorting
    };

    // A row in the left quick-access rail.
    struct QuickItem {
        std::string label;
        fs::path    path;
        int         glyph    = 0;     // imfd::glyph id (see .cpp)
        bool        bookmark = false; // user bookmark → removable
    };

    // Navigation / scanning.
    void navigate_to(const fs::path& dir);
    void refresh();
    void rebuild_visible();
    void build_quick_access();
    void add_bookmark(const fs::path& dir);
    void remove_bookmark(std::size_t quick_index);

    // Sub-renderers.
    void draw_toolbar();
    void draw_breadcrumbs();
    void draw_rail(float width, float height);
    void draw_list(const ImVec2& size);
    void draw_grid(const ImVec2& size);
    void draw_footer();
    void draw_modals();

    // Selection / acceptance.
    void on_entry_activated(int entry_index);   // double-click / Enter
    void on_entry_clicked(int entry_index, bool ctrl, bool shift);
    void accept_current();                       // honor the footer name + selection

    // State.
    bool        open_   = false;
    Result      result_ = Result::None;
    Config      cfg_;
    std::string popup_id_;

    fs::path                 cwd_;
    std::vector<Entry>       entries_;    // everything in cwd_
    std::vector<int>         visible_;    // indices into entries_ after filter
    std::vector<int>         selection_;  // indices into entries_ (selected)
    int                      anchor_ = -1;// shift-range anchor
    std::string              error_;      // last scan error (permission etc.)
    std::vector<QuickItem>   quick_;
    std::vector<std::string> bookmarks_;
    std::vector<std::string> selected_;   // results

    int  active_filter_ = 0;
    bool grid_view_     = false;
    bool show_hidden_   = false;
    int  sort_col_      = 0;     // 0 = name, 1 = size, 2 = modified
    bool sort_asc_      = true;

    // Text buffers (ImGui InputText wants raw char arrays).
    char name_buf_[1024]   = {};
    char filter_buf_[256]  = {};
    char path_buf_[2048]   = {};
    char newdir_buf_[256]  = {};
    bool editing_path_     = false;

    // Deferred popup actions.
    bool        want_open_popup_   = false;
    bool        open_overwrite_    = false;
    bool        open_newfolder_    = false;
    std::string overwrite_path_;   // SaveFile target awaiting confirmation
    bool        focus_filter_      = false;

    // A pick can be requested from a nested popup (overwrite confirm) or a
    // double-click deep in the list; it is applied once, at a single site in the
    // main popup body, so CloseCurrentPopup() always targets the right level.
    bool                     do_finish_ = false;
    Result                   finish_result_ = Result::None;
    std::vector<std::string> finish_paths_;
    void request_finish(Result r, std::vector<std::string> paths);
};

}  // namespace imfd
