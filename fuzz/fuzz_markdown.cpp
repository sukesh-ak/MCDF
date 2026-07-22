// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// Fuzzes the CommonMark heading parser (md4c callbacks + {#id} extraction).
#include <string_view>

#include <mcdf/serialize/markdown.hpp>

#include "driver.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  (void)mcdf::parse_headings(
      std::string_view(reinterpret_cast<const char*>(data), size));
  return 0;
}
