# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project
#
# Build glue for a THIRD-PARTY vendored library: ImGuiColorTextEdit (the
# goossens rewrite). Library license: MIT (see LICENSE in this folder) - the
# source files keep their own MIT headers; only this CMake glue is Apache-2.0.
# A syntax-highlighting text-editor widget for Dear ImGui; depends only on
# Dear ImGui (imgui.h + imgui_internal.h).

add_library(studio_texteditor STATIC TextEditor.cpp)
add_library(mcdf::texteditor ALIAS studio_texteditor)
target_compile_features(studio_texteditor PUBLIC cxx_std_23)
target_include_directories(studio_texteditor PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_texteditor PUBLIC imgui::imgui)
