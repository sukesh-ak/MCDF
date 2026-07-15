# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project

find_package(doctest CONFIG REQUIRED)

add_executable(mcdf-tests
  test_smoke.cpp
  test_inspect.cpp
  test_canonical_json.cpp
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
# independent implementation would be scored. The runner is POSIX sh, so it is
# registered only where a real sh is available (not on Windows).
find_program(MCDF_SH sh)
if(MCDF_SH AND NOT WIN32)
  add_test(NAME mcdf-conformance
    COMMAND ${MCDF_SH} ${CMAKE_SOURCE_DIR}/conformance/run.sh $<TARGET_FILE:mcdf-cli>)
endif()
