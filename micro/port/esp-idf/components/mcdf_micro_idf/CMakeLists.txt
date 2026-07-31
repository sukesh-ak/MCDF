# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# The ESP-IDF adapter. Public REQUIRES rather than PRIV_REQUIRES because the
# header hands the caller an esp_partition_t and an esp_err_t - a consumer
# including it needs those headers on its own include path.

idf_component_register(
  SRCS src/mcdf_micro_idf.c
  INCLUDE_DIRS include
  REQUIRES mcdf_micro esp_partition)

# The same warning set as the core, minus -Wpedantic: this is the one component
# that includes vendor headers, ESP-IDF passes its component includes with -I
# rather than -isystem, and a pedantic diagnostic raised inside esp_partition.h
# is not a defect anyone here can fix. -Wconversion stays, because it is the one
# that catches a 64-bit container offset being narrowed onto a 32-bit off_t.
target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wall -Wextra -Wconversion -Wshadow)

# Off by default, on in CI - see the option in micro/CMakeLists.txt for why a
# shipped component must not carry -Werror by default.
if(MCDF_MICRO_WERROR)
  target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
endif()
