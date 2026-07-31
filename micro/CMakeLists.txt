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

# The files that decide whether a document is valid. They are listed separately
# from the rest because of the guard below: what they must never do is depend on
# a feature gate, and gated sources get appended to MCDF_MICRO_SOURCES.
set(MCDF_MICRO_UNGATED_SOURCES
  src/ustar.c
  src/index.c
  src/manifest.c
  src/yaml_min.c
  src/schema.c
  src/anchors.c
  src/validate.c)

# The gated ones. Each keeps its stubs in the same translation unit, so a
# disabled entry point still links and returns E_DISABLED; what a gate removes
# is the implementation, and for render that includes md4c itself, which is
# added to the source list only when the gate is on.
set(MCDF_MICRO_SOURCES
  ${MCDF_MICRO_UNGATED_SOURCES}
  src/sha256.c
  src/verify.c
  render/render.c)

set(MCDF_MICRO_MD4C ${CMAKE_CURRENT_SOURCE_DIR}/render/md4c/md4c.c)

# ---- structural guard: format decisions must not depend on a gate ------------
#
# A Core-only build and an all-on build must consider exactly the same
# documents valid. The one concrete way to break that is to resolve heading
# anchors through md4c once the render gate brings md4c into the tree - which
# will look like an obvious simplification, because md4c is a real CommonMark
# parser and anchors.c is a line scanner. It would also make
# MCDF_MICRO_ENABLE_RENDER change which documents pass validation, and a comment
# saying "don't" has never stopped anyone.
#
# So the rule is checked rather than written down: no file that participates in
# a validity decision may include md4c. Mentioning it in a comment is fine and
# in fact encouraged - it is the include that couples them.
foreach(mcdf_micro_guarded
        ${MCDF_MICRO_UNGATED_SOURCES}
        src/internal.h
        include/mcdf_micro/mcdf_micro.h)
  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${mcdf_micro_guarded}" mcdf_micro_text)
  if(mcdf_micro_text MATCHES "#[ \t]*include[ \t]*[<\"]md4c")
    message(FATAL_ERROR
      "mcdf_micro: ${mcdf_micro_guarded} includes md4c.\n"
      "These files decide whether a document is valid, and that decision must "
      "be identical in every feature-gate configuration (see the anchor scanner "
      "in src/anchors.c). Routing it through a parser that only exists when "
      "MCDF_MICRO_ENABLE_RENDER is on would make a build's gates change what "
      "'valid' means. Keep md4c inside the render translation unit.")
  endif()
endforeach()

# ---- feature gates -----------------------------------------------------------
# Gates are named after the spec's profile ladder, so a build's configuration
# and the conformance vocabulary are the same vocabulary. Each removes code
# rather than merely skipping calls, the disabled entry points survive as stubs
# returning MCDF_MICRO_E_DISABLED so one piece of caller code compiles against
# every configuration, and no gate ever changes which documents the reader
# considers valid.
#
# The definition is PUBLIC: a consumer's translation unit has to agree with the
# library about what was compiled, or MCDF_MICRO_HAS_INTEGRITY promises a
# capability that is not linked in.
#
# Only the gates whose code exists are declared. SIGNED arrives with its
# implementation; an option that toggled nothing would be a lie the build
# system tells.
#
# RENDER is the expensive one, and not only in flash: md4c needs the whole of
# content.md in one contiguous buffer and allocates internally, so a build with
# it on requires a heap where a Core-only build requires none. That is the
# trade the gate exists to let a part decline.
option(MCDF_MICRO_ENABLE_INTEGRITY
       "SHA-256 hashing and manifest.json verification (Integrity profile)" ON)
option(MCDF_MICRO_ENABLE_RENDER
       "md4c and the block/span event stream (Render profile)" ON)

if(ESP_PLATFORM)
  if(MCDF_MICRO_ENABLE_RENDER)
    list(APPEND MCDF_MICRO_SOURCES ${MCDF_MICRO_MD4C})
  endif()
  idf_component_register(
    SRCS ${MCDF_MICRO_SOURCES}
    INCLUDE_DIRS include
    PRIV_INCLUDE_DIRS src render/md4c)
  if(MCDF_MICRO_ENABLE_INTEGRITY)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC MCDF_MICRO_ENABLE_INTEGRITY)
  endif()
  if(MCDF_MICRO_ENABLE_RENDER)
    target_compile_definitions(${COMPONENT_LIB} PUBLIC MCDF_MICRO_ENABLE_RENDER)
  endif()
  return()
endif()

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED PROJECT_NAME)
  project(mcdf_micro VERSION 0.2.0 LANGUAGES C CXX)
  set(MCDF_MICRO_STANDALONE TRUE)
endif()

# md4c is third-party and unmodified, and render/md4c/README.md says it stays
# that way - so it is compiled without our warning set. Warnings we have
# forbidden ourselves to act on are noise, and noise is what gets a real
# warning ignored. Ours still apply to every file we wrote.
if(MSVC)
  set(MCDF_MICRO_VENDOR_QUIET /W0)
else()
  set(MCDF_MICRO_VENDOR_QUIET -w)
endif()
set_source_files_properties(${MCDF_MICRO_MD4C}
  DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  PROPERTIES COMPILE_OPTIONS "${MCDF_MICRO_VENDOR_QUIET}")

# One library, one configuration. Called once for the target consumers link,
# and once per extra gate configuration the matrix keeps alive.
function(mcdf_micro_add_library target integrity render)
  set(sources ${MCDF_MICRO_SOURCES})
  if(render)
    list(APPEND sources ${MCDF_MICRO_MD4C})
  endif()

  add_library(${target} STATIC ${sources})
  target_include_directories(${target}
    PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
           $<INSTALL_INTERFACE:include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/render/md4c)
  set_target_properties(${target} PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
  if(integrity)
    target_compile_definitions(${target} PUBLIC MCDF_MICRO_ENABLE_INTEGRITY)
  endif()
  if(render)
    target_compile_definitions(${target} PUBLIC MCDF_MICRO_ENABLE_RENDER)
  endif()
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic
                                             -Wconversion -Wshadow)
  endif()
endfunction()

mcdf_micro_add_library(mcdf_micro
  ${MCDF_MICRO_ENABLE_INTEGRITY} ${MCDF_MICRO_ENABLE_RENDER})
add_library(mcdf::micro ALIAS mcdf_micro)

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

# Everything a configuration is worth testing with: the C99 suite, the C++23
# header gate, and the CLI the conformance kit scores. Built per configuration,
# because the classic failure mode of a feature-gated library is the smallest
# build quietly ceasing to compile while nobody looks.
function(mcdf_micro_add_harness lib suffix)
  add_executable(mcdf-micro-tests${suffix} host/tests/test_micro.c)
  target_link_libraries(mcdf-micro-tests${suffix} PRIVATE ${lib})
  set_target_properties(mcdf-micro-tests${suffix} PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  target_compile_definitions(mcdf-micro-tests${suffix} PRIVATE
    MCDF_MICRO_FIXTURE_MINIMAL="${MCDF_MICRO_REPO_ROOT}/fuzz/corpus/unpack/minimal.mcdf"
    MCDF_MICRO_FIXTURE_SHOWCASE="${MCDF_MICRO_REPO_ROOT}/examples/showcase.mcdf"
    MCDF_MICRO_VECTORS="${MCDF_MICRO_REPO_ROOT}/conformance/vectors")
  add_test(NAME mcdf-micro-tests${suffix} COMMAND mcdf-micro-tests${suffix})

  # The C++ gate. Writing the library in C is only worth something if C++
  # callers can include it, and the guards rot the first time someone adds a
  # designated initialiser to an inline helper - so a TU that includes every
  # public header is compiled at the repo's C++23 with warnings as errors.
  add_executable(mcdf-micro-cxx-gate${suffix} host/tests/cxx_header_gate.cpp)
  target_link_libraries(mcdf-micro-cxx-gate${suffix} PRIVATE ${lib})
  set_target_properties(mcdf-micro-cxx-gate${suffix} PROPERTIES
    CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(mcdf-micro-cxx-gate${suffix} PRIVATE /W4 /WX)
  else()
    target_compile_options(mcdf-micro-cxx-gate${suffix} PRIVATE
      -Wall -Wextra -Wpedantic -Werror)
  endif()
  add_test(NAME mcdf-micro-cxx-gate${suffix} COMMAND mcdf-micro-cxx-gate${suffix})

  # The harness run.sh scores. host/ never ships to a device, which is what
  # lets the canonical-manifest emitter live there without the target paying
  # for it - see host/jcs.h.
  add_executable(mcdf-micro-cli${suffix} host/mcdf_micro_cli.c host/jcs.c)
  target_link_libraries(mcdf-micro-cli${suffix} PRIVATE ${lib})
  target_include_directories(mcdf-micro-cli${suffix} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/host)
  set_target_properties(mcdf-micro-cli${suffix} PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(mcdf-micro-cli${suffix} PRIVATE /W4)
  else()
    target_compile_options(mcdf-micro-cli${suffix} PRIVATE
      -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  endif()
endfunction()

if(MCDF_MICRO_BUILD_TESTS AND
   EXISTS "${MCDF_MICRO_REPO_ROOT}/examples/showcase.mcdf")
  if(MCDF_MICRO_STANDALONE)
    enable_testing()
  endif()

  mcdf_micro_add_harness(mcdf_micro "")

  # The gate matrix. Four builds of a tiny C library with no dependencies is
  # close to free, and it is the only way to catch code someone parked inside a
  # gate that turns out to be needed outside it. The configuration the options
  # already produced is not built twice.
  #
  # `core-render` and `all-on` are what make the anchor assertions in
  # host/tests/test_micro.c run against a build that has md4c in it. Without
  # them nothing compiles the render-on configuration, so nothing could catch
  # it disagreeing with a Core-only build about which documents are valid -
  # which is the one thing a feature gate may never do.
  if(MCDF_MICRO_ENABLE_INTEGRITY AND MCDF_MICRO_ENABLE_RENDER)
    set(MCDF_MICRO_PRIMARY_CONFIG all-on)
  elseif(MCDF_MICRO_ENABLE_INTEGRITY)
    set(MCDF_MICRO_PRIMARY_CONFIG core-integrity)
  elseif(MCDF_MICRO_ENABLE_RENDER)
    set(MCDF_MICRO_PRIMARY_CONFIG core-render)
  else()
    set(MCDF_MICRO_PRIMARY_CONFIG core)
  endif()

  foreach(config core core-integrity core-render all-on)
    if("${config}" STREQUAL "${MCDF_MICRO_PRIMARY_CONFIG}")
      continue()
    endif()
    if("${config}" STREQUAL "core-integrity" OR "${config}" STREQUAL "all-on")
      set(config_integrity ON)
    else()
      set(config_integrity OFF)
    endif()
    if("${config}" STREQUAL "core-render" OR "${config}" STREQUAL "all-on")
      set(config_render ON)
    else()
      set(config_render OFF)
    endif()
    mcdf_micro_add_library(mcdf_micro_${config} ${config_integrity} ${config_render})
    mcdf_micro_add_harness(mcdf_micro_${config} "-${config}")
  endforeach()

  # Score the CLI against the published conformance vectors, exactly as any
  # independent implementation is scored. Only the TAR form: this reader has no
  # filesystem and never will, which spec 3 makes a conforming position by
  # naming TAR the one REQUIRED serialization.
  #
  # The runner reports the profiles this build does not evaluate as "not
  # implemented" rather than as passes or failures - see conformance/errors.md.
  if(EXISTS "${MCDF_MICRO_REPO_ROOT}/conformance/run.sh")
    if(WIN32)
      find_program(MCDF_MICRO_PWSH NAMES pwsh powershell)
      if(MCDF_MICRO_PWSH)
        add_test(NAME mcdf-micro-conformance
          COMMAND ${MCDF_MICRO_PWSH} -NoProfile -ExecutionPolicy Bypass
                  -File ${MCDF_MICRO_REPO_ROOT}/conformance/run.ps1
                  $<TARGET_FILE:mcdf-micro-cli> tar)
      endif()
    else()
      find_program(MCDF_MICRO_SH sh)
      if(MCDF_MICRO_SH)
        add_test(NAME mcdf-micro-conformance
          COMMAND ${MCDF_MICRO_SH} ${MCDF_MICRO_REPO_ROOT}/conformance/run.sh
                  $<TARGET_FILE:mcdf-micro-cli> tar)
      endif()
    endif()
  endif()
endif()
