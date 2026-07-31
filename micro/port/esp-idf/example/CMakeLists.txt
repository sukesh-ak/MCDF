# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

cmake_minimum_required(VERSION 3.16)

# Both components come from this repository rather than the registry, so the
# example builds from a fresh clone with nothing to fetch. A project consuming
# the published component drops this line and adds the dependency to its own
# main/idf_component.yml instead.
get_filename_component(MCDF_MICRO_COMPONENTS
                       "${CMAKE_CURRENT_LIST_DIR}/../components" ABSOLUTE)
set(EXTRA_COMPONENT_DIRS "${MCDF_MICRO_COMPONENTS}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mcdf-micro-reader)
