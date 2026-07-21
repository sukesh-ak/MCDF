// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// MCDF Studio - multi-document, document-centric editor.
//
// Links libmcdf directly (no CLI/FFI). A single .mcdf file is "the document"
// (like .docx): opening one unpacks it to a hidden temp working copy; Save
// re-packs it. Each open file is its own dockable window containing an
// editor | preview split (source Markdown editor + live imgui_md/md4c preview).
// Images attach into the container's assets/ and render in the preview.
//
// Layout is ImGui's own (mcdf.ini at a stable path); theme/fonts/window/dialog
// bookmarks live in a separate preferences.ini. Shell look adapted from YMOVE.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>  // pulls in <GL/gl.h> for textures + the frame clear

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX  // keep windows.h from defining min/max macros (breaks std::min/max)
#endif
#include <windows.h>
#include <dwmapi.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
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

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace fs = std::filesystem;

namespace {

const ImVec4 kAccent{0.00f, 0.47f, 0.93f, 1.0f};
const ImVec4 kDirty{1.00f, 0.70f, 0.20f, 1.0f};

// An open document: its own editor, working copy, and save state.
struct Document {
  int id = 0;
  bool open = true;         // window open flag ([x] closes it)
  float split = 0.5f;       // editor|preview split ratio

  std::string path;         // .mcdf file or folder shown to the user
  std::string status;
  std::vector<std::string> files;
  bool has_content = false;
  bool is_archive = false;  // opened from a .mcdf file
  std::string title;        // metadata.title
  std::string doc_type;     // schema.document_type
  int heading_count = 0;

  // E2: editable metadata.yaml / schema.yaml models + the live outline.
  mcdf::Metadata meta;
  mcdf::Schema schema;
  bool write_meta = false;    // metadata.yaml exists or was edited -> written on save
  bool write_schema = false;  // same for schema.yaml
  bool props_dirty = false;   // unsaved metadata/schema edits
  std::vector<mcdf::Heading> outline;  // headings of the current editor buffer
  std::vector<int> outline_lines;      // 0-based editor line per outline entry (-1 unknown)
  std::size_t outline_undo = static_cast<std::size_t>(-1);  // undo index the cache was built at

  std::unique_ptr<TextEditor> editor;
  std::size_t saved_undo = 0;
  fs::path workdir;
  bool workdir_is_temp = false;
  std::string archive_path;  // .mcdf to save back to (empty in folder mode)
};

// imgui_md subclass: image textures + a visible link color. get_image resolves
// against g_render_workdir (the document currently being previewed).
struct MarkdownView : imgui_md {
  bool get_image(image_info& nfo) const override;  // defined after the texture cache
  ImFont* get_font() const override;               // defined after g_mono is declared
  ImVec4 get_color() const override {
    if (!m_href.empty()) return ImVec4(0.32f, 0.66f, 1.00f, 1.00f);
    return ImGui::GetStyle().Colors[ImGuiCol_Text];
  }
};

std::vector<std::unique_ptr<Document>> g_documents;
int g_next_doc_id = 1;
Document* g_active = nullptr;     // focused document (menu ops target it)
Document* g_target_doc = nullptr; // document a Save As / Insert dialog acts on
ImGuiID g_dockspace_id = 0;       // main dockspace; new documents dock here by default

imfd::FileDialog g_dialog;
ImFont* g_ui = nullptr;
ImFont* g_mono = nullptr;

struct Prefs {
  int theme = 0;      // 0 Dark, 1 Light, 2 Grey
  float font_size = 16.0f;
  int ui_idx = 0;
  int mono_idx = 0;
  bool idle_throttle = true;  // lower frame rate when there's no input
  std::string wallpaper_path; // optional background image
  float wallpaper_opacity = 0.35f;
};
Prefs g_prefs;
std::vector<std::pair<std::string, std::string>> g_fonts;
bool g_font_rebuild = false;
bool g_show_settings = false;

// E2 panels (View menu): outline + schema binding, metadata form, schema form.
bool g_show_structure = true;
bool g_show_metadata = false;
bool g_show_schema = false;

bool g_quit = false;
GLFWwindow* g_window = nullptr;
double g_last_input = 0.0;  // glfwGetTime() of the last user input (idle throttle)
std::string g_last_dir;
std::vector<std::string> g_recent;  // recently opened documents (most-recent first)
fs::path g_render_workdir;  // working dir of the document being previewed (for images)

enum class OpenMode { None, Archive, Folder, SaveAs, InsertImage, Wallpaper };
OpenMode g_pending = OpenMode::None;

bool is_dirty(const Document& d) {
  return d.props_dirty || (d.editor && d.editor->GetUndoIndex() != d.saved_undo);
}

// Reparse the outline when the editor buffer changed (undo index moved), and
// map each heading to its editor line: an ordered scan over ATX (and setext)
// heading lines, skipping fenced code blocks. libmcdf's parse_headings stays
// the authority on what a heading *is* (levels, {#id} binding); the line scan
// only locates them for click-to-jump.
const std::vector<mcdf::Heading>& outline_of(Document& d) {
  if (!d.editor) return d.outline;
  const std::size_t undo = d.editor->GetUndoIndex();
  if (undo == d.outline_undo) return d.outline;
  d.outline_undo = undo;
  d.outline.clear();
  d.outline_lines.clear();
  const std::string text = d.editor->GetText();
  if (auto h = mcdf::parse_headings(text)) d.outline = std::move(*h);
  d.heading_count = static_cast<int>(d.outline.size());

  std::vector<int> lines;
  bool in_fence = false;
  int line_no = 0;
  std::string prev;  // previous line (for setext underlines)
  std::size_t pos = 0;
  while (pos <= text.size()) {
    const std::size_t eol = text.find('\n', pos);
    const std::string line =
        text.substr(pos, (eol == std::string::npos ? text.size() : eol) - pos);
    std::size_t i = 0;
    while (i < line.size() && i < 3 && line[i] == ' ') ++i;
    const std::string_view body = std::string_view(line).substr(i);
    if (body.rfind("```", 0) == 0 || body.rfind("~~~", 0) == 0) {
      in_fence = !in_fence;
    } else if (!in_fence) {
      std::size_t hashes = 0;
      while (hashes < body.size() && body[hashes] == '#') ++hashes;
      const bool atx = hashes >= 1 && hashes <= 6 &&
                       (hashes == body.size() || body[hashes] == ' ' ||
                        body[hashes] == '\t');
      const bool setext =
          !body.empty() && !prev.empty() &&
          body.find_first_not_of(body[0] == '=' ? "= \t" : "- \t") ==
              std::string_view::npos &&
          (body[0] == '=' || body[0] == '-') &&
          prev.find_first_not_of(" \t") != std::string::npos;
      if (atx) lines.push_back(line_no);
      else if (setext && (lines.empty() || lines.back() != line_no - 1))
        lines.push_back(line_no - 1);  // the underline names the line above
    }
    prev = line;
    ++line_no;
    if (eol == std::string::npos) break;
    pos = eol + 1;
  }
  // The scan and md4c agree on ordering for well-formed documents; align by
  // index and leave any tail unmapped rather than guessing.
  d.outline_lines.assign(d.outline.size(), -1);
  for (std::size_t k = 0; k < d.outline.size() && k < lines.size(); ++k)
    d.outline_lines[k] = lines[k];
  return d.outline;
}

// True if some heading in the current outline carries this explicit {#id}.
bool outline_has_id(const Document& d, const std::string& id) {
  if (id.empty()) return false;
  for (const auto& h : d.outline)
    if (h.id == id) return true;
  return false;
}

// ---- preview image textures ----------------------------------------------------
#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif

struct Tex {
  unsigned int id = 0;
  int w = 0, h = 0;
};
std::unordered_map<std::string, Tex> g_tex_cache;

const Tex* load_texture(const std::string& href) {
  fs::path p(href);
  if (p.is_relative() && !g_render_workdir.empty()) p = g_render_workdir / p;
  const std::string key = p.string();
  if (auto it = g_tex_cache.find(key); it != g_tex_cache.end())
    return it->second.id ? &it->second : nullptr;

  Tex t;
  int n = 0;
  unsigned char* data = stbi_load(key.c_str(), &t.w, &t.h, &n, 4);
  if (data) {
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, data);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(data);
  }
  const auto res = g_tex_cache.insert_or_assign(key, t);
  return res.first->second.id ? &res.first->second : nullptr;
}

void clear_texture_cache() {
  for (auto& kv : g_tex_cache)
    if (kv.second.id) { unsigned int id = kv.second.id; glDeleteTextures(1, &id); }
  g_tex_cache.clear();
}

// Optional background image (behind the dockspace), like the YMOVE studio.
unsigned int g_wallpaper_tex = 0;
int g_wallpaper_w = 0, g_wallpaper_h = 0;

void unload_wallpaper() {
  if (g_wallpaper_tex) {
    unsigned int t = g_wallpaper_tex;
    glDeleteTextures(1, &t);
    g_wallpaper_tex = 0;
    g_wallpaper_w = g_wallpaper_h = 0;
  }
}

void load_wallpaper(const std::string& path) {
  unload_wallpaper();
  if (path.empty()) return;
  int n = 0;
  unsigned char* data = stbi_load(path.c_str(), &g_wallpaper_w, &g_wallpaper_h, &n, 4);
  if (!data) { g_wallpaper_w = g_wallpaper_h = 0; return; }
  glGenTextures(1, &g_wallpaper_tex);
  glBindTexture(GL_TEXTURE_2D, g_wallpaper_tex);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, g_wallpaper_w, g_wallpaper_h, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, data);
  glBindTexture(GL_TEXTURE_2D, 0);
  stbi_image_free(data);
}

// Aspect-fill the viewport on the background draw list (behind all windows).
void render_wallpaper() {
  if (!g_wallpaper_tex || g_wallpaper_w <= 0 || g_wallpaper_h <= 0) return;
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  const ImVec2 pos = vp->Pos, size = vp->Size;
  const float va = size.x / size.y;
  const float ia = static_cast<float>(g_wallpaper_w) / static_cast<float>(g_wallpaper_h);
  ImVec2 draw;
  if (va > ia) { draw.x = size.x; draw.y = size.x / ia; }
  else { draw.y = size.y; draw.x = size.y * ia; }
  const ImVec2 p0 = {pos.x + (size.x - draw.x) * 0.5f, pos.y + (size.y - draw.y) * 0.5f};
  const ImVec2 p1 = {p0.x + draw.x, p0.y + draw.y};
  const int a = static_cast<int>(std::clamp(g_prefs.wallpaper_opacity, 0.0f, 1.0f) * 255.0f);
  ImGui::GetBackgroundDrawList()->AddImage(
      static_cast<ImTextureID>(g_wallpaper_tex), p0, p1, ImVec2(0, 0), ImVec2(1, 1),
      IM_COL32(255, 255, 255, a));
}

bool MarkdownView::get_image(image_info& nfo) const {
  if (m_href.empty()) return false;
  const Tex* t = load_texture(m_href);
  if (!t) return false;
  nfo.texture_id = static_cast<ImTextureID>(t->id);
  nfo.size = ImVec2(static_cast<float>(t->w), static_cast<float>(t->h));
  nfo.uv0 = ImVec2(0, 0);
  nfo.uv1 = ImVec2(1, 1);
  nfo.col_tint = ImVec4(1, 1, 1, 1);
  nfo.col_border = ImVec4(0, 0, 0, 0);
  return true;
}

// Code (fenced blocks + inline spans) renders in the monospace face; everything
// else uses the default UI font.
ImFont* MarkdownView::get_font() const {
  if (m_is_code && g_mono) return g_mono;
  return nullptr;  // default UI font (imgui_md handles heading sizing itself)
}

// ---- assets + config -----------------------------------------------------------

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
std::string g_saved_ui_font, g_saved_mono_font;

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
    } else if (k == "idle_throttle") {
      g_prefs.idle_throttle = (v != "0");
    } else if (k == "panel_structure") {
      g_show_structure = (v != "0");
    } else if (k == "panel_metadata") {
      g_show_metadata = (v != "0");
    } else if (k == "panel_schema") {
      g_show_schema = (v != "0");
    } else if (k == "wallpaper") {
      g_prefs.wallpaper_path = v;
    } else if (k == "wallpaper_opacity") {
      try { g_prefs.wallpaper_opacity = std::stof(v); } catch (...) {}
    } else if (k == "window") {
      std::sscanf(v.c_str(), "%d %d %d %d", &g_win_geom.w, &g_win_geom.h,
                  &g_win_geom.x, &g_win_geom.y);
    } else if (k == "last_dir") {
      g_last_dir = v;
    } else if (k == "bookmark") {
      if (!v.empty()) bm.push_back(v);
    } else if (k == "recent") {
      if (!v.empty()) g_recent.push_back(v);
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
  f << "idle_throttle=" << (g_prefs.idle_throttle ? 1 : 0) << '\n';
  f << "panel_structure=" << (g_show_structure ? 1 : 0) << '\n';
  f << "panel_metadata=" << (g_show_metadata ? 1 : 0) << '\n';
  f << "panel_schema=" << (g_show_schema ? 1 : 0) << '\n';
  f << "wallpaper=" << g_prefs.wallpaper_path << '\n';
  f << "wallpaper_opacity=" << g_prefs.wallpaper_opacity << '\n';
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
  for (const auto& r : g_recent) f << "recent=" << r << '\n';
}

// ---- fonts + theme -------------------------------------------------------------

void scan_fonts() {
  g_fonts.clear();
  const std::string dir = assets_dir() + "/fonts";
  std::error_code ec;
  if (fs::exists(dir, ec)) {
    for (const auto& e : fs::directory_iterator(dir, ec)) {
      if (!e.is_regular_file() || e.path().extension() != ".ttf") continue;
      const std::string stem = e.path().stem().string();
      if (stem.rfind("fa-", 0) == 0) continue;
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

void resolve_saved_fonts() {
  for (int i = 0; i < static_cast<int>(g_fonts.size()); ++i) {
    if (!g_saved_ui_font.empty() && g_fonts[i].first == g_saved_ui_font) g_prefs.ui_idx = i;
    if (!g_saved_mono_font.empty() && g_fonts[i].first == g_saved_mono_font) g_prefs.mono_idx = i;
  }
}

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

  if (fs::exists(fa, ec)) {
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    cfg.GlyphMinAdvanceX = kRef;
    static const ImWchar fa_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    io.Fonts->AddFontFromFileTTF(fa.c_str(), kRef, &cfg, fa_ranges);
  }

  g_mono = nullptr;
  if (!mono.empty() && fs::exists(mono, ec))
    g_mono = io.Fonts->AddFontFromFileTTF(mono.c_str(), kRef);
  const std::string mono_fallback = fonts + "/DejaVuSansMono.ttf";
  if (g_mono && fs::exists(mono_fallback, ec)) {
    ImFontConfig cfg;
    cfg.MergeMode = true;
    cfg.PixelSnapH = true;
    static const ImWchar sym_ranges[] = {
        0x2000, 0x206F, 0x2190, 0x21FF, 0x2200, 0x22FF,
        0x2500, 0x257F, 0x25A0, 0x25FF, 0x2600, 0x26FF, 0x2700, 0x27BF, 0,
    };
    io.Fonts->AddFontFromFileTTF(mono_fallback.c_str(), kRef, &cfg, sym_ranges);
  }
  if (!g_mono) g_mono = g_ui;
}

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
  set_native_dark_titlebar(t != 1);
}

// ---- filesystem helpers --------------------------------------------------------

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

// ---- document operations -------------------------------------------------------

Document* new_document() {
  auto d = std::make_unique<Document>();
  d->id = g_next_doc_id++;
  d->editor = std::make_unique<TextEditor>();
  d->editor->SetLanguage(TextEditor::Language::Markdown());
  d->editor->SetPalette(TextEditor::GetDarkPalette());
  Document* ptr = d.get();
  g_documents.push_back(std::move(d));
  g_active = ptr;
  return ptr;
}

// Read the container in d.workdir into d + the editor.
void load_into(Document& d) {
  auto container = mcdf::open_container(d.workdir);
  if (!container) {
    d.status = "open failed: " + container.error().message;
    d.editor->SetText("");
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
  d.meta = {};
  d.schema = {};
  d.write_meta = d.write_schema = d.props_dirty = false;
  if (auto doc = mcdf::load_document(c)) {
    if (doc->has_metadata) {
      d.meta = doc->metadata;
      d.title = doc->metadata.title;
    }
    d.write_meta = doc->has_metadata;
    if (doc->has_schema) {
      d.schema = doc->schema;
      d.doc_type = doc->schema.document_type;
    }
    d.write_schema = doc->has_schema;
    d.heading_count = static_cast<int>(doc->headings.size());
  }
  d.editor->SetText(content);
  d.editor->SetReadOnlyEnabled(false);
  d.saved_undo = d.editor->GetUndoIndex();
  d.outline_undo = static_cast<std::size_t>(-1);  // force an outline reparse
  if (d.status.empty()) d.status = "opened " + d.path;
}

// Point a document at a .mcdf file: unpack to a fresh temp working copy.
void set_archive(Document& d, const std::string& path) {
  std::error_code ec;
  if (d.workdir_is_temp && !d.workdir.empty()) fs::remove_all(d.workdir, ec);
  d.path = path;
  d.is_archive = true;
  d.archive_path = path;
  d.files.clear();
  d.has_content = false;
  d.title.clear();
  d.doc_type.clear();
  d.heading_count = 0;
  d.meta = {};
  d.schema = {};
  d.write_meta = d.write_schema = d.props_dirty = false;
  d.outline_undo = static_cast<std::size_t>(-1);
  d.status.clear();

  bool ok = false;
  const std::string bytes = read_file_bytes(path, ok);
  if (!ok) {
    d.workdir.clear();
    d.workdir_is_temp = false;
    d.status = "cannot read " + path;
    d.editor->SetText("");
    return;
  }
  const fs::path work = make_workdir();
  if (auto un = mcdf::unpack_archive(bytes, work); !un) {
    fs::remove_all(work, ec);
    d.workdir.clear();
    d.workdir_is_temp = false;
    d.status = "unpack failed: " + un.error().message;
    d.editor->SetText("");
    return;
  }
  d.workdir = work;
  d.workdir_is_temp = true;
  load_into(d);
}

void add_recent(const std::string& path) {
  if (path.empty()) return;
  g_recent.erase(std::remove(g_recent.begin(), g_recent.end(), path), g_recent.end());
  g_recent.insert(g_recent.begin(), path);
  if (g_recent.size() > 12) g_recent.resize(12);
}

void open_archive(const std::string& mcdf_path) {
  set_archive(*new_document(), mcdf_path);
  add_recent(mcdf_path);
}

void open_folder(const std::string& dir_path) {
  Document* d = new_document();
  d->path = dir_path;
  d->is_archive = false;
  d->workdir = fs::path(dir_path);
  d->workdir_is_temp = false;
  load_into(*d);
  add_recent(dir_path);
}

// Reopen a path from the recent list (dispatch by directory vs .mcdf file).
void open_path(const std::string& path) {
  std::error_code ec;
  if (fs::is_directory(path, ec)) open_folder(path);
  else open_archive(path);
}

// Create a new, empty document backed by a temporary working copy. It stays
// "untitled" (no path) until the first Save, which opens the Save As dialog.
void new_blank_document() {
  Document* d = new_document();
  const fs::path work = make_workdir();
  if (auto dir = mcdf::DirectoryContainer::open(work))
    (void)(*dir)->write("content.md", "");
  // Best-effort: seed a manifest so the first Save yields a well-formed .mcdf.
  if (auto c = mcdf::open_container(work))
    if (auto m = mcdf::build_manifest(**c))
      if (auto j = mcdf::manifest_to_canonical_json(*m))
        if (auto dir = mcdf::DirectoryContainer::open(work))
          (void)(*dir)->write("manifest.json", *j);
  d->workdir = work;
  d->workdir_is_temp = true;
  d->is_archive = false;
  d->archive_path.clear();
  d->path.clear();  // untitled
  d->has_content = true;
  d->editor->SetText("");
  d->editor->SetReadOnlyEnabled(false);
  d->saved_undo = d->editor->GetUndoIndex();
  d->status = "new document - Save to choose a file";
}

// Write the document's editable members (content.md, metadata.yaml,
// schema.yaml) into a directory and keep its manifest in sync. YAML bytes come
// from libmcdf's writers so Studio and the CLI emit identical members.
bool write_members(Document& d, const fs::path& where, std::string& err) {
  auto dir = mcdf::DirectoryContainer::open(where);
  if (!dir) {
    err = dir.error().message;
    return false;
  }
  if (auto w = (*dir)->write("content.md", d.editor->GetText()); !w) {
    err = w.error().message;
    return false;
  }
  if (d.write_meta) {
    if (auto w = (*dir)->write("metadata.yaml", mcdf::metadata_to_yaml(d.meta)); !w) {
      err = w.error().message;
      return false;
    }
  }
  if (d.write_schema) {
    if (auto w = (*dir)->write("schema.yaml", mcdf::schema_to_yaml(d.schema)); !w) {
      err = w.error().message;
      return false;
    }
  }
  if ((*dir)->contains("manifest.json")) {
    if (auto container = mcdf::open_container(where))
      if (auto m = mcdf::build_manifest(**container))
        if (auto json = mcdf::manifest_to_canonical_json(*m))
          (void)(*dir)->write("manifest.json", *json);
  }
  return true;
}

bool flush_working_copy(Document& d) {
  std::string err;
  if (!write_members(d, d.workdir, err)) {
    d.status = "save failed: " + err;
    return false;
  }
  d.props_dirty = false;
  return true;
}

void open_save_as_dialog();  // fwd: an untitled document's first Save opens it

void save(Document& d) {
  if (!d.editor || d.workdir.empty()) return;
  if (d.path.empty()) {  // untitled (new) document: first save chooses a file
    g_active = &d;
    open_save_as_dialog();
    return;
  }
  if (!flush_working_copy(d)) return;

  if (d.is_archive && !d.archive_path.empty()) {
    auto container = mcdf::open_container(d.workdir);
    if (!container) {
      d.status = "save failed: " + container.error().message;
      return;
    }
    auto archive = mcdf::pack_container(**container);
    if (!archive) {
      d.status = "save failed: " + archive.error().message;
      return;
    }
    if (!write_file_bytes(d.archive_path, *archive)) {
      d.status = "save failed: cannot write " + d.archive_path;
      return;
    }
    d.status = "saved " + d.archive_path + " (" + std::to_string(archive->size()) + " bytes)";
  } else {
    d.status = "saved content.md";
  }
  d.saved_undo = d.editor->GetUndoIndex();
}

void save_as(Document& d, std::string path) {
  if (!d.editor || d.workdir.empty()) return;
  if (path.size() < 5 || path.substr(path.size() - 5) != ".mcdf") path += ".mcdf";

  // Stage into a temp copy: in folder mode d.workdir is the user's real
  // folder, which Save As must not modify.
  std::error_code ec;
  const fs::path tmp = make_workdir();
  fs::copy(d.workdir, tmp,
           fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
  std::string err;
  if (!write_members(d, tmp, err)) {
    fs::remove_all(tmp, ec);
    d.status = "save-as failed: " + err;
    return;
  }
  bool ok = false;
  if (auto c = mcdf::open_container(tmp))
    if (auto archive = mcdf::pack_container(**c))
      ok = write_file_bytes(path, *archive);
  fs::remove_all(tmp, ec);

  if (!ok) {
    d.status = "save-as failed: " + path;
    return;
  }
  set_archive(d, path);  // this document now points at the new .mcdf
}

void insert_image(Document& d, const std::string& src) {
  if (!d.editor || d.workdir.empty()) return;
  std::error_code ec;
  const fs::path assets = d.workdir / "assets";
  fs::create_directories(assets, ec);

  const fs::path srcp(src);
  const std::string stem = srcp.stem().string();
  const std::string ext = srcp.extension().string();
  std::string safe;
  for (char ch : stem)
    safe += (std::isalnum(static_cast<unsigned char>(ch)) || ch == '-' || ch == '_')
                ? ch : '_';
  if (safe.empty()) safe = "image";

  fs::path dest = assets / (safe + ext);
  for (int i = 1; fs::exists(dest, ec); ++i)
    dest = assets / (safe + "-" + std::to_string(i) + ext);

  fs::copy_file(srcp, dest, fs::copy_options::overwrite_existing, ec);
  if (ec) { d.status = "insert image failed: " + ec.message(); return; }

  const std::string rel = "assets/" + dest.filename().string();
  const std::string md = "![" + stem + "](" + rel + ")";
  const TextEditor::CursorPosition p = d.editor->GetMainCursorPosition();
  d.editor->ReplaceSectionText(p.line, p.column, p.line, p.column, md);
  d.status = "inserted " + rel;
}

void close_document(Document& d) {
  if (d.workdir_is_temp && !d.workdir.empty()) {
    std::error_code ec;
    fs::remove_all(d.workdir, ec);
  }
}

// ---- dialogs -------------------------------------------------------------------

void open_archive_dialog() {
  imfd::Config cfg;
  cfg.title = "Open MCDF document";
  cfg.mode = imfd::Mode::OpenFile;
  cfg.filters = {{"MCDF document", {".mcdf"}}, {"All files", {"*"}}};
  if (!g_last_dir.empty()) { cfg.start_dir = g_last_dir; g_last_dir.clear(); }
  g_pending = OpenMode::Archive;
  g_dialog.Open(cfg);
}

void open_folder_dialog() {
  imfd::Config cfg;
  cfg.title = "Open MCDF container folder";
  cfg.mode = imfd::Mode::PickFolder;
  if (!g_last_dir.empty()) { cfg.start_dir = g_last_dir; g_last_dir.clear(); }
  g_pending = OpenMode::Folder;
  g_dialog.Open(cfg);
}

void open_save_as_dialog() {
  if (!g_active || !g_active->has_content) return;
  g_target_doc = g_active;
  imfd::Config cfg;
  cfg.title = "Save MCDF document as";
  cfg.mode = imfd::Mode::SaveFile;
  cfg.default_name = g_active->path.empty()
                         ? "untitled.mcdf"
                         : fs::path(g_active->path).filename().string();
  cfg.filters = {{"MCDF document", {".mcdf"}}, {"All files", {"*"}}};
  if (!g_last_dir.empty()) { cfg.start_dir = g_last_dir; g_last_dir.clear(); }
  g_pending = OpenMode::SaveAs;
  g_dialog.Open(cfg);
}

void open_insert_image_dialog() {
  if (!g_active || !g_active->has_content) return;
  g_target_doc = g_active;
  imfd::Config cfg;
  cfg.title = "Insert image";
  cfg.mode = imfd::Mode::OpenFile;
  cfg.filters = {{"Images", {".png", ".jpg", ".jpeg", ".gif", ".bmp"}},
                 {"All files", {"*"}}};
  if (!g_last_dir.empty()) { cfg.start_dir = g_last_dir; g_last_dir.clear(); }
  g_pending = OpenMode::InsertImage;
  g_dialog.Open(cfg);
}

void open_wallpaper_dialog() {
  imfd::Config cfg;
  cfg.title = "Choose background image";
  cfg.mode = imfd::Mode::OpenFile;
  cfg.filters = {{"Images", {".png", ".jpg", ".jpeg", ".bmp"}}, {"All files", {"*"}}};
  if (!g_last_dir.empty()) { cfg.start_dir = g_last_dir; g_last_dir.clear(); }
  g_pending = OpenMode::Wallpaper;
  g_dialog.Open(cfg);
}

// ---- UI ------------------------------------------------------------------------

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
        apply_theme(i);
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

  section("Performance");
  ImGui::Checkbox("Reduce frame rate when idle", &g_prefs.idle_throttle);
  ImGui::SameLine();
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Lower CPU/GPU use when there is no input.\n"
                      "A document app needn't redraw at 60fps while idle.");

  section("Background");
  ImGui::TextDisabled("%s", g_prefs.wallpaper_path.empty()
                                ? "No background image"
                                : g_prefs.wallpaper_path.c_str());
  if (ImGui::Button(ICON_FA_IMAGE "  Choose image...")) open_wallpaper_dialog();
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    g_prefs.wallpaper_path.clear();
    unload_wallpaper();
  }
  ImGui::SetNextItemWidth(200.0f);
  ImGui::SliderFloat("Opacity", &g_prefs.wallpaper_opacity, 0.0f, 1.0f, "%.2f");

  ImGui::Spacing();
  ImGui::Separator();
  if (ImGui::Button("Close", ImVec2(120, 0))) ImGui::CloseCurrentPopup();
  ImGui::EndPopup();
}

void draw_menu_bar() {
  if (!ImGui::BeginMenuBar()) return;
  std::string recent_to_open;
  if (ImGui::BeginMenu("File")) {
    if (ImGui::MenuItem(ICON_FA_FILE_CIRCLE_PLUS "  New", "Ctrl+N"))
      new_blank_document();
    if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open .mcdf document...", "Ctrl+O"))
      open_archive_dialog();
    if (ImGui::MenuItem(ICON_FA_FOLDER "  Open unpacked folder..."))
      open_folder_dialog();
    if (ImGui::BeginMenu(ICON_FA_CLOCK_ROTATE_LEFT "  Open Recent", !g_recent.empty())) {
      std::error_code ec;
      for (const auto& p : g_recent) {
        ImGui::BeginDisabled(!fs::exists(p, ec));
        if (ImGui::MenuItem(p.c_str())) recent_to_open = p;
        ImGui::EndDisabled();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Clear recent")) g_recent.clear();
      ImGui::EndMenu();
    }
    ImGui::Separator();
    const bool can_save = g_active && g_active->has_content;
    if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save", "Ctrl+S", false, can_save))
      save(*g_active);
    if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save As...", "Ctrl+Shift+S", false, can_save))
      open_save_as_dialog();
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_GEAR "  Settings...", "Ctrl+,"))
      g_show_settings = true;
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_RIGHT_FROM_BRACKET "  Quit", "Ctrl+Q"))
      g_quit = true;
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("View")) {
    ImGui::MenuItem(ICON_FA_LIST "  Structure", nullptr, &g_show_structure);
    ImGui::MenuItem(ICON_FA_TAGS "  Metadata", nullptr, &g_show_metadata);
    ImGui::MenuItem(ICON_FA_SITEMAP "  Schema", nullptr, &g_show_schema);
    ImGui::EndMenu();
  }
  if (ImGui::BeginMenu("Insert")) {
    const bool can = g_active && g_active->has_content;
    if (ImGui::MenuItem(ICON_FA_IMAGE "  Image...", nullptr, false, can))
      open_insert_image_dialog();
    ImGui::EndMenu();
  }
  ImGui::EndMenuBar();
  if (!recent_to_open.empty()) open_path(recent_to_open);  // deferred (not mid-iteration)
}

void draw_host() {
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowPos(vp->WorkPos);
  ImGui::SetNextWindowSize(vp->WorkSize);
  ImGui::SetNextWindowViewport(vp->ID);

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
  ImGuiDockNodeFlags dock_flags = ImGuiDockNodeFlags_None;
  if (g_wallpaper_tex) {  // let the wallpaper show through the dock host + empty area
    flags |= ImGuiWindowFlags_NoBackground;
    dock_flags |= ImGuiDockNodeFlags_PassthruCentralNode;
  }

  const float pad_x = ImGui::GetStyle().FramePadding.x;
  const float def_pad_y = ImGui::GetStyle().FramePadding.y;
  const float wp_y = ImGui::GetStyle().WindowPadding.y;
  const ImVec2 tall_pad(pad_x, def_pad_y + 4.0f);
  const float status_h = ImGui::GetFontSize() + def_pad_y * 2.0f + wp_y * 2.0f;

  // Roomier menu bar - its height is fixed at Begin() from FramePadding. Pop the
  // inflated padding before DockSpace so the docked tabs get the normal, compact
  // height, then restore it only for the menu items.
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tall_pad);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::Begin("##StudioHost", nullptr, flags);
  ImGui::PopStyleVar(4);  // incl. FramePadding -> dock tabs use the normal height

  const ImGuiID dock_id = ImGui::GetID("StudioDock");
  g_dockspace_id = dock_id;  // documents dock here by default (see draw_document_window)
  ImGui::DockSpace(dock_id, ImVec2(0.0f, -status_h), dock_flags);

  if (g_documents.empty()) {
    ImGui::SetCursorPos(ImVec2(vp->WorkSize.x * 0.5f - 130.0f, vp->WorkSize.y * 0.4f));
    ImGui::TextDisabled(ICON_FA_FOLDER_OPEN "  Open a .mcdf document (File menu, Ctrl+O)");
  }

  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, tall_pad);  // pad menu items to the bar
  draw_menu_bar();
  ImGui::PopStyleVar();
  ImGui::End();
}

// Right-click menu inside the source editor: standard edit ops + Insert Image.
void editor_context_menu(Document& d) {
  const bool sel = d.editor->AnyCursorHasSelection();
  if (ImGui::MenuItem(ICON_FA_SCISSORS "  Cut", "Ctrl+X", false, sel)) d.editor->Cut();
  if (ImGui::MenuItem(ICON_FA_COPY "  Copy", "Ctrl+C", false, sel)) d.editor->Copy();
  if (ImGui::MenuItem(ICON_FA_PASTE "  Paste", "Ctrl+V")) d.editor->Paste();
  ImGui::Separator();
  if (ImGui::MenuItem(ICON_FA_IMAGE "  Insert Image...")) {
    g_active = &d;  // the dialog inserts into the active document
    open_insert_image_dialog();
  }
}

// ---- E2 panels: Structure / Metadata / Schema ----------------------------------

const ImVec4 kBound{0.30f, 0.80f, 0.40f, 1.0f};
const ImVec4 kMissing{0.90f, 0.35f, 0.35f, 1.0f};

// InputText over a std::string via a stack buffer (fields here are short).
bool input_text(const char* label, std::string& value) {
  char buf[512];
  std::snprintf(buf, sizeof buf, "%s", value.c_str());
  ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x * 0.60f));
  if (ImGui::InputText(label, buf, sizeof buf)) {
    value = buf;
    return true;
  }
  return false;
}

std::string iso8601_now_utc() {
  const std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &t);
#else
  gmtime_r(&t, &tm);
#endif
  char buf[32];
  std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &tm);
  return buf;
}

void jump_to_heading(Document& d, std::size_t idx) {
  if (!d.editor || idx >= d.outline_lines.size()) return;
  const int line = d.outline_lines[idx];
  if (line < 0) return;
  d.editor->SetCursor(line, 0);
  d.editor->ScrollToLine(line, TextEditor::Scroll::alignTop);
  d.editor->SetFocus();
}

// The active document, or null if none has content (panels act on g_active).
Document* panel_target() {
  return (g_active && g_active->has_content) ? g_active : nullptr;
}

// Outline of the live editor buffer + schema sections with their binding
// status. A section is "bound" when some heading carries its explicit {#id} -
// the same rule core/validate.cpp checks with E_REQUIRED_SECTION_MISSING /
// E_SCHEMA_UNBOUND.
void draw_structure_panel() {
  if (!g_show_structure) return;
  ImGui::SetNextWindowSize(ImVec2(320, 520), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(ICON_FA_LIST "  Structure###Structure", &g_show_structure)) {
    Document* dp = panel_target();
    if (!dp) {
      ImGui::TextDisabled("No document with content.md is active.");
    } else {
      Document& d = *dp;
      const auto& outline = outline_of(d);

      ImGui::SeparatorText(ICON_FA_HEADING "  Outline");
      if (outline.empty()) ImGui::TextDisabled("(no headings)");
      for (std::size_t i = 0; i < outline.size(); ++i) {
        const mcdf::Heading& h = outline[i];
        ImGui::PushID(static_cast<int>(i));
        const float indent = static_cast<float>(std::max(0, h.level - 1)) * 14.0f;
        if (indent > 0.0f) ImGui::Indent(indent);
        std::string label = h.text.empty() ? "(untitled)" : h.text;
        if (!h.id.empty()) label += "  {#" + h.id + "}";
        if (ImGui::Selectable(label.c_str())) jump_to_heading(d, i);
        if (ImGui::IsItemHovered() && i < d.outline_lines.size() &&
            d.outline_lines[i] >= 0)
          ImGui::SetTooltip("H%d - line %d", h.level, d.outline_lines[i] + 1);
        if (indent > 0.0f) ImGui::Unindent(indent);
        ImGui::PopID();
      }

      ImGui::Spacing();
      ImGui::SeparatorText(ICON_FA_SITEMAP "  Schema sections");
      if (!d.write_schema) {
        ImGui::TextDisabled("(no schema.yaml - use the Schema panel to add one)");
      } else if (d.schema.sections.empty()) {
        ImGui::TextDisabled("(schema has no sections)");
      } else {
        for (std::size_t i = 0; i < d.schema.sections.size(); ++i) {
          const mcdf::SchemaSection& s = d.schema.sections[i];
          const bool bound = outline_has_id(d, s.id);
          ImGui::PushID(static_cast<int>(1000 + i));
          if (bound) ImGui::TextColored(kBound, ICON_FA_CIRCLE_CHECK);
          else if (s.required) ImGui::TextColored(kMissing, ICON_FA_CIRCLE_XMARK);
          else ImGui::TextDisabled(ICON_FA_CIRCLE_QUESTION);
          ImGui::SameLine();
          const std::string label = s.title.empty() ? s.id : s.title;
          if (ImGui::Selectable(label.c_str()) && bound) {
            for (std::size_t k = 0; k < outline.size(); ++k)
              if (outline[k].id == s.id) { jump_to_heading(d, k); break; }
          }
          if (ImGui::IsItemHovered()) {
            if (bound)
              ImGui::SetTooltip("{#%s}%s - bound to a heading", s.id.c_str(),
                                s.required ? " (required)" : "");
            else
              ImGui::SetTooltip("{#%s} has no matching heading\n%s\n"
                                "Bind it with a heading like:  ## %s {#%s}",
                                s.id.c_str(),
                                s.required ? "E_REQUIRED_SECTION_MISSING"
                                           : "E_SCHEMA_UNBOUND",
                                label.c_str(), s.id.c_str());
          }
          ImGui::PopID();
        }
        ImGui::Spacing();
        ImGui::TextDisabled("Sections bind to headings by explicit {#id}.");
      }
    }
  }
  ImGui::End();
}

// Form over metadata.yaml. Editing any field marks the document dirty and the
// member is (re)written on Save via libmcdf's metadata_to_yaml.
void draw_metadata_panel() {
  if (!g_show_metadata) return;
  ImGui::SetNextWindowSize(ImVec2(400, 480), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(ICON_FA_TAGS "  Metadata###Metadata", &g_show_metadata)) {
    Document* dp = panel_target();
    if (!dp) {
      ImGui::TextDisabled("No document with content.md is active.");
    } else {
      Document& d = *dp;
      bool ch = false;
      if (!d.write_meta)
        ImGui::TextDisabled("(no metadata.yaml yet - editing creates it on Save)");

      ch |= input_text("Title", d.meta.title);
      ch |= input_text("Version", d.meta.version);

      ImGui::SeparatorText(ICON_FA_USER "  Authors");
      int remove = -1;
      for (std::size_t i = 0; i < d.meta.authors.size(); ++i) {
        ImGui::PushID(static_cast<int>(i));
        ImGui::TextDisabled("Author %d", static_cast<int>(i) + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_TRASH)) remove = static_cast<int>(i);
        ch |= input_text("Name", d.meta.authors[i].name);
        ch |= input_text("DID / id", d.meta.authors[i].id);
        ImGui::PopID();
      }
      if (remove >= 0) {
        d.meta.authors.erase(d.meta.authors.begin() + remove);
        ch = true;
      }
      if (ImGui::Button(ICON_FA_PLUS "  Add author")) {
        d.meta.authors.push_back({});
        ch = true;
      }

      ImGui::SeparatorText("Details");
      ch |= input_text("Created at", d.meta.created_at);
      ImGui::SameLine();
      if (ImGui::SmallButton("now")) {
        d.meta.created_at = iso8601_now_utc();
        ch = true;
      }
      ch |= input_text("Classification", d.meta.classification);
      ch |= input_text("Language", d.meta.language);

      bool generated = d.meta.generated_by.has_value();
      if (ImGui::Checkbox("AI-generated (provenance)", &generated)) {
        if (generated) d.meta.generated_by = std::string{};
        else d.meta.generated_by.reset();
        ch = true;
      }
      if (d.meta.generated_by) {
        std::string tool = *d.meta.generated_by;
        if (input_text("Generated by", tool)) {
          d.meta.generated_by = tool;
          ch = true;
        }
      }

      if (ch) {
        d.write_meta = true;
        d.props_dirty = true;
        d.title = d.meta.title;  // keep the status bar in sync
      }
    }
  }
  ImGui::End();
}

// Form over schema.yaml: document_type + the ordered section list.
void draw_schema_panel() {
  if (!g_show_schema) return;
  ImGui::SetNextWindowSize(ImVec2(400, 480), ImGuiCond_FirstUseEver);
  if (ImGui::Begin(ICON_FA_SITEMAP "  Schema###Schema", &g_show_schema)) {
    Document* dp = panel_target();
    if (!dp) {
      ImGui::TextDisabled("No document with content.md is active.");
    } else {
      Document& d = *dp;
      bool ch = false;
      if (!d.write_schema)
        ImGui::TextDisabled("(no schema.yaml yet - editing creates it on Save)");

      ch |= input_text("Document type", d.schema.document_type);

      ImGui::SeparatorText("Sections");
      outline_of(d);  // refresh binding status
      int remove = -1, move_up = -1, move_down = -1;
      for (std::size_t i = 0; i < d.schema.sections.size(); ++i) {
        mcdf::SchemaSection& s = d.schema.sections[i];
        ImGui::PushID(static_cast<int>(i));
        if (outline_has_id(d, s.id)) ImGui::TextColored(kBound, ICON_FA_CIRCLE_CHECK);
        else if (s.required) ImGui::TextColored(kMissing, ICON_FA_CIRCLE_XMARK);
        else ImGui::TextDisabled(ICON_FA_CIRCLE_QUESTION);
        ImGui::SameLine();
        ImGui::TextDisabled("Section %d", static_cast<int>(i) + 1);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_ARROW_UP) && i > 0)
          move_up = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_ARROW_DOWN) &&
            i + 1 < d.schema.sections.size())
          move_down = static_cast<int>(i);
        ImGui::SameLine();
        if (ImGui::SmallButton(ICON_FA_TRASH)) remove = static_cast<int>(i);
        ch |= input_text("Id", s.id);
        ch |= input_text("Title", s.title);
        ch |= ImGui::Checkbox("Required", &s.required);
        ImGui::PopID();
      }
      if (move_up > 0) {
        std::swap(d.schema.sections[move_up - 1], d.schema.sections[move_up]);
        ch = true;
      }
      if (move_down >= 0) {
        std::swap(d.schema.sections[move_down], d.schema.sections[move_down + 1]);
        ch = true;
      }
      if (remove >= 0) {
        d.schema.sections.erase(d.schema.sections.begin() + remove);
        ch = true;
      }
      if (ImGui::Button(ICON_FA_PLUS "  Add section")) {
        d.schema.sections.push_back({});
        ch = true;
      }
      ImGui::Spacing();
      ImGui::TextDisabled("A section binds when content.md has a heading\n"
                          "with its id, e.g.  ## Title {#id}");

      if (ch) {
        d.write_schema = true;
        d.props_dirty = true;
        d.doc_type = d.schema.document_type;  // keep the status bar in sync
      }
    }
  }
  ImGui::End();
}

// One document = a dockable window with an editor | preview split.
void draw_document_window(Document& d) {
  const std::string name = d.path.empty() ? std::string("untitled")
                                          : fs::path(d.path).filename().string();
  const std::string label =
      (is_dirty(d) ? name + " *" : name) + "###doc" + std::to_string(d.id);
  // Dock new documents into the main dockspace by default so first-run users get
  // a tabbed layout (FirstUseEver leaves any later manual (re)docking intact).
  if (g_dockspace_id) ImGui::SetNextWindowDockID(g_dockspace_id, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize(ImVec2(920, 640), ImGuiCond_FirstUseEver);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));  // panes fill to edge
  const bool win_open = ImGui::Begin(label.c_str(), &d.open);
  ImGui::PopStyleVar();
  if (win_open) {
    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows)) g_active = &d;

    if (!d.has_content) {
      ImGui::TextUnformatted("(no content.md)");
    } else {
      const ImVec2 avail = ImGui::GetContentRegionAvail();
      const float splitter = 6.0f;
      float left_w = std::clamp(d.split, 0.15f, 0.85f) * (avail.x - splitter);

      ImGui::BeginChild("##editor", ImVec2(left_w, avail.y), ImGuiChildFlags_None);
      if (g_mono)
#if defined(IMGUI_VERSION_NUM) && IMGUI_VERSION_NUM >= 19200
        ImGui::PushFont(g_mono, 0.0f);
#else
        ImGui::PushFont(g_mono);
#endif
      d.editor->SetTextContextMenuCallback([&d](int, int) { editor_context_menu(d); });
      d.editor->Render("##ed", ImGui::GetContentRegionAvail(), false);
      if (g_mono) ImGui::PopFont();
      ImGui::EndChild();

      ImGui::SameLine(0.0f, 0.0f);
      ImGui::InvisibleButton("##split", ImVec2(splitter, avail.y));
      const bool split_active = ImGui::IsItemHovered() || ImGui::IsItemActive();
      if (split_active) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      if (ImGui::IsItemActive() && avail.x > 1.0f)
        d.split += ImGui::GetIO().MouseDelta.x / avail.x;
      d.split = std::clamp(d.split, 0.15f, 0.85f);
      // A single thin divider line down the splitter (no double outline).
      const ImVec2 smin = ImGui::GetItemRectMin(), smax = ImGui::GetItemRectMax();
      const float scx = (smin.x + smax.x) * 0.5f;
      ImGui::GetWindowDrawList()->AddLine(
          ImVec2(scx, smin.y), ImVec2(scx, smax.y),
          ImGui::GetColorU32(split_active ? ImGuiCol_SeparatorActive : ImGuiCol_Separator),
          1.0f);
      ImGui::SameLine(0.0f, 0.0f);

      ImGui::BeginChild("##preview", ImVec2(0.0f, avail.y), ImGuiChildFlags_None);
      g_render_workdir = d.workdir;  // for image resolution in get_image
      const std::string text = d.editor->GetText();
      static MarkdownView md;
      md.print(text.c_str(), text.c_str() + text.size());
      ImGui::EndChild();
    }
  }
  ImGui::End();
}

// Formatted document summary shown as a tooltip on the footer's file label.
void document_info_tooltip(Document& d) {
  outline_of(d);  // keep heading count + section binding fresh
  ImGui::BeginTooltip();
  // Two columns: dim labels auto-size to the widest, values left-align in a
  // single column beside them.
  if (ImGui::BeginTable("##docinfo", 2, ImGuiTableFlags_SizingFixedFit)) {
    const auto lbl = [](const char* t) {
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextDisabled("%s", t);
      ImGui::TableNextColumn();
    };
    if (!d.title.empty()) { lbl("Title"); ImGui::TextUnformatted(d.title.c_str()); }
    lbl("Type");
    ImGui::TextUnformatted(d.doc_type.empty() ? "(none)" : d.doc_type.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(from schema.yaml)");
    lbl("Kind");
    ImGui::TextUnformatted(d.is_archive ? ".mcdf document" : "unpacked folder");
    lbl("Headings"); ImGui::Text("%d", d.heading_count);
    if (d.write_schema && !d.schema.sections.empty()) {
      int bound = 0;
      for (const auto& s : d.schema.sections)
        if (outline_has_id(d, s.id)) ++bound;
      lbl("Sections");
      ImGui::Text("%d / %d bound", bound, static_cast<int>(d.schema.sections.size()));
    }
    lbl("Members");  ImGui::Text("%d", static_cast<int>(d.files.size()));
    ImGui::EndTable();
  }
  if (!d.files.empty()) {
    ImGui::Separator();
    for (const auto& f : d.files) ImGui::BulletText("%s", f.c_str());
  }
  if (!d.status.empty()) {
    ImGui::Separator();
    ImGui::TextDisabled("%s", d.status.c_str());
  }
  ImGui::EndTooltip();
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
    if (!g_active) {
      ImGui::TextDisabled(ICON_FA_FILE "  %d document(s) open",
                          static_cast<int>(g_documents.size()));
    } else {
      Document& d = *g_active;
      ImGui::BeginGroup();
      ImGui::TextColored(kAccent, ICON_FA_FILE_LINES);
      ImGui::SameLine();
      ImGui::Text("%s", d.path.c_str());
      if (is_dirty(d)) {
        ImGui::SameLine();
        ImGui::TextColored(kDirty, ICON_FA_PEN " unsaved");
      }
      ImGui::EndGroup();
      if (ImGui::IsItemHovered()) document_info_tooltip(d);  // formatted summary
      const std::string right =
          (d.doc_type.empty() ? std::string("document") : d.doc_type) +
          (d.is_archive ? "  |  .mcdf" : "  |  folder");
      const float tw = ImGui::CalcTextSize(right.c_str()).x;
      ImGui::SameLine(ImGui::GetWindowWidth() - tw - 12.0f);
      ImGui::TextDisabled("%s", right.c_str());
    }
  }
  ImGui::End();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();
}

void handle_shortcuts() {
  const ImGuiIO& io = ImGui::GetIO();
  if (!io.KeyCtrl) return;
  if (ImGui::IsKeyPressed(ImGuiKey_S)) {
    if (io.KeyShift) open_save_as_dialog();
    else if (g_active && g_active->has_content) save(*g_active);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_N)) new_blank_document();
  if (ImGui::IsKeyPressed(ImGuiKey_O)) open_archive_dialog();
  if (ImGui::IsKeyPressed(ImGuiKey_Q)) g_quit = true;
  if (ImGui::IsKeyPressed(ImGuiKey_Comma)) g_show_settings = true;
}

// Idle throttle: how long to sleep before the next frame, growing with idle
// time. 0 = render at vsync (recent input).
double idle_wait_timeout() {
  const double idle = glfwGetTime() - g_last_input;
  if (idle < 0.5) return 0.0;          // recently active
  if (idle < 5.0) return 1.0 / 30.0;   // ~30 fps
  if (idle < 30.0) return 1.0 / 10.0;  // ~10 fps
  return 0.25;                          // ~4 fps deep idle
}

void glfw_error_callback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

}  // namespace

int main() {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) return 1;

  load_preferences();

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
  static std::string ini_path = (config_dir() / "mcdf.ini").string();
  io.IniFilename = ini_path.c_str();
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  scan_fonts();
  resolve_saved_fonts();
  rebuild_fonts();
  apply_theme(g_prefs.theme);
  load_wallpaper(g_prefs.wallpaper_path);

  g_last_input = glfwGetTime();
  while (!glfwWindowShouldClose(window) && !g_quit) {
    // Idle throttle: sleep between frames when there is no input (a document app
    // needn't redraw at 60fps while idle), but wake instantly on any event.
    if (g_prefs.idle_throttle) {
      const ImGuiIO& iio = ImGui::GetIO();
      bool active = iio.MouseDelta.x != 0.0f || iio.MouseDelta.y != 0.0f ||
                    iio.MouseWheel != 0.0f || iio.MouseWheelH != 0.0f ||
                    ImGui::IsAnyMouseDown() || iio.InputQueueCharacters.Size > 0;
      for (int k = ImGuiKey_NamedKey_BEGIN; !active && k < ImGuiKey_NamedKey_END; ++k)
        if (ImGui::IsKeyDown(static_cast<ImGuiKey>(k))) active = true;
      if (active) g_last_input = glfwGetTime();
      const double wait = idle_wait_timeout();
      if (wait > 0.0) glfwWaitEventsTimeout(wait);
      else glfwPollEvents();
    } else {
      glfwPollEvents();
    }
    if (g_font_rebuild) rebuild_fonts();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    handle_shortcuts();
    render_wallpaper();
    draw_host();

    const imfd::Result dlg_res = g_dialog.Draw();
    if (dlg_res == imfd::Result::Picked) {
      const std::string picked = g_dialog.SelectedPath();
      if (g_pending == OpenMode::Archive) open_archive(picked);
      else if (g_pending == OpenMode::Folder) open_folder(picked);
      else if (g_pending == OpenMode::SaveAs && g_target_doc) {
        save_as(*g_target_doc, picked);
        add_recent(g_target_doc->path);
      }
      else if (g_pending == OpenMode::InsertImage && g_target_doc) insert_image(*g_target_doc, picked);
      else if (g_pending == OpenMode::Wallpaper) { g_prefs.wallpaper_path = picked; load_wallpaper(picked); }
      g_pending = OpenMode::None;
      g_target_doc = nullptr;
    } else if (dlg_res == imfd::Result::Cancelled) {
      g_pending = OpenMode::None;
      g_target_doc = nullptr;
    }
    if (dlg_res != imfd::Result::None) save_preferences(g_window);

    for (auto& d : g_documents) draw_document_window(*d);
    draw_structure_panel();
    draw_metadata_panel();
    draw_schema_panel();
    draw_status_bar();
    draw_settings();

    // Reap closed documents (remove + clean their temp working copies).
    for (auto it = g_documents.begin(); it != g_documents.end();) {
      if (!(*it)->open) {
        if (g_active == it->get()) g_active = nullptr;
        close_document(**it);
        it = g_documents.erase(it);
      } else {
        ++it;
      }
    }
    if (!g_active && !g_documents.empty()) g_active = g_documents.back().get();

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
  for (auto& d : g_documents) close_document(*d);
  g_documents.clear();
  clear_texture_cache();
  unload_wallpaper();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
