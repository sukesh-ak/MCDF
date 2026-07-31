# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

find_package(OpenSSL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(md4c CONFIG REQUIRED)
find_package(pugixml CONFIG REQUIRED)  # EPUB OPF/XHTML parsing (import path)
find_package(ZLIB REQUIRED)            # DEFLATE for the ZIP reader (import path)
find_package(spdlog CONFIG REQUIRED)

# libmcdf: the core engine. Sources are added per module as milestones land
# (foundation/, serialize/, model/, container/, crypto/, core/, render/).
# STATIC: the API carries no dllexport annotations, so shared builds are not
# supported (keeps vcpkg dynamic triplets correct too).
add_library(mcdf STATIC
  version.cpp
  error.cpp
  container/directory_container.cpp
  container/tar.cpp
  container/tar_container.cpp
  container/zip.cpp
  container/open.cpp
  serialize/markdown.cpp
  serialize/parsers.cpp
  serialize/writers.cpp
  serialize/canonical_json.cpp
  serialize/policy_yaml.cpp
  serialize/audit_log.cpp
  serialize/render.cpp
  crypto/hash.cpp
  crypto/encoding.cpp
  crypto/keys.cpp
  crypto/jws.cpp
  crypto/aead.cpp
  crypto/enc_keys.cpp
  core/create_ops.cpp
  core/epub_import.cpp
  core/html_import.cpp
  core/manifest_ops.cpp
  core/sealed.cpp
  core/validate.cpp
  core/sign_ops.cpp
  core/pack_ops.cpp
  core/encrypt_ops.cpp
  core/audit_ops.cpp
  core/render.cpp
  model/document.cpp
)
add_library(mcdf::mcdf ALIAS mcdf)

# PROJECT_SOURCE_DIR (not CMAKE_SOURCE_DIR) so the path stays correct when
# mcdf is consumed via add_subdirectory/FetchContent.
target_include_directories(mcdf PUBLIC
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)
# Internal headers shared between translation units (not installed).
target_include_directories(mcdf PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# The version comes from project() and nowhere else. version.cpp asserts that
# the public header's constants agree, so a bump that misses one is a build
# failure rather than a binary that reports the wrong version.
target_compile_definitions(mcdf PRIVATE
  MCDF_VERSION_STRING="${PROJECT_VERSION}"
  MCDF_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
  MCDF_VERSION_MINOR=${PROJECT_VERSION_MINOR}
  MCDF_VERSION_PATCH=${PROJECT_VERSION_PATCH})

target_compile_features(mcdf PUBLIC cxx_std_23)

target_link_libraries(mcdf
  PUBLIC
    nlohmann_json::nlohmann_json
  PRIVATE
    OpenSSL::Crypto
    yaml-cpp::yaml-cpp
    md4c::md4c
    md4c::md4c-html
    pugixml::pugixml
    ZLIB::ZLIB
    spdlog::spdlog
)
