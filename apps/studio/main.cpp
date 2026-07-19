// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// MCDF Studio - document-centric editor shell.
//
// A Dear ImGui desktop editor that links libmcdf directly (no CLI/FFI seam) and
// treats a single .mcdf file as "the document" - like .docx. Opening a .mcdf
// unpacks it to a hidden temp working copy; Save re-packs it back into the same
// .mcdf file. It provides a syntax-highlighted Markdown source editor
// (ImGuiColorTextEdit, monospace), a live rendered preview (imgui_md over md4c),
// dirty tracking, a themed docked shell, and a status bar.
//
// The look (dark theme, Roboto UI + RobotoMono editor + FontAwesome icons,
// docked host + status bar) is adapted from the YMOVE studio shell.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>  // pulls in <GL/gl.h> for the frame clear (GL 1.1)

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // keep windows.h from defining min/max macros (breaks std::min/max)
#endif
#include <windows.h>  // GetModuleFileNameA for assets_dir()
#include <dwmapi.h>   // dark native title bar
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>  // glfwGetWin32Window
#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <mcdf/mcdf.hpp>

#include "imfiledialog/imfiledialog.h"
#include "TextEditor.h"
#include "imgui_md.h"
#include "IconsFontAwesome6.h"

namespace fs = std::filesystem;

namespace {

// Accent + warning colors used by the status bar / dirty marker.
const ImVec4 kAccent{0.00f, 0.47f, 0.93f, 1.0f};
const ImVec4 kDirty{1.00f, 0.70f, 0.20f, 1.0f};

// imgui_md subclass; skip inline images in the E1 preview.
struct MarkdownView : imgui_md {
  bool get_image(image_info&) const override { return false; }
  // Visible link color (imgui_md's default uses ImGuiCol_ButtonHovered, which is
  // near-invisible in the dark theme).
  ImVec4 get_color() const override {
    if (!m_href.empty()) return ImVec4(0.32f, 0.66f, 1.00f, 1.00f);
    return ImGui::GetStyle().Colors[ImGuiCol_Text];
  }
};

// The open document. The editable content.md text lives in the TextEditor; the
// container files live under g_workdir.
struct OpenDoc {
  std::string path;
  std::string status;
  std::vector<std::string> files;
  bool has_content = false;
  bool is_archive = false;
  std::string title;
  std::string doc_type;
  int heading_count = 0;
};

std::optional<OpenDoc> g_doc;
imfd::FileDialog g_dialog;
std::unique_ptr<TextEditor> g_editor;
std::size_t g_saved_undo = 0;
bool g_quit = false;
GLFWwindow* g_window = nullptr;

ImFont* g_ui = nullptr;
ImFont* g_mono = nullptr;

// User preferences (theme + fonts), adjustable via File > Settings.
struct Prefs {
  int theme = 0;      // 0 Dark, 1 Light, 2 Grey
  float font_size = 16.0f;
  int ui_idx = 0;     // index into g_fonts
  int mono_idx = 0;   // index into g_fonts
};
Prefs g_prefs;
std::vector<std::pair<std::string, std::string>> g_fonts;  // {display name, path}
bool g_font_rebuild = false;
bool g_show_settings = false;

// Panel visibility (toggled from the View menu; the panels' own [x] closes them).
bool g_show_editor = true;
bool g_show_preview = true;
bool g_show_document = true;

fs::path g_workdir;
bool g_workdir_is_temp = false;
std::string g_archive_path;
std::string g_last_dir;  // persisted last-visited dir for the file dialog

enum class OpenMode { None, Archive, Folder };
OpenMode g_pending = OpenMode::None;

// ---- assets --------------------------------------------------------------------

fs::path executable_path() {
#if defined(_WIN32)
  char buf[MAX_PATH];
  const DWORD n = GetModuleFileNameA(nullptr, buf, static_cast<DWORD>(sizeof(buf)));
  return n ? fs::path(std::string(buf, n)) : fs::path{};
#elif defined(__APPLE__)
  char buf[4096];
  std::uint32_t size = sizeof(buf);
  if (_NSGetExecutablePath(buf, &size) != 0) return {};
  std::error_code ec;
  auto canon = fs::canonical(buf, ec);
  return ec ? fs::path(buf) : canon;
#else
  std::error_code ec;
  auto p = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path{} : p;
#endif
}

// assets/ next to the executable (deployed by CMake post-build); falls back to
// the source assets dir (a compile-time define) so dev runs work too.
std::string assets_dir() {
  std::error_code ec;
  const fs::path exe = executable_path();
  if (!exe.empty()) {
    const fs::path a = exe.parent_path() / "assets";
    if (fs::exists(a / "fonts", ec)) return a.string();
  }
#ifdef MCDF_STUDIO_ASSETS_DIR
  return MCDF_STUDIO_ASSETS_DIR;
#else
  return "assets";
#endif
}

// ---- persisted UI state (config dir) ------------------------------------------

// Per-user config dir: %APPDATA%\MCDF Studio on Windows, $XDG_CONFIG_HOME or
// ~/.config/mcdf-studio elsewhere. Holds the imgui layout, window geometry, and
// file-dialog bookmarks so they survive across runs.
fs::path config_dir() {
#if defined(_WIN32)
  const char* base = std::getenv("APPDATA");
  fs::path dir = (base && *base) ? fs::path(base) : fs::path(".");
  dir /= "MCDF Studio";
#else
  const char* xdg = std::getenv("XDG_CONFIG_HOME");
  const char* home = std::getenv("HOME");
  fs::path dir = (xdg && *xdg)    ? fs::path(xdg)
                 : (home && *home) ? fs::path(home) / ".config"
                                   : fs::path(".");
  dir /= "mcdf-studio";
#endif
  std::error_code ec;
  fs::create_directories(dir, ec);
  return dir;
}

struct WinGeom {
  int w = 1280, h = 800, x = -1, y = -1;
};

WinGeom g_win_geom;
std::string g_saved_ui_font, g_saved_mono_font;  // resolved to indices after scan_fonts

// Editor preferences: theme, fonts, window geometry, and file-dialog bookmarks.
// This is a separate concern from ImGui's own window-layout ini (imgui.ini),
// which ImGui manages itself - we only point it at a stable path.
void load_preferences() {
  std::ifstream f((config_dir() / "preferences.ini").string());
  if (!f) return;
  auto& bm = g_dialog.Bookmarks();
  bm.clear();
  std::string line;
  while (std::getline(f, line)) {
    const auto eq = line.find('=');
    if (eq == std::string::npos) continue;
    const std::string k = line.substr(0, eq);
    const std::string v = line.substr(eq + 1);
    if (k == "theme") {
      try { g_prefs.theme = std::clamp(std::stoi(v), 0, 2); } catch (...) {}
    } else if (k == "font_size") {
      try { g_prefs.font_size = std::stof(v); } catch (...) {}
    } else if (k == "ui_font") {
      g_saved_ui_font = v;
    } else if (k == "mono_font") {
      g_saved_mono_font = v;
    } else if (k == "window") {
      std::sscanf(v.c_str(), "%d %d %d %d", &g_win_geom.w, &g_win_geom.h,
                  &g_win_geom.x, &g_win_geom.y);
    } else if (k == "last_dir") {
      g_last_dir = v;
    } else if (k == "bookmark") {
      if (!v.empty()) bm.push_back(v);
    }
  }
  g_prefs.font_size = std::clamp(g_prefs.font_size, 10.0f, 32.0f);
  if (g_win_geom.w < 400) g_win_geom.w = 1280;
  if (g_win_geom.h < 300) g_win_geom.h = 800;
}

void save_preferences(GLFWwindow* w) {
  std::ofstream f((config_dir() / "preferences.ini").string(), std::ios::trunc);
  if (!f) return;
  const int n = static_cast<int>(g_fonts.size());
  f << "theme=" << g_prefs.theme << '\n';
  f << "font_size=" << g_prefs.font_size << '\n';
  f << "ui_font=" << (g_prefs.ui_idx < n ? g_fonts[g_prefs.ui_idx].first : "") << '\n';
  f << "mono_font=" << (g_prefs.mono_idx < n ? g_fonts[g_prefs.mono_idx].first : "") << '\n';
  if (w) {
    int ww = 0, wh = 0, wx = 0, wy = 0;
    glfwGetWindowSize(w, &ww, &wh);
    glfwGetWindowPos(w, &wx, &wy);
    f << "window=" << ww << ' ' << wh << ' ' << wx << ' ' << wy << '\n';
  }
  f << "last_dir=" << g_dialog.LastDirectory() << '\n';
  for (const auto& b : g_dialog.Bookmarks()) f << "bookmark=" << b << '\n';
}

// After scan_fonts(): map the saved font names back to combo indices.
void resolve_saved_fonts() {
  for (int i = 0; i < static_cast<int>(g_fonts.size()); ++i) {
    if (!g_saved_ui_font.empty() && g_fonts[i].first == g_saved_ui_font)
      g_prefs.ui_idx = i;
    if (!g_saved_mono_font.empty() && g_fonts[i].first == g_saved_mono_font)
      g_prefs.mono_idx = i;
  }
}

// ---- fonts + theme -------------------------------------------------------------

// Discover the .ttf faces under assets/fonts (icon fonts excluded) and pick the
// Roboto / RobotoMono defaults for the UI and editor.
void scan_fonts() {
  g_fonts.clear();
  const std::string dir = assets_dir() + "/fonts";
  std::error_code ec;
  if (fs::exists(dir, ec)) {
    for (const auto& e : fs::directory_iterator(dir, ec)) {
      if (!e.is_regular_file() || e.path().extension() != ".ttf") continue;
      const std::string stem = e.path().stem().string();
      if (stem.rfind("fa-", 0) == 0) continue;  // icon fonts aren't UI faces
      g_fonts.push_back({stem, e.path().string()});
    }
  }
  std::sort(g_fonts.begin(), g_fonts.end());
  if (g_fonts.empty()) g_fonts.push_back({"Default", ""});
  for (int i = 0; i < static_cast<int>(g_fonts.size()); ++i) {
    if (g_fonts[i].first == "Roboto-Regular") g_prefs.ui_idx = i;
    if (g_fonts[i].first == "RobotoMono-Regular") g_prefs.mono_idx = i;
  }
}

// (Re)build the font atlas from the current prefs. imgui 1.92 builds fonts
// dynamically, so ClearFonts() + re-add is the supported mid-run font swap; the
// user-facing size rides on FontScaleMain.
void rebuild_fonts() {
  g_font_rebuild = false;
  ImGuiIO& io = ImGui::GetIO();
  ImGuiStyle& style = ImGui::GetStyle();
  constexpr float kRef = 16.0f;
  style.FontSizeBase = kRef;
  style.FontScaleMain = g_prefs.font_size / kRef;

  const auto path_of = [](int idx) -> std::string {
    return (idx >= 0 && idx < static_cast<int>(g_fonts.size())) ? g_fonts[idx].second
                                                                : std::string();
  };
  const std::string fonts = assets_dir() + "/fonts";
  const std::string ui = path_of(g_prefs.ui_idx);
  const std::string mono = path_of(g_prefs.mono_idx);
  const std::string fa = fonts + "/fa-solid-900.ttf";
  std::error_code ec;

  io.Fonts->ClearFonts();

  static const ImWchar ui_ranges[] = {
      0x0020, 0x00FF, 0x0100, 0x024F, 0x0370, 0x03FF,
      0x2000, 0x206F, 0x20A0, 0x20CF, 0,
  };
  g_ui = nullptr;
  if (!ui.empty() && fs::exists(ui, ec))
    g_ui = io.Fonts->AddFontFromFileTTF(ui.c_str(), kRef, nullptr, ui_ranges);
  if (!g_ui) {
    ImFontConfig cfg;
    cfg.SizePixels = kRef;
    g_ui = io.Fonts->AddFontDefault(&cfg);
  }

  // Merge FontAwesome solid icons into the UI font (menus + status bar).
  if (fs::exists(fa, ec)) {
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.GlyphMinAdvanceX = kRef;
    static const ImWchar fa_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    io.Fonts->AddFontFromFileTTF(fa.c_str(), kRef, &cfg, fa_ranges);
  }

  // Monospace face for the code editor.
  g_mono = nullptr;
  if (!mono.empty() && fs::exists(mono, ec))
    g_mono = io.Fonts->AddFontFromFileTTF(mono.c_str(), kRef);
  // Merge a broad-coverage monospace fallback (DejaVu Sans Mono) so glyphs the
  // chosen editor face lacks - arrows, math, box drawing, dingbats - render
  // instead of tofu boxes. Merge only fills glyphs the base font is missing.
  const std::string mono_fallback = fonts + "/DejaVuSansMono.ttf";
  if (g_mono && fs::exists(mono_fallback, ec)) {
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    static const ImWchar sym_ranges[] = {
        0x2000, 0x206F,  // General Punctuation (dashes, bullets, ellipsis)
        0x2190, 0x21FF,  // Arrows
        0x2200, 0x22FF,  // Mathematical Operators
        0x2500, 0x257F,  // Box Drawing
        0x25A0, 0x25FF,  // Geometric Shapes
        0x2600, 0x26FF,  // Miscellaneous Symbols
        0x2700, 0x27BF,  // Dingbats
        0,
    };
    io.Fonts->AddFontFromFileTTF(mono_fallback.c_str(), kRef, &cfg, sym_ranges);
  }
  if (!g_mono) g_mono = g_ui;
}

// Match the native window title bar to the theme (Windows draws it light by
// default, which clashes with the dark client area). No-op off Windows.
void set_native_dark_titlebar(bool dark) {
#if defined(_WIN32)
  if (!g_window) return;
  const HWND hwnd = glfwGetWin32Window(g_window);
  const BOOL v = dark ? TRUE : FALSE;
  DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &v, sizeof(v));
#else
  (void)dark;
#endif
}

void apply_theme_dark() {
  ImGui::StyleColorsDark();
  ImVec4* c = ImGui::GetStyle().Colors;
  c[ImGuiCol_Text]              = ImVec4(0.81f, 0.83f, 0.88f, 1.00f);
  c[ImGuiCol_TextDisabled]      = ImVec4(0.81f, 0.83f, 0.88f, 0.50f);
  c[ImGuiCol_WindowBg]          = ImVec4(0.07f, 0.09f, 0.12f, 1.00f);
  c[ImGuiCol_ChildBg]           = ImVec4(0.00f, 0.00f, 0.00f, 0.16f);
  c[ImGuiCol_PopupBg]           = ImVec4(0.07f, 0.09f, 0.12f, 1.00f);
  c[ImGuiCol_Border]            = ImVec4(0.16f, 0.21f, 0.28f, 1.00f);
  c[ImGuiCol_FrameBg]           = ImVec4(0.14f, 0.15f, 0.21f, 0.50f);
  c[ImGuiCol_FrameBgHovered]    = ImVec4(0.00f, 0.47f, 0.93f, 0.25f);
  c[ImGuiCol_FrameBgActive]     = ImVec4(0.00f, 0.47f, 0.93f, 1.00f);
  c[ImGuiCol_TitleBg]           = ImVec4(0.09f, 0.10f, 0.14f, 1.00f);
  c[ImGuiCol_TitleBgActive]     = ImVec4(0.05f, 0.06f, 0.13f, 1.00f);
  c[ImGuiCol_MenuBarBg]         = ImVec4(0.09f, 0.10f, 0.14f, 1.00f);
  c[ImGuiCol_CheckMark]         = ImVec4(0.00f, 0.47f, 0.93f, 1.00f);
  c[ImGuiCol_SliderGrab]        = ImVec4(0.00f, 0.34f, 0.88f, 1.00f);
  c[ImGuiCol_SliderGrabActive]  = ImVec4(0.00f, 0.39f, 0.98f, 1.00f);
  c[ImGuiCol_Button]            = ImVec4(0.31f, 0.40f, 0.44f, 0.50f);
  c[ImGuiCol_ButtonHovered]     = ImVec4(0.12f, 0.15f, 0.21f, 1.00f);
  c[ImGuiCol_ButtonActive]      = ImVec4(0.00f, 0.47f, 0.93f, 1.00f);
  c[ImGuiCol_Header]            = ImVec4(0.12f, 0.15f, 0.21f, 1.00f);
  c[ImGuiCol_HeaderHovered]     = ImVec4(0.00f, 0.47f, 0.93f, 0.25f);
  c[ImGuiCol_HeaderActive]      = ImVec4(0.00f, 0.47f, 0.93f, 1.00f);
  c[ImGuiCol_Tab]               = ImVec4(0.12f, 0.15f, 0.21f, 1.00f);
  c[ImGuiCol_TabHovered]        = ImVec4(0.00f, 0.47f, 0.93f, 0.25f);
  c[ImGuiCol_TabActive]         = ImVec4(0.00f, 0.47f, 0.93f, 1.00f);
  c[ImGuiCol_TabUnfocused]      = ImVec4(0.12f, 0.15f, 0.21f, 1.00f);
  c[ImGuiCol_TabUnfocusedActive]= ImVec4(0.00f, 0.47f, 0.93f, 1.00f);
  c[ImGuiCol_DockingPreview]    = ImVec4(0.00f, 0.40f, 0.98f, 0.70f);
  c[ImGuiCol_TableHeaderBg]     = ImVec4(0.09f, 0.10f, 0.14f, 1.00f);
  c[ImGuiCol_TableBorderStrong] = ImVec4(0.16f, 0.21f, 0.28f, 1.00f);
  c[ImGuiCol_TableBorderLight]  = ImVec4(0.16f, 0.21f, 0.28f, 0.50f);
  c[ImGuiCol_TableRowBgAlt]     = ImVec4(1.00f, 1.00f, 1.00f, 0.04f);
}

void apply_theme_grey() {
  ImGui::StyleColorsDark();
  ImVec4* c = ImGui::GetStyle().Colors;
  c[ImGuiCol_Text]           = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
  c[ImGuiCol_TextDisabled]   = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
  c[ImGuiCol_WindowBg]       = ImVec4(0.13f, 0.13f, 0.13f, 1.00f);
  c[ImGuiCol_PopupBg]        = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
  c[ImGuiCol_MenuBarBg]      = ImVec4(0.16f, 0.16f, 0.16f, 1.00f);
  c[ImGuiCol_Border]         = ImVec4(0.30f, 0.30f, 0.30f, 1.00f);
  c[ImGuiCol_FrameBg]        = ImVec4(0.20f, 0.20f, 0.20f, 0.50f);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.40f, 0.40f, 0.40f, 0.40f);
  c[ImGuiCol_FrameBgActive]  = ImVec4(0.55f, 0.55f, 0.55f, 0.70f);
  c[ImGuiCol_TitleBgActive]  = ImVec4(0.18f, 0.18f, 0.18f, 1.00f);
  c[ImGuiCol_CheckMark]      = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);
  c[ImGuiCol_Button]         = ImVec4(0.28f, 0.28f, 0.28f, 0.50f);
  c[ImGuiCol_ButtonHovered]  = ImVec4(0.38f, 0.38f, 0.38f, 1.00f);
  c[ImGuiCol_ButtonActive]   = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
  c[ImGuiCol_Header]         = ImVec4(0.22f, 0.22f, 0.22f, 1.00f);
  c[ImGuiCol_HeaderHovered]  = ImVec4(0.38f, 0.38f, 0.38f, 0.40f);
  c[ImGuiCol_HeaderActive]   = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);
  c[ImGuiCol_Tab]            = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);
  c[ImGuiCol_TabActive]      = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);
}

void apply_theme(int t) {
  if (t == 1) ImGui::StyleColorsLight();
  else if (t == 2) apply_theme_grey();
  else apply_theme_dark();
  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 4.0f;
  s.FrameRounding = 2.0f;
  s.TabRounding = 2.0f;
  s.ScrollbarRounding = 3.0f;
  s.GrabRounding = 2.0f;
  set_native_dark_titlebar(t != 1);  // light title bar only for the Light theme
}

// ---- small filesystem helpers --------------------------------------------------

std::string read_file_bytes(const std::string& path, bool& ok) {
  std::ifstream in(path, std::ios::binary);
  ok = static_cast<bool>(in);
  if (!ok) return {};
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

bool write_file_bytes(const std::string& path, std::string_view bytes) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if (!out) return false;
  out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  return static_cast<bool>(out);
}

fs::path make_workdir() {
  static int counter = 0;
  std::error_code ec;
  const fs::path dir = fs::temp_directory_path(ec) / "mcdf-studio" /
                       ("work-" + std::to_string(++counter));
  fs::remove_all(dir, ec);
  fs::create_directories(dir, ec);
  return dir;
}

void cleanup_workdir() {
  if (g_workdir_is_temp && !g_workdir.empty()) {
    std::error_code ec;
    fs::remove_all(g_workdir, ec);
  }
  g_workdir.clear();
  g_workdir_is_temp = false;
  g_archive_path.clear();
}

bool is_dirty() {
  return g_editor && g_doc && g_editor->GetUndoIndex() != g_saved_undo;
}

// ---- open / save ---------------------------------------------------------------

void load_from_workdir(const std::string& display_path, bool is_archive) {
  OpenDoc d;
  d.path = display_path;
  d.is_archive = is_archive;

  auto container = mcdf::open_container(g_workdir);
  if (!container) {
    d.status = "open failed: " + container.error().message;
    if (g_editor) g_editor->SetText("");
    g_doc = std::move(d);
    return;
  }
  mcdf::Container& c = **container;

  if (auto files = c.list()) d.files = *files;

  std::string content;
  if (c.contains("content.md")) {
    if (auto raw = c.read("content.md")) {
      content = *raw;
      d.has_content = true;
    } else {
      d.status = "read content.md failed: " + raw.error().message;
    }
  }

  if (auto doc = mcdf::load_document(c)) {
    if (doc->has_metadata) d.title = doc->metadata.title;
    if (doc->has_schema) d.doc_type = doc->schema.document_type;
    d.heading_count = static_cast<int>(doc->headings.size());
  }

  if (g_editor) {
    g_editor->SetText(content);
    g_editor->SetReadOnlyEnabled(false);
    g_saved_undo = g_editor->GetUndoIndex();
  }

  if (d.status.empty()) d.status = "opened " + display_path;
  g_doc = std::move(d);
}

void open_archive(const std::string& mcdf_path) {
  cleanup_workdir();
  bool ok = false;
  const std::string bytes = read_file_bytes(mcdf_path, ok);
  if (!ok) {
    OpenDoc d;
    d.path = mcdf_path;
    d.status = "cannot read " + mcdf_path;
    g_doc = std::move(d);
    return;
  }
  const fs::path work = make_workdir();
  if (auto un = mcdf::unpack_archive(bytes, work); !un) {
    std::error_code ec;
    fs::remove_all(work, ec);
    OpenDoc d;
    d.path = mcdf_path;
    d.status = "unpack failed: " + un.error().message;
    g_doc = std::move(d);
    return;
  }
  g_workdir = work;
  g_workdir_is_temp = true;
  g_archive_path = mcdf_path;
  load_from_workdir(mcdf_path, /*is_archive=*/true);
}

void open_folder(const std::string& dir_path) {
  cleanup_workdir();
  g_workdir = fs::path(dir_path);
  g_workdir_is_temp = false;
  g_archive_path.clear();
  load_from_workdir(dir_path, /*is_archive=*/false);
}

void save_document() {
  if (!g_editor || !g_doc || g_workdir.empty()) return;

  auto dir = mcdf::DirectoryContainer::open(g_workdir);
  if (!dir) {
    g_doc->status = "save failed: " + dir.error().message;
    return;
  }

  const std::string text = g_editor->GetText();
  if (auto w = (*dir)->write("content.md", text); !w) {
    g_doc->status = "save failed: " + w.error().message;
    return;
  }

  // Keep integrity consistent: rebuild the manifest if the document carries one
  // (invalidates any existing signatures by design; re-signing is a later step).
  if ((*dir)->contains("manifest.json")) {
    if (auto container = mcdf::open_container(g_workdir)) {
      if (auto m = mcdf::build_manifest(**container)) {
        if (auto json = mcdf::manifest_to_canonical_json(*m))
          (void)(*dir)->write("manifest.json", *json);
      }
    }
  }

  // For a .mcdf document, re-pack the working copy back into the single file.
  if (g_doc->is_archive && !g_archive_path.empty()) {
    auto container = mcdf::open_container(g_workdir);
    if (!container) {
      g_doc->status = "save failed: " + container.error().message;
      return;
    }
    auto archive = mcdf::pack_container(**container);
    if (!archive) {
      g_doc->status = "save failed: " + archive.error().message;
      return;
    }
    if (!write_file_bytes(g_archive_path, *archive)) {
      g_doc->status = "save failed: cannot write " + g_archive_path;
      return;
    }
    g_doc->status =
        "saved " + g_archive_path + " (" + std::to_string(archive->size()) + " bytes)";
  } else {
    g_doc->status = "saved content.md";
  }

  g_saved_undo = g_editor->GetUndoIndex();
}

// ---- UI ------------------------------------------------------------------------

void open_archive_dialog() {
  imfd::Config cfg;
  cfg.title = "Open MCDF document";
  cfg.mode = imfd::Mode::OpenFile;
  cfg.filters = {{"MCDF document", {".mcdf"}}, {"All files", {"*"}}};
  if (!g_last_dir.empty()) { cfg.start_dir = g_last_dir; g_last_dir.clear(); }
  g_pending = OpenMode::Archive;
  g_dialog.Open(cfg);
}

void draw_menu_bar() {
  if (!ImGui::BeginMenuBar()) return;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open .mcdf document...", "Ctrl+O"))
      open_archive_dialog();
    if (ImGui::MenuItem(ICON_FA_FOLDER "  Open unpacked folder...")) {
      imfd::Config cfg;
      cfg.title = "Open MCDF container folder";
      cfg.mode = imfd::Mode::PickFolder;
      if (!g_last_dir.empty()) { cfg.start_dir = g_last_dir; g_last_dir.clear(); }
      g_pending = OpenMode::Folder;
      g_dialog.Open(cfg);
    }
    ImGui::Separator();
    const bool can_save = g_doc && g_doc->has_content;
    if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save", "Ctrl+S", false, can_save))
      save_document();
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_GEAR "  Settings...", "Ctrl+,"))
      g_show_settings = true;
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_RIGHT_FROM_BRACKET "  Quit", "Ctrl+Q"))
      g_quit = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem(ICON_FA_PEN "  Editor", nullptr, &g_show_editor);
    ImGui::MenuItem(ICON_FA_FILE_LINES "  Preview", nullptr, &g_show_preview);
    ImGui::MenuItem(ICON_FA_CIRCLE_INFO "  Document", nullptr, &g_show_document);
    ImGui::EndMenu();
  }
  ImGui::EndMenuBar();
}

void draw_host() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowViewport(vp->ID);

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

  // A slightly taller menu bar: its height is fixed at Begin() from FramePadding.
  const float def_pad_y = ImGui::GetStyle().FramePadding.y;
  const float status_h = ImGui::GetFontSize() + def_pad_y * 2.0f +
                         ImGui::GetStyle().WindowPadding.y * 2.0f;

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                      ImVec2(ImGui::GetStyle().FramePadding.x, def_pad_y + 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("##StudioHost", nullptr, flags);
  ImGui::PopStyleVar(3);  // rounding/border/window-padding; taller FramePadding stays

  ImGui::DockSpace(ImGui::GetID("StudioDock"), ImVec2(0.0f, -status_h));
  draw_menu_bar();
  ImGui::PopStyleVar();  // taller FramePadding
  ImGui::End();
}

void draw_status_bar() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const float h = ImGui::GetFrameHeight() + ImGui::GetStyle().WindowPadding.y * 2.0f;
  ImGui::SetNextWindowPos(ImVec2(vp->Pos.x, vp->Pos.y + vp->Size.y - h));
  ImGui::SetNextWindowSize(ImVec2(vp->Size.x, h));

  const ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoScrollbar;

  ImGui::PushStyleColor(ImGuiCol_WindowBg,
                        ImGui::GetStyle().Colors[ImGuiCol_MenuBarBg]);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 4.0f));
  if (ImGui::Begin("##StatusBar", nullptr, flags)) {
    if (!g_doc) {
      ImGui::TextDisabled(ICON_FA_FILE "  No document open");
    } else {
      ImGui::TextColored(kAccent, ICON_FA_FILE_LINES);
      ImGui::SameLine();
      ImGui::Text("%s", g_doc->path.c_str());
      if (is_dirty()) {
        ImGui::SameLine();
        ImGui::TextColored(kDirty, ICON_FA_PEN " unsaved");
      }
      // Right side: document type / kind.
      const std::string right =
          (g_doc->doc_type.empty() ? std::string("document") : g_doc->doc_type) +
          (g_doc->is_archive ? "  |  .mcdf" : "  |  folder");
      const float tw = ImGui::CalcTextSize(right.c_str()).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - tw - 12.0f);
      ImGui::TextDisabled("%s", right.c_str());
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void draw_settings() {
  if (g_show_settings) {
    ImGui::OpenPopup("Settings");
    g_show_settings = false;
  }
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::SetNextWindowSize(ImVec2(460.0f, 0.0f), ImGuiCond_Appearing);

  bool open = true;
  if (!ImGui::BeginPopupModal("Settings", &open, ImGuiWindowFlags_AlwaysAutoResize))
    return;

  const auto section = [](const char* label) {
    ImGui::Spacing();
    ImGui::SeparatorText(label);
    ImGui::Spacing();
  };

  section("Appearance");
  const char* themes[] = {"Dark", "Light", "Grey"};
  ImGui::SetNextItemWidth(200.0f);
  if (ImGui::BeginCombo("Theme", themes[g_prefs.theme])) {
    for (int i = 0; i < 3; ++i)
      if (ImGui::Selectable(themes[i], i == g_prefs.theme)) {
        g_prefs.theme = i;
        apply_theme(i);  // live preview
      }
    ImGui::EndCombo();
  }

  section("Font");
  ImGui::SetNextItemWidth(200.0f);
  if (ImGui::SliderFloat("Size", &g_prefs.font_size, 10.0f, 32.0f, "%.0f px"))
    ImGui::GetStyle().FontScaleMain = g_prefs.font_size / 16.0f;
  ImGui::SameLine();
  if (ImGui::Button("Reset")) {
    g_prefs.font_size = 16.0f;
    ImGui::GetStyle().FontScaleMain = 1.0f;
  }

  ImGui::SetNextItemWidth(300.0f);
  if (ImGui::BeginCombo("UI font", g_fonts[g_prefs.ui_idx].first.c_str())) {
    for (int i = 0; i < static_cast<int>(g_fonts.size()); ++i)
      if (ImGui::Selectable(g_fonts[i].first.c_str(), i == g_prefs.ui_idx)) {
        g_prefs.ui_idx = i;
        g_font_rebuild = true;
      }
    ImGui::EndCombo();
  }
  ImGui::SetNextItemWidth(300.0f);
  if (ImGui::BeginCombo("Editor font", g_fonts[g_prefs.mono_idx].first.c_str())) {
    for (int i = 0; i < static_cast<int>(g_fonts.size()); ++i)
      if (ImGui::Selectable(g_fonts[i].first.c_str(), i == g_prefs.mono_idx)) {
        g_prefs.mono_idx = i;
        g_font_rebuild = true;
      }
    ImGui::EndCombo();
  }
  ImGui::TextDisabled("Editor font applies to the content.md pane (use a monospace face).");

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void draw_document_panel() {
  if (!g_show_document) return;
  if (ImGui::Begin(ICON_FA_CIRCLE_INFO "  Document", &g_show_document)) {
    if (!g_doc) {
      ImGui::TextUnformatted("No document open.");
      ImGui::Spacing();
      if (ImGui::Button(ICON_FA_FOLDER_OPEN "  Open .mcdf document..."))
        open_archive_dialog();
    } else {
      ImGui::TextWrapped("File: %s", g_doc->path.c_str());
      ImGui::TextDisabled(g_doc->is_archive ? "(.mcdf document)"
                                            : "(unpacked folder)");
      ImGui::TextWrapped("%s", g_doc->status.c_str());
      ImGui::Separator();
      if (!g_doc->title.empty()) ImGui::Text("Title: %s", g_doc->title.c_str());
      if (!g_doc->doc_type.empty())
        ImGui::Text("Type:  %s", g_doc->doc_type.c_str());
      ImGui::Text("Headings: %d", g_doc->heading_count);
      ImGui::Text("Members:  %d", static_cast<int>(g_doc->files.size()));
      ImGui::Separator();
      for (const auto& f : g_doc->files) ImGui::BulletText("%s", f.c_str());
    }
  }
  ImGui::End();
}

void draw_editor_panel() {
  if (!g_show_editor) return;
  if (ImGui::Begin(ICON_FA_PEN "  Editor", &g_show_editor)) {
    if (g_doc && g_doc->has_content && g_editor) {
      if (g_mono)
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19200
        ImGui::PushFont(g_mono, 0.0f);
#else
        ImGui::PushFont(g_mono);
#endif
      g_editor->Render("##editor", ImGui::GetContentRegionAvail(), false);
      if (g_mono) ImGui::PopFont();
    } else {
      ImGui::TextUnformatted("(open a document with a content.md)");
    }
  }
  ImGui::End();
}

void draw_preview_panel() {
  if (!g_show_preview) return;
  if (ImGui::Begin(ICON_FA_FILE_LINES "  Preview", &g_show_preview)) {
    if (g_doc && g_doc->has_content && g_editor) {
      const std::string text = g_editor->GetText();
      static MarkdownView md;
      md.print(text.c_str(), text.c_str() + text.size());
    } else {
      ImGui::TextUnformatted("(no preview)");
    }
  }
  ImGui::End();
}

void handle_shortcuts() {
  const ImGuiIO& io = ImGui::GetIO();
  if (!io.KeyCtrl) return;
  if (ImGui::IsKeyPressed(ImGuiKey_S) && g_doc && g_doc->has_content) save_document();
  if (ImGui::IsKeyPressed(ImGuiKey_O)) open_archive_dialog();
  if (ImGui::IsKeyPressed(ImGuiKey_Q)) g_quit = true;
  if (ImGui::IsKeyPressed(ImGuiKey_Comma)) g_show_settings = true;
}

void glfw_error_callback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

}  // namespace

int main() {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) return 1;

  load_preferences();  // theme, fonts, window geometry, dialog bookmarks

  // GL context hints. macOS only grants OpenGL 3.2+ Core, forward-compatible;
  // Linux/Windows are happy with a 3.0 compatibility context.
#if defined(__APPLE__)
  const char* glsl_version = "#version 150";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#else
  const char* glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);
#endif

  GLFWwindow* window = glfwCreateWindow(g_win_geom.w, g_win_geom.h, "MCDF Studio",
                                        nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  g_window = window;
  if (g_win_geom.x >= 0 && g_win_geom.y >= 0)
    glfwSetWindowPos(window, g_win_geom.x, g_win_geom.y);
  glfwSwapInterval(1);  // vsync

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  static std::string ini_path = (config_dir() / "imgui.ini").string();
  io.IniFilename = ini_path.c_str();  // stable layout persistence (not CWD-relative)
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  scan_fonts();
  resolve_saved_fonts();  // map saved font names -> combo indices
  rebuild_fonts();
  apply_theme(g_prefs.theme);

  // Create the editor once the ImGui context exists; bind Markdown syntax.
  g_editor = std::make_unique<TextEditor>();
  g_editor->SetLanguage(TextEditor::Language::Markdown());
  g_editor->SetPalette(TextEditor::GetDarkPalette());
  g_editor->SetReadOnlyEnabled(true);

  while (!glfwWindowShouldClose(window) && !g_quit) {
    glfwPollEvents();
    if (g_font_rebuild) rebuild_fonts();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    handle_shortcuts();
    draw_host();
    const imfd::Result dlg_res = g_dialog.Draw();
    if (dlg_res == imfd::Result::Picked) {
      const std::string picked = g_dialog.SelectedPath();
      if (g_pending == OpenMode::Archive) open_archive(picked);
      else if (g_pending == OpenMode::Folder) open_folder(picked);
      g_pending = OpenMode::None;
    }
    if (dlg_res != imfd::Result::None) save_preferences(g_window);  // persist bookmarks/last dir
    draw_document_panel();
    draw_editor_panel();
    draw_preview_panel();
    draw_status_bar();
    draw_settings();

    ImGui::Render();
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
    glClearColor(bg.x, bg.y, bg.z, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  save_preferences(window);
  cleanup_workdir();
  g_editor.reset();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
