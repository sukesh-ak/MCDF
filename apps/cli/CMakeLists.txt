# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

find_package(cxxopts CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)

# The `mcdf` command-line client (first client of libmcdf).
add_executable(mcdf-cli main.cpp)
set_target_properties(mcdf-cli PROPERTIES OUTPUT_NAME mcdf)
target_link_libraries(mcdf-cli PRIVATE mcdf::mcdf cxxopts::cxxopts fmt::fmt)
