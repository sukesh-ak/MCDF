# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project
cmake_minimum_required(VERSION 3.21)
project(mcdf VERSION 0.5.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(MCDF_BUILD_TESTS "Build unit tests" ON)
option(MCDF_BUILD_STUDIO "Build the MCDF Studio GUI editor (Dear ImGui)" OFF)
option(MCDF_BUILD_FUZZERS "Build the fuzz harnesses (fuzz/)" OFF)

if(MCDF_BUILD_TESTS)
  enable_testing()
endif()

# Core library (the engine; no UI).
add_subdirectory(src)

# Clients built on the library. Add server/ here as they arrive.
add_subdirectory(apps/cli)

# MCDF Studio - the Dear ImGui desktop editor. The GUI is off by default and
# needs the vcpkg `studio` feature (-DVCPKG_MANIFEST_FEATURES=studio) for
# imgui + glfw3; Studio's GUI-free document core and its headless tests build
# whenever tests do, so they run in the plain CI matrix too.
if(MCDF_BUILD_STUDIO OR MCDF_BUILD_TESTS)
  add_subdirectory(apps/studio)
endif()

if(MCDF_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(MCDF_BUILD_FUZZERS)
  add_subdirectory(fuzz)
endif()
