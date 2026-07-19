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
find_package(Stb REQUIRED)  # stb_image.h for preview image loading

# First-party file dialog target (mcdf::imfd).
add_subdirectory(include/imfiledialog)

# Third-party vendored widgets (MIT): source editor + Markdown preview.
add_subdirectory(third_party/ImGuiColorTextEdit)
add_subdirectory(third_party/imgui_md)

add_executable(mcdf-studio main.cpp)
set_target_properties(mcdf-studio PROPERTIES OUTPUT_NAME mcdf-studio)
target_compile_features(mcdf-studio PRIVATE cxx_std_23)
target_include_directories(mcdf-studio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${Stb_INCLUDE_DIR})  # IconsFontAwesome6.h + stb
target_link_libraries(mcdf-studio PRIVATE
  mcdf::mcdf
  mcdf::imfd
  mcdf::texteditor
  mcdf::imgui_md
  imgui::imgui
  glfw
  OpenGL::GL
)
if(WIN32)
  target_link_libraries(mcdf-studio PRIVATE dwmapi)  # dark native title bar
endif()

# Fonts/assets live next to the executable (deployed post-build); the compile-
# time fallback points at the source tree so dev runs from the build dir work.
target_compile_definitions(mcdf-studio PRIVATE
  MCDF_STUDIO_ASSETS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets")

add_custom_command(TARGET mcdf-studio POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:mcdf-studio>/assets"
  COMMENT "Deploying MCDF Studio assets (fonts) next to the binary")
