// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// MCDF Studio - E0 shell.
//
// A Dear ImGui desktop window that links libmcdf directly (no CLI/FFI seam),
// opens an MCDF container - a directory or a .mcdf archive - via the first-party
// imfd file dialog, and shows its member list, a small document summary, and
// content.md read-only. Later milestones (see internal plan 04) add the editor,
// live Markdown preview, and the manifest / trust / audit panels.

#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>  // pulls in <GL/gl.h> for the frame clear (GL 1.1)

#include <mcdf/mcdf.hpp>

#include "imfiledialog/imfiledialog.h"

namespace {

// The currently open container, flattened into view-friendly fields. E1 will
// grow this into a proper Workspace/Document model (plan 04, section 6).
struct OpenDoc {
  std::string path;
  std::string status;
  std::vector<std::string> files;
  std::string content;         // content.md bytes (UTF-8)
  bool has_content = false;
  std::string title;           // metadata.title, if any
  std::string doc_type;        // schema.document_type, if any
  int heading_count = 0;
};

std::optional<OpenDoc> g_doc;
imfd::FileDialog g_dialog;
bool g_quit = false;

// Open a container by path and populate g_doc. All MCDF work is an in-process
// libmcdf call - the whole point of the C++ client.
void open_path(const std::string& path) {
  OpenDoc d;
  d.path = path;

  auto container = mcdf::open_container(path);
  if (!container) {
    d.status = "open failed: " + container.error().message;
    g_doc = std::move(d);
    return;
  }
  mcdf::Container& c = **container;

  if (auto files = c.list()) d.files = *files;

  if (c.contains("content.md")) {
    if (auto raw = c.read("content.md")) {
      d.content = *raw;
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

  if (d.status.empty()) d.status = "opened " + path;
  g_doc = std::move(d);
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
      ImGui::TextWrapped("Path: %s", g_doc->path.c_str());
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

void draw_content_panel() {
  if (ImGui::Begin("content.md")) {
    if (g_doc && g_doc->has_content) {
      // Read-only placeholder for the E1 source editor (ImGuiColorTextEdit).
      ImGui::InputTextMultiline(
          "##content", g_doc->content.data(), g_doc->content.size() + 1,
          ImGui::GetContentRegionAvail(), ImGuiInputTextFlags_ReadOnly);
    } else {
      ImGui::TextUnformatted("(no content.md)");
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
    draw_content_panel();

    ImGui::Render();
    int fb_w = 0, fb_h = 0;
    glfwGetFramebufferSize(window, &fb_w, &fb_h);
    glViewport(0, 0, fb_w, fb_h);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
