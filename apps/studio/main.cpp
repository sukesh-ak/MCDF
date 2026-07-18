// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// MCDF Studio - E1 shell (document-centric).
//
// A Dear ImGui desktop editor that links libmcdf directly (no CLI/FFI seam) and
// treats a single .mcdf file as "the document" - like .docx. Opening a .mcdf
// unpacks it to a hidden temp working copy; Save re-packs that working copy back
// into the same .mcdf file. It provides:
//   - a syntax-highlighted Markdown source editor (ImGuiColorTextEdit) for
//     content.md;
//   - a live rendered preview (imgui_md over md4c - the same parser libmcdf's
//     renderer uses);
//   - Save that keeps the manifest in sync and re-packs the .mcdf.
//
// "Open unpacked folder..." remains as a secondary path for the directory
// authoring form (git-friendly). Later milestones add the manifest / trust /
// audit / conformance panels and the signature-invalidate demo (plan 04).

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

#include <mcdf/mcdf.hpp>

#include "imfiledialog/imfiledialog.h"
#include "TextEditor.h"
#include "imgui_md.h"

namespace fs = std::filesystem;

namespace {

// imgui_md subclass. The base renders text/lists/links/tables with the default
// font; E-later can override get_font() for larger headings. We skip inline
// images in the E1 preview (get_image -> false).
struct MarkdownView : imgui_md {
  bool get_image(image_info&) const override { return false; }
};

// The open document, flattened into view-friendly fields. The editable
// content.md text lives in the TextEditor; the container files live in g_workdir.
struct OpenDoc {
  std::string path;          // the .mcdf file, or the folder, shown to the user
  std::string status;
  std::vector<std::string> files;
  bool has_content = false;
  bool is_archive = false;   // opened from a .mcdf file (vs an unpacked folder)
  std::string title;         // metadata.title, if any
  std::string doc_type;      // schema.document_type, if any
  int heading_count = 0;
};

std::optional<OpenDoc> g_doc;
imfd::FileDialog g_dialog;
std::unique_ptr<TextEditor> g_editor;   // created after the ImGui context exists
std::size_t g_saved_undo = 0;           // editor undo index at last load/save
bool g_quit = false;

fs::path g_workdir;              // working copy: unpacked temp (archive) or the folder
bool g_workdir_is_temp = false;  // remove on close if it is our temp dir
std::string g_archive_path;      // .mcdf file to save back to (empty in folder mode)

enum class OpenMode { None, Archive, Folder };
OpenMode g_pending = OpenMode::None;

// ---- small filesystem helpers -------------------------------------------------

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
  const fs::path dir =
      fs::temp_directory_path(ec) / "mcdf-studio" / ("work-" + std::to_string(++counter));
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

// ---- open / save --------------------------------------------------------------

// Load the container that currently lives in g_workdir into the UI + editor.
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

// Open a .mcdf document: unpack it to a hidden temp working copy, then load.
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

// Open an already-unpacked directory container (authoring form) in place.
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

  // Keep integrity consistent: if the document carries a manifest, rebuild it so
  // content.md and manifest.json stay in sync. This invalidates any existing
  // signatures by design (tamper evidence); re-signing is a later milestone.
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

// ---- UI -----------------------------------------------------------------------

void draw_menu_bar() {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open .mcdf document...")) {
        imfd::Config cfg;
        cfg.title = "Open MCDF document";
        cfg.mode = imfd::Mode::OpenFile;
        cfg.filters = {{"MCDF document", {".mcdf"}}, {"All files", {"*"}}};
        g_pending = OpenMode::Archive;
        g_dialog.Open(cfg);
      }
      if (ImGui::MenuItem("Open unpacked folder...")) {
        imfd::Config cfg;
        cfg.title = "Open MCDF container folder";
        cfg.mode = imfd::Mode::PickFolder;
        g_pending = OpenMode::Folder;
        g_dialog.Open(cfg);
      }
      ImGui::Separator();
      const bool can_save = g_doc && g_doc->has_content;
      if (ImGui::MenuItem("Save", nullptr, false, can_save)) save_document();
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) g_quit = true;
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void draw_document_panel() {
  if (ImGui::Begin("Document")) {
    if (!g_doc) {
      ImGui::TextUnformatted("No document open.");
      ImGui::TextUnformatted("File > Open .mcdf document...");
    } else {
      ImGui::TextWrapped("File: %s%s", g_doc->path.c_str(),
                         is_dirty() ? "  *(unsaved)" : "");
      ImGui::TextUnformatted(g_doc->is_archive ? "(.mcdf document)"
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
  if (ImGui::Begin("content.md")) {
    if (g_doc && g_doc->has_content && g_editor) {
      g_editor->Render("##editor", ImGui::GetContentRegionAvail(), false);
    } else {
      ImGui::TextUnformatted("(open a document with a content.md)");
    }
  }
  ImGui::End();
}

void draw_preview_panel() {
  if (ImGui::Begin("Preview")) {
    if (g_doc && g_doc->has_content && g_editor) {
      // MVP: re-render from the editor text each frame. E-later debounces this
      // to only re-parse on edits (plan 04, section 5 - live preview budget).
      const std::string text = g_editor->GetText();
      static MarkdownView md;
      md.print(text.c_str(), text.c_str() + text.size());
    } else {
      ImGui::TextUnformatted("(no preview)");
    }
  }
  ImGui::End();
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

    ImGui::DockSpaceOverViewport();
    draw_menu_bar();
    if (g_dialog.Draw() == imfd::Result::Picked) {
      const std::string picked = g_dialog.SelectedPath();
      if (g_pending == OpenMode::Archive) open_archive(picked);
      else if (g_pending == OpenMode::Folder) open_folder(picked);
      g_pending = OpenMode::None;
    }
    draw_document_panel();
    draw_editor_panel();
    draw_preview_panel();

    ImGui::Render();
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
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
