# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project
#
# Build glue for a THIRD-PARTY vendored library: imgui_md (Mekhontsev). Library
# license: MIT (see LICENSE in this folder) - the source files keep their own
# MIT headers; only this CMake glue is Apache-2.0. Renders a subset of Markdown
# in Dear ImGui using the MD4C parser (md4c::md4c - the same parser libmcdf
# already uses), so no new parser is introduced.

find_package(md4c CONFIG REQUIRED)

add_library(studio_imgui_md STATIC imgui_md.cpp)
add_library(mcdf::imgui_md ALIAS studio_imgui_md)
target_compile_features(studio_imgui_md PUBLIC cxx_std_23)
target_include_directories(studio_imgui_md PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(studio_imgui_md PUBLIC imgui::imgui md4c::md4c)
