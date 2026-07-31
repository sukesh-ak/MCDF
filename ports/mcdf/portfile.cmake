# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# Overlay port for libmcdf. Consume with:
#   vcpkg install mcdf --overlay-ports=<MCDF checkout>/ports
# or add the `ports/` directory to "overlay-ports" in vcpkg-configuration.json.
# REF/SHA512 are updated each release.

vcpkg_check_linkage(ONLY_STATIC_LIBRARY)  # no export macros; static-only

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO sukesh-ak/MCDF
    REF v0.8.5
    SHA512 c79156141317468a4f50bc8bc4443b567cfabb2312f5b171e5ddb0418538e338869434c73e12012f83659e5b30989bc980bfaff1e63a76776c44a686123c0583
    HEAD_REF main
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DMCDF_BUILD_TESTS=OFF
        -DMCDF_BUILD_CLI=OFF
        -DMCDF_BUILD_STUDIO=OFF
        -DMCDF_BUILD_FUZZERS=OFF
        -DMCDF_INSTALL=ON
)

vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH share/mcdf PACKAGE_NAME mcdf)

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include"
                    "${CURRENT_PACKAGES_DIR}/debug/share")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage"
     DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")
