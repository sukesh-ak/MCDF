# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# mcdf_micro - the portable C99 reader.
#
# One build description serves both worlds, because a second one would drift:
#   - under ESP-IDF, this file registers a component;
#   - everywhere else it builds a plain static library, standalone or as a
#     subdirectory of the main MCDF build.
#
# The host matrix is where the real testing happens. An IDF build can only
# prove that it compiles.

set(MCDF_MICRO_SOURCES
  src/ustar.c
  src/index.c
  src/manifest.c
  src/yaml_min.c)

if(ESP_PLATFORM)
  idf_component_register(
    SRCS ${MCDF_MICRO_SOURCES}
    INCLUDE_DIRS include)
  return()
endif()

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_NAME)
  project(mcdf_micro VERSION 0.1.0 LANGUAGES C CXX)
  set(MCDF_MICRO_STANDALONE TRUE)
endif()

add_library(mcdf_micro STATIC ${MCDF_MICRO_SOURCES})
add_library(mcdf::micro ALIAS mcdf_micro)

target_include_directories(mcdf_micro
  PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
         $<INSTALL_INTERFACE:include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set_target_properties(mcdf_micro PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF)

if(MSVC)
  target_compile_options(mcdf_micro PRIVATE /W4)
else()
  target_compile_options(mcdf_micro PRIVATE -Wall -Wextra -Wpedantic
                                            -Wconversion -Wshadow)
endif()

# Tests follow the parent build's switch when there is one. Both need the
# repo's conformance vectors and .mcdf fixtures, so they are skipped when this
# directory is vendored somewhere else without them.
if(DEFINED MCDF_BUILD_TESTS)
  set(MCDF_MICRO_TESTS_DEFAULT ${MCDF_BUILD_TESTS})
else()
  set(MCDF_MICRO_TESTS_DEFAULT ${MCDF_MICRO_STANDALONE})
endif()
option(MCDF_MICRO_BUILD_TESTS "Build the mcdf_micro host tests"
       ${MCDF_MICRO_TESTS_DEFAULT})

get_filename_component(MCDF_MICRO_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

if(MCDF_MICRO_BUILD_TESTS AND
   EXISTS "${MCDF_MICRO_REPO_ROOT}/examples/showcase.mcdf")
  if(MCDF_MICRO_STANDALONE)
    enable_testing()
  endif()

  # The C99 suite: the library exercised exactly as firmware would use it,
  # over real archives the reference runtime produced.
  add_executable(mcdf-micro-tests host/tests/test_micro.c)
  target_link_libraries(mcdf-micro-tests PRIVATE mcdf_micro)
  set_target_properties(mcdf-micro-tests PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_compile_definitions(mcdf-micro-tests PRIVATE
    MCDF_MICRO_FIXTURE_MINIMAL="${MCDF_MICRO_REPO_ROOT}/fuzz/corpus/unpack/minimal.mcdf"
    MCDF_MICRO_FIXTURE_SHOWCASE="${MCDF_MICRO_REPO_ROOT}/examples/showcase.mcdf")
  add_test(NAME mcdf-micro-tests COMMAND mcdf-micro-tests)

  # The C++ gate. Writing the library in C is only worth something if C++
  # callers can include it, and the guards rot the first time someone adds a
  # designated initialiser to an inline helper - so a TU that includes every
  # public header is compiled at the repo's C++23 with warnings as errors.
  add_executable(mcdf-micro-cxx-gate host/tests/cxx_header_gate.cpp)
  target_link_libraries(mcdf-micro-cxx-gate PRIVATE mcdf_micro)
  set_target_properties(mcdf-micro-cxx-gate PROPERTIES
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(mcdf-micro-cxx-gate PRIVATE /W4 /WX)
  else()
    target_compile_options(mcdf-micro-cxx-gate PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME mcdf-micro-cxx-gate COMMAND mcdf-micro-cxx-gate)
endif()
