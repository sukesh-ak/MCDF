// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// Fuzzes the in-memory TAR (.mcdf) parser - the surface every untrusted
// archive hits first - then exercises reads over whatever parsed.
#include <string>

#include <mcdf/container/tar_container.hpp>

#include "driver.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  auto container = mcdf::TarContainer::from_bytes(
      std::string(reinterpret_cast<const char*>(data), size));
  if (!container) return 0;
  if (auto files = (*container)->list()) {
    for (const auto& f : *files) {
      (void)(*container)->contains(f);
      (void)(*container)->read(f);
    }
  }
  return 0;
}
