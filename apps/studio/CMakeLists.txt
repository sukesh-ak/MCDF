# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project

# MCDF Studio - the Dear ImGui desktop editor (a client of libmcdf).
#
# Enable with:
#   cmake --preset <preset> -DMCDF_BUILD_STUDIO=ON -DVCPKG_MANIFEST_FEATURES=studio
# The `studio` vcpkg feature provides imgui (docking + glfw/opengl3 bindings)
# and glfw3; OpenGL comes from the system.

find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

# First-party file dialog target (mcdf::imfd).
add_subdirectory(include/imfiledialog)

# Third-party vendored widgets (MIT): source editor + Markdown preview.
add_subdirectory(third_party/ImGuiColorTextEdit)
add_subdirectory(third_party/imgui_md)

add_executable(mcdf-studio main.cpp)
set_target_properties(mcdf-studio PROPERTIES OUTPUT_NAME mcdf-studio)
target_compile_features(mcdf-studio PRIVATE cxx_std_23)
target_link_libraries(mcdf-studio PRIVATE
  mcdf::mcdf
  mcdf::imfd
  mcdf::texteditor
  mcdf::imgui_md
  imgui::imgui
  glfw
  OpenGL::GL
)
