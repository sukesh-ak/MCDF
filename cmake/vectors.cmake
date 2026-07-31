# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# Pack every conformance vector into its TAR interchange form, or check that
# the committed forms still match.
#
#   cmake -DMCDF_CLI=<path> -DMODE=pack  -P cmake/vectors.cmake   # regenerate
#   cmake -DMCDF_CLI=<path> -DMODE=check -P cmake/vectors.cmake   # verify
#
# Spec §3 makes TAR the REQUIRED serialization, so every vector is published in
# both forms: the directory for reading and diffing, `container.mcdf` for
# handing over. An implementation with no filesystem can be scored on nothing
# else.
#
# A CMake script rather than a shell/PowerShell pair, so one file covers every
# platform: `run.sh` and `run.ps1` are twins only because they are the scored
# harness and must run wherever an implementer builds.
#
# The check doubles as a regression test for `tar_write`, since packing is
# specified to be byte-deterministic (§3).

cmake_minimum_required(VERSION 3.21)

if(NOT DEFINED MCDF_CLI)
  message(FATAL_ERROR "MCDF_CLI must point at the mcdf CLI")
endif()
if(NOT DEFINED MODE)
  set(MODE check)
endif()
if(NOT MODE STREQUAL "pack" AND NOT MODE STREQUAL "check")
  message(FATAL_ERROR "MODE must be 'pack' or 'check', got '${MODE}'")
endif()

# Scratch space for `check`, removed again on the way out: a script run with
# `cmake -P` inherits the caller's working directory, so defaulting to it would
# drop a stray directory in whatever tree the maintainer happened to be in.
if(NOT DEFINED SCRATCH)
  set(SCRATCH "${CMAKE_CURRENT_BINARY_DIR}/.mcdf-vector-check")
endif()

get_filename_component(kit "${CMAKE_CURRENT_LIST_DIR}/../conformance" ABSOLUTE)
file(GLOB case_files "${kit}/vectors/*/*/case.json")
list(SORT case_files)

if(case_files STREQUAL "")
  message(FATAL_ERROR "no vectors found under ${kit}/vectors")
endif()

set(stale "")
set(count 0)

foreach(case_file IN LISTS case_files)
  get_filename_component(vector_dir "${case_file}" DIRECTORY)
  get_filename_component(name "${vector_dir}" NAME)
  if(NOT IS_DIRECTORY "${vector_dir}/container")
    continue()
  endif()
  math(EXPR count "${count} + 1")

  set(committed "${vector_dir}/container.mcdf")
  if(MODE STREQUAL "pack")
    set(target "${committed}")
  else()
    set(target "${SCRATCH}/${name}.mcdf")
  endif()
  get_filename_component(target_dir "${target}" DIRECTORY)
  file(MAKE_DIRECTORY "${target_dir}")

  execute_process(
    COMMAND "${MCDF_CLI}" pack "${vector_dir}/container" -o "${target}"
    RESULT_VARIABLE rc
    OUTPUT_VARIABLE out
    ERROR_VARIABLE out)
  if(NOT rc EQUAL 0)
    message(FATAL_ERROR "packing ${name} failed (${rc}): ${out}")
  endif()

  if(MODE STREQUAL "check")
    if(NOT EXISTS "${committed}")
      list(APPEND stale "${name} (no committed container.mcdf)")
    else()
      file(SHA256 "${committed}" have)
      file(SHA256 "${target}" want)
      if(NOT have STREQUAL want)
        list(APPEND stale "${name}")
      endif()
    endif()
  endif()
endforeach()

if(MODE STREQUAL "pack")
  message(STATUS "packed ${count} vectors into their TAR interchange form")
  return()
endif()

file(REMOVE_RECURSE "${SCRATCH}")

if(NOT stale STREQUAL "")
  string(REPLACE ";" "\n  " listing "${stale}")
  message(FATAL_ERROR
    "packed vectors are out of date or packing is no longer deterministic:\n"
    "  ${listing}\n"
    "Repack with:\n"
    "  cmake -DMCDF_CLI=<cli> -DMODE=pack -P cmake/vectors.cmake")
endif()
message(STATUS "all ${count} packed vectors match their directory form")
