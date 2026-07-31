# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

find_package(doctest CONFIG REQUIRED)

add_executable(mcdf-tests
  test_smoke.cpp
  test_inspect.cpp
  test_canonical_json.cpp
  test_writers.cpp
  test_create.cpp
  test_epub_import.cpp
  test_html_import.cpp
  test_manifest.cpp
  test_validate.cpp
  test_crypto.cpp
  test_sign.cpp
  test_pack.cpp
  test_encrypt.cpp
  test_audit.cpp
  test_render.cpp
)
target_link_libraries(mcdf-tests PRIVATE mcdf::mcdf doctest::doctest)
target_compile_definitions(mcdf-tests PRIVATE
  MCDF_TEST_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures")

add_test(NAME mcdf-tests COMMAND mcdf-tests)

# Score the built CLI against the published conformance vectors, exactly as an
# independent implementation would be scored. run.sh is POSIX sh, so Windows
# runs conformance/run.ps1 - the same checks, kept deliberately in step. Leaving
# Windows unscored is what allowed a CRLF defect to reach `mcdf manifest`
# unnoticed: a byte-exact format has to be checked wherever it is built.
#
# Scored twice, because the reference runtime supports both serializations:
# `tar` is the REQUIRED interchange form (spec 3) and the default, `dir` is the
# OPTIONAL authoring form. Both are scored because both are shipped.
if(WIN32)
  find_program(MCDF_PWSH NAMES pwsh powershell)
  if(MCDF_PWSH)
    foreach(form tar dir)
      add_test(NAME mcdf-conformance-${form}
        COMMAND ${MCDF_PWSH} -NoProfile -ExecutionPolicy Bypass
                -File ${CMAKE_SOURCE_DIR}/conformance/run.ps1
                $<TARGET_FILE:mcdf-cli> ${form})
    endforeach()
  endif()
else()
  find_program(MCDF_SH sh)
  if(MCDF_SH)
    foreach(form tar dir)
      add_test(NAME mcdf-conformance-${form}
        COMMAND ${MCDF_SH} ${CMAKE_SOURCE_DIR}/conformance/run.sh
                $<TARGET_FILE:mcdf-cli> ${form})
    endforeach()
  endif()
endif()

# The two published forms of each vector must stay in step, and packing must
# stay byte-deterministic (spec 3). One check covers both: repack every vector
# and compare against the committed archive. A failure means either a vector
# changed without being repacked, or `tar_write` stopped being deterministic.
add_test(NAME mcdf-conformance-vectors-stable
  COMMAND ${CMAKE_COMMAND} -DMCDF_CLI=$<TARGET_FILE:mcdf-cli> -DMODE=check
          -DSCRATCH=${CMAKE_CURRENT_BINARY_DIR}/vector-check
          -P ${CMAKE_SOURCE_DIR}/cmake/vectors.cmake)
