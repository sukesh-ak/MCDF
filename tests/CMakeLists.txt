# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project

find_package(doctest CONFIG REQUIRED)

add_executable(mcdf-tests test_smoke.cpp)
target_link_libraries(mcdf-tests PRIVATE mcdf::mcdf doctest::doctest)

add_test(NAME mcdf-tests COMMAND mcdf-tests)
