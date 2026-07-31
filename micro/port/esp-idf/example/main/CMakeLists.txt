# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

idf_component_register(
  SRCS main.c
  REQUIRES mcdf_micro mcdf_micro_idf esp_partition)

# The example is shipped code, so it is held to the same bar as the library.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra -Wconversion -Wshadow)
if(MCDF_MICRO_WERROR)
  target_compile_options(${COMPONENT_LIB} PRIVATE -Werror)
endif()
