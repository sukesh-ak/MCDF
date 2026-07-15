# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project

find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(md4c CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# libmcdf: the core engine. Sources are added per module as milestones land
# (foundation/, serialize/, model/, container/, crypto/, core/, render/).
add_library(mcdf
  version.cpp
  error.cpp
  container/directory_container.cpp
  container/tar.cpp
  container/tar_container.cpp
  container/open.cpp
  serialize/markdown.cpp
  serialize/parsers.cpp
  serialize/canonical_json.cpp
  crypto/hash.cpp
  crypto/encoding.cpp
  crypto/keys.cpp
  crypto/jws.cpp
  core/manifest_ops.cpp
  core/validate.cpp
  core/sign_ops.cpp
  core/pack_ops.cpp
  model/document.cpp
)
add_library(mcdf::mcdf ALIAS mcdf)

target_include_directories(mcdf PUBLIC
  $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_compile_features(mcdf PUBLIC cxx_std_23)

target_link_libraries(mcdf
  PUBLIC
    nlohmann_json::nlohmann_json
  PRIVATE
    OpenSSL::Crypto
    yaml-cpp::yaml-cpp
    md4c::md4c
    md4c::md4c-html
    spdlog::spdlog
)
