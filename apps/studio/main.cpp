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

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
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

ImFont* g_ui = nullptr;
ImFont* g_mono = nullptr;

fs::path g_workdir;
bool g_workdir_is_temp = false;
std::string g_archive_path;

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

// ---- fonts + theme -------------------------------------------------------------

void load_fonts() {
  ImGuiIO& io = ImGui::GetIO();
  ImGuiStyle& style = ImGui::GetStyle();
  constexpr float kRef = 16.0f;
  style.FontSizeBase = kRef;    // imgui 1.92: reference rasterization size
  style.FontScaleMain = 1.0f;   // on-screen scale (the user-facing size lever)

  const std::string fonts = assets_dir() + "/fonts";
  const std::string ui = fonts + "/Roboto-Regular.ttf";
  const std::string mono = fonts + "/RobotoMono-Regular.ttf";
  const std::string fa = fonts + "/fa-solid-900.ttf";
  std::error_code ec;

  static const ImWchar ui_ranges[] = {
      0x0020, 0x00FF,  // Basic Latin + Latin-1
      0x0100, 0x024F,  // Latin Extended-A/B
      0x0370, 0x03FF,  // Greek
      0x2000, 0x206F,  // General Punctuation (em-dash, bullets)
      0x20A0, 0x20CF,  // Currency
      0,
  };
  if (fs::exists(ui, ec))
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
    cfg.GlyphMinAdvanceX = kRef;  // keep icons monospaced
    static const ImWchar fa_ranges[] = {ICON_MIN_FA, ICON_MAX_FA, 0};
    io.Fonts->AddFontFromFileTTF(fa.c_str(), kRef, &cfg, fa_ranges);
  }

  // Monospace face for the code editor.
  if (fs::exists(mono, ec))
    g_mono = io.Fonts->AddFontFromFileTTF(mono.c_str(), kRef);
  if (!g_mono) g_mono = g_ui;
}

void apply_theme() {
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

  ImGuiStyle& s = ImGui::GetStyle();
  s.WindowRounding = 4.0f;
  s.FrameRounding = 2.0f;
  s.TabRounding = 2.0f;
  s.ScrollbarRounding = 3.0f;
  s.GrabRounding = 2.0f;
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
      g_pending = OpenMode::Folder;
      g_dialog.Open(cfg);
    }
    ImGui::Separator();
    const bool can_save = g_doc && g_doc->has_content;
    if (ImGui::MenuItem(ICON_FA_FLOPPY_DISK "  Save", "Ctrl+S", false, can_save))
      save_document();
    ImGui::Separator();
    if (ImGui::MenuItem(ICON_FA_RIGHT_FROM_BRACKET "  Quit", "Ctrl+Q"))
      g_quit = true;
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

void draw_document_panel() {
  if (ImGui::Begin("Document")) {
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
  if (ImGui::Begin(ICON_FA_PEN "  content.md")) {
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
  if (ImGui::Begin(ICON_FA_FILE_LINES "  Preview")) {
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
}

void glfw_error_callback(int error, const char* description) {
  std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

}  // namespace

int main() {
  glfwSetErrorCallback(glfw_error_callback);
  if (!glfwInit()) return 1;

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

  GLFWwindow* window =
      glfwCreateWindow(1280, 800, "MCDF Studio", nullptr, nullptr);
  if (!window) {
    glfwTerminate();
    return 1;
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);  // vsync

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO& io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  ImGui::StyleColorsDark();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init(glsl_version);

  apply_theme();
  load_fonts();

  // Create the editor once the ImGui context exists; bind Markdown syntax.
  g_editor = std::make_unique<TextEditor>();
  g_editor->SetLanguage(TextEditor::Language::Markdown());
  g_editor->SetPalette(TextEditor::GetDarkPalette());
  g_editor->SetReadOnlyEnabled(true);

  while (!glfwWindowShouldClose(window) && !g_quit) {
    glfwPollEvents();

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    handle_shortcuts();
    draw_host();
    if (g_dialog.Draw() == imfd::Result::Picked) {
      const std::string picked = g_dialog.SelectedPath();
      if (g_pending == OpenMode::Archive) open_archive(picked);
      else if (g_pending == OpenMode::Folder) open_folder(picked);
      g_pending = OpenMode::None;
    }
    draw_document_panel();
    draw_editor_panel();
    draw_preview_panel();
    draw_status_bar();

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

  cleanup_workdir();
  g_editor.reset();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
