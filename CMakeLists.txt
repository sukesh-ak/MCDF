# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project
cmake_minimum_required(VERSION 3.21)
project(mcdf VERSION 0.3.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(MCDF_BUILD_TESTS "Build unit tests" ON)
option(MCDF_BUILD_STUDIO "Build the MCDF Studio GUI editor (Dear ImGui)" OFF)

# Core library (the engine; no UI).
add_subdirectory(src)

# Clients built on the library. Add server/ here as they arrive.
add_subdirectory(apps/cli)

# MCDF Studio - the Dear ImGui desktop editor. Off by default; needs the vcpkg
# `studio` feature (-DVCPKG_MANIFEST_FEATURES=studio) for imgui + glfw3.
if(MCDF_BUILD_STUDIO)
  add_subdirectory(apps/studio)
endif()

if(MCDF_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
