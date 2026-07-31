// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Fuzzes the in-house ZIP reader and its DEFLATE decoder - the surface every
// untrusted EPUB hits during import.
#include <string>

#include <mcdf/container/zip.hpp>

#include "driver.hpp"

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  const std::string bytes(reinterpret_cast<const char*>(data), size);
  // The raw inflate path (bounded output), then the archive path.
  (void)mcdf::inflate_raw(bytes, 1u << 20);
  auto zip = mcdf::ZipArchive::from_bytes(bytes);
  if (!zip) return 0;
  for (const auto& name : zip->names()) (void)zip->read(name);
  return 0;
}
