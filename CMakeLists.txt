# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
cmake_minimum_required(VERSION 3.21)
project(mcdf VERSION 0.8.5 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# When consumed via add_subdirectory/FetchContent, build only the library;
# tests, clients and install rules are for top-level builds (and the vcpkg
# port, which sets MCDF_INSTALL explicitly).
option(MCDF_BUILD_TESTS "Build unit tests" ${PROJECT_IS_TOP_LEVEL})
option(MCDF_BUILD_CLI "Build the mcdf command-line client" ${PROJECT_IS_TOP_LEVEL})
option(MCDF_BUILD_STUDIO "Build the MCDF Studio GUI editor (Dear ImGui)" OFF)
option(MCDF_BUILD_FUZZERS "Build the fuzz harnesses (fuzz/)" OFF)
option(MCDF_BUILD_MICRO "Build mcdf_micro, the portable C99 reader (micro/)" ${PROJECT_IS_TOP_LEVEL})
option(MCDF_INSTALL "Generate install/export rules for libmcdf" ${PROJECT_IS_TOP_LEVEL})

if(MCDF_BUILD_TESTS)
  enable_testing()
endif()

# Core library (the engine; no UI).
add_subdirectory(src)

# mcdf_micro - the portable C99 reader. Shares nothing with libmcdf
# by design: it is an independent implementation, and reusing the engine's
# logic would make scoring it prove nothing. It has no dependencies, so it
# rides the existing matrix at close to zero cost.
if(MCDF_BUILD_MICRO)
  add_subdirectory(micro)
endif()

# Clients built on the library. Add server/ here as they arrive.
if(MCDF_BUILD_CLI)
  add_subdirectory(apps/cli)
endif()

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

# ---- install/export: make `find_package(mcdf CONFIG)` work ---------------------
# Consumers get the mcdf::mcdf target with its dependencies resolved via
# find_dependency (see cmake/mcdfConfig.cmake.in). Used by the vcpkg port
# (ports/mcdf) and any plain `cmake --install`.
if(MCDF_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(TARGETS mcdf
    EXPORT mcdfTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  install(DIRECTORY include/mcdf
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
  install(EXPORT mcdfTargets
    NAMESPACE mcdf::
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mcdf)

  configure_package_config_file(
    cmake/mcdfConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/mcdfConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_DATADIR}/mcdf)
  # Pre-1.0, minor versions may change the API: SameMinorVersion.
  write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/mcdfConfigVersion.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMinorVersion)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/mcdfConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/mcdfConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_DATADIR}/mcdf)
endif()
