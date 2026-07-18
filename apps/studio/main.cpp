// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// MCDF Studio - E1 shell.
//
// A Dear ImGui desktop window that links libmcdf directly (no CLI/FFI seam).
// Opens an MCDF container (a directory or a .mcdf archive) via the first-party
// imfd file dialog and provides:
//   - a syntax-highlighted Markdown source editor (ImGuiColorTextEdit) for
//     content.md, editable when the container is a directory;
//   - a live rendered preview (imgui_md over md4c - the same parser libmcdf's
//     renderer uses);
//   - dirty tracking and Save (writes content.md back via DirectoryContainer).
//
// Later milestones add the manifest / trust / audit / conformance panels and
// the "edit invalidates signature" demo (see internal plan 04).
//
// NOTE: this file has not yet been compiled in-tree (the authoring environment
// has no C++ toolchain/vcpkg). Build it with `cmake --preset studio`.

#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>  // pulls in <GL/gl.h> for the frame clear (GL 1.1)

#include <mcdf/mcdf.hpp>

#include "imfiledialog/imfiledialog.h"
#include "TextEditor.h"
#include "imgui_md.h"

namespace {

// Trivial imgui_md subclass - the base class renders text/lists/links/tables
// with the default font; E-later can override get_font() for larger headings.
struct MarkdownView : imgui_md {};

// The currently open container, flattened into view-friendly fields. E-later
// grows this into a proper Workspace/Document model (plan 04, section 6). The
// editable content.md text lives in the TextEditor, not here.
struct OpenDoc {
  std::string path;
  std::string status;
  std::vector<std::string> files;
  bool has_content = false;   // container has a content.md member
  bool editable = false;      // opened as a directory -> writable
  std::string title;          // metadata.title, if any
  std::string doc_type;       // schema.document_type, if any
  int heading_count = 0;
};

std::optional<OpenDoc> g_doc;
imfd::FileDialog g_dialog;
std::unique_ptr<TextEditor> g_editor;  // created after the ImGui context exists
std::size_t g_saved_undo = 0;          // editor undo index at last load/save
bool g_quit = false;

bool is_dirty() {
  return g_editor && g_doc && g_doc->editable &&
         g_editor->GetUndoIndex() != g_saved_undo;
}

// Open a container by path and populate g_doc + the editor. All MCDF work is an
// in-process libmcdf call - the whole point of the C++ client.
void open_path(const std::string& path) {
  OpenDoc d;
  d.path = path;
  d.editable = std::filesystem::is_directory(path);

  auto container = mcdf::open_container(path);
  if (!container) {
    d.status = "open failed: " + container.error().message;
    if (g_editor) {
      g_editor->SetText("");
      g_editor->SetReadOnlyEnabled(true);
    }
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
    g_editor->SetReadOnlyEnabled(!d.editable);
    g_saved_undo = g_editor->GetUndoIndex();
  }

  if (d.status.empty())
    d.status = d.editable ? ("opened " + path) : ("opened (read-only) " + path);
  g_doc = std::move(d);
}

// Write content.md back to a directory container.
void save_content() {
  if (!g_editor || !g_doc || !g_doc->editable) return;
  auto dir = mcdf::DirectoryContainer::open(g_doc->path);
  if (!dir) {
    g_doc->status = "save failed: " + dir.error().message;
    return;
  }
  const std::string text = g_editor->GetText();
  if (auto w = (*dir)->write("content.md", text)) {
    g_saved_undo = g_editor->GetUndoIndex();
    g_doc->status = "saved content.md (" + std::to_string(text.size()) + " bytes)";
  } else {
    g_doc->status = "save failed: " + w.error().message;
  }
}

void draw_menu_bar() {
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open container folder...")) {
        imfd::Config cfg;
        cfg.title = "Open MCDF container folder";
        cfg.mode = imfd::Mode::PickFolder;
        g_dialog.Open(cfg);
      }
      if (ImGui::MenuItem("Open .mcdf archive...")) {
        imfd::Config cfg;
        cfg.title = "Open MCDF archive";
        cfg.mode = imfd::Mode::OpenFile;
        cfg.filters = {{"MCDF container", {".mcdf"}}, {"All files", {"*"}}};
        g_dialog.Open(cfg);
      }
      ImGui::Separator();
      const bool can_save = g_doc && g_doc->editable && g_doc->has_content;
      if (ImGui::MenuItem("Save content.md", nullptr, false, can_save))
        save_content();
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) g_quit = true;
      ImGui::EndMenu();
    }
    ImGui::EndMainMenuBar();
  }
}

void draw_container_panel() {
  if (ImGui::Begin("Container")) {
    if (!g_doc) {
      ImGui::TextUnformatted("No container open.");
      ImGui::TextUnformatted("File > Open container folder... or Open .mcdf");
    } else {
      ImGui::TextWrapped("Path: %s%s", g_doc->path.c_str(),
                         is_dirty() ? "  *(unsaved)" : "");
      ImGui::TextWrapped("%s", g_doc->status.c_str());
      if (!g_doc->editable)
        ImGui::TextUnformatted("read-only (archive; unpack to edit)");
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
      ImGui::TextUnformatted("(open a container with a content.md)");
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

  const char* glsl_version = "#version 130";
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 0);

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
    if (g_dialog.Draw() == imfd::Result::Picked)
      open_path(g_dialog.SelectedPath());
    draw_container_panel();
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

  g_editor.reset();
  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
