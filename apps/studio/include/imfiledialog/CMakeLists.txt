# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project

# imfd — portable, self-contained Dear ImGui file open/save/folder dialog.
# First-party MCDF source (originally from the YMOVE studio project). Its only
# dependency is Dear ImGui + the C++ standard library <filesystem>.
add_library(imfd STATIC
  imfiledialog.cpp
)
add_library(mcdf::imfd ALIAS imfd)

# Match the project standard (C++23). PUBLIC so consumers inherit it.
target_compile_features(imfd PUBLIC cxx_std_23)

# Parent dir on the include path so consumers use the documented, folder-
# qualified form: #include "imfiledialog/imfiledialog.h".
target_include_directories(imfd PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)

# Dear ImGui comes from vcpkg (the `studio` feature in vcpkg.json).
target_link_libraries(imfd PUBLIC imgui::imgui)
