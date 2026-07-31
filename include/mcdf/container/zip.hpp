// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"

namespace mcdf {

// A minimal, read-only ZIP reader for ingest paths (EPUB import). The
// container walking is in-house (like the TAR backend) and written for
// untrusted input - every offset and length is bounds-checked - while
// decompression is delegated to zlib, whose inflate has decades of
// hostile-input exposure; output is capped at the declared size.
//
// Supports the two methods the format actually uses: stored (0) and deflate
// (8). Zip64, encryption and multi-disk archives are rejected explicitly.
// NOTE: this is deliberately NOT a container backend - spec §3 has exactly two
// container forms, TAR and directory, and ZIP is not one of them (its
// duplicate entries and local-header/central-directory disagreement let two
// parsers read different content from one file). This exists only to read
// foreign archives during import.
class ZipArchive {
 public:
  static Result<ZipArchive> from_bytes(std::string bytes);

  std::vector<std::string> names() const;
  bool contains(std::string_view name) const;
  Result<std::string> read(std::string_view name) const;

 private:
  struct Entry {
    std::uint16_t method = 0;
    std::uint32_t compressed_size = 0;
    std::uint32_t uncompressed_size = 0;
    std::uint32_t local_header_offset = 0;
  };

  std::string bytes_;
  std::map<std::string, Entry, std::less<>> entries_;
};

// Raw DEFLATE (RFC 1951) decompression via zlib. `expected_size` is both the
// output buffer size and the cap: a stream that does not finish within it is
// rejected.
Result<std::string> inflate_raw(std::string_view compressed,
                                std::size_t expected_size);

}  // namespace mcdf
