# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project

# MCDF Studio - the Dear ImGui desktop editor (a client of libmcdf).
#
# Enable the GUI with:
#   cmake --preset <preset> -DMCDF_BUILD_STUDIO=ON -DVCPKG_MANIFEST_FEATURES=studio
# The `studio` vcpkg feature provides imgui (docking + glfw/opengl3 bindings)
# and glfw3; OpenGL comes from the system.
#
# The document core below has no GUI dependency and is also added when only
# MCDF_BUILD_TESTS is on, so the headless tests run in the plain CI matrix.

# Studio's document engine: every op the app performs, with no GUI dependency,
# so it can be driven headlessly by mcdf-studio-tests (plan 04 §10).
add_library(mcdf-studio-core STATIC core/studio_core.cpp)
add_library(mcdf::studio_core ALIAS mcdf-studio-core)
target_compile_features(mcdf-studio-core PUBLIC cxx_std_23)
target_include_directories(mcdf-studio-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(mcdf-studio-core PUBLIC mcdf::mcdf)

# Headless tests over the core: open -> edit -> rebuild -> sign -> verify must
# match the CLI on the same engine; packed bytes must be CLI-identical.
if(MCDF_BUILD_TESTS)
  find_package(doctest CONFIG REQUIRED)
  add_executable(mcdf-studio-tests tests/test_studio_core.cpp)
  target_link_libraries(mcdf-studio-tests PRIVATE mcdf::studio_core doctest::doctest)
  add_test(NAME mcdf-studio-tests COMMAND mcdf-studio-tests)
endif()

if(NOT MCDF_BUILD_STUDIO)
  return()
endif()

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
  mcdf::studio_core
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
