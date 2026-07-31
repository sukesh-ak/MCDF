# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# The portable core, as an ESP-IDF component.
#
# There are no sources here on purpose. micro/CMakeLists.txt is the one build
# description for every world this library builds in - it registers the
# component itself when ESP_PLATFORM is set - and a second copy of the source
# list is the thing that drifts.
#
# What this directory contributes is the *name*. ESP-IDF takes a component's
# name from its directory, component names are a single flat namespace shared by
# every component in a firmware image, and `micro` is far too general a word to
# claim in one. So the component is spelled the way the include path, the symbol
# prefix and the registry entry are spelled, and the four agree.

get_filename_component(MCDF_MICRO_HOME "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
include(${MCDF_MICRO_HOME}/CMakeLists.txt)
