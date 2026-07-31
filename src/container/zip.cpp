// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include "mcdf/container/zip.hpp"

#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

namespace mcdf {
namespace {

// ---- little-endian readers (bounds-checked by the callers) --------------------

std::uint16_t rd16(const char* p) {
  const auto* b = reinterpret_cast<const unsigned char*>(p);
  return static_cast<std::uint16_t>(b[0] | (b[1] << 8));
}

std::uint32_t rd32(const char* p) {
  const auto* b = reinterpret_cast<const unsigned char*>(p);
  return static_cast<std::uint32_t>(b[0]) |
         (static_cast<std::uint32_t>(b[1]) << 8) |
         (static_cast<std::uint32_t>(b[2]) << 16) |
         (static_cast<std::uint32_t>(b[3]) << 24);
}

}  // namespace

Result<std::string> inflate_raw(std::string_view compressed,
                                std::size_t expected_size) {
  // Hard cap so a malformed stream cannot balloon memory; imports are
  // documents, not disk images.
  constexpr std::size_t kMaxOutput = 512u * 1024u * 1024u;
  if (expected_size > kMaxOutput)
    return fail(ErrorCode::kUnsupported, "deflate output too large");
  if (compressed.empty())
    return fail(ErrorCode::kParse, "empty deflate stream");

  z_stream zs{};
  // Negative window bits selects raw DEFLATE (no zlib/gzip wrapper), which is
  // what ZIP entries store.
  if (inflateInit2(&zs, -MAX_WBITS) != Z_OK)
    return fail(ErrorCode::kIo, "cannot initialize inflate");

  std::string out;
  out.resize(expected_size);
  zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.data()));
  zs.avail_in = static_cast<uInt>(compressed.size());
  zs.next_out = reinterpret_cast<Bytef*>(out.data());
  zs.avail_out = static_cast<uInt>(out.size());

  const int rc = ::inflate(&zs, Z_FINISH);
  const std::size_t produced = out.size() - zs.avail_out;
  inflateEnd(&zs);

  // Z_STREAM_END is the only success: the entry must decode fully within the
  // size the archive declared (a stream wanting more room is malformed here).
  if (rc != Z_STREAM_END)
    return fail(ErrorCode::kParse, "corrupt or truncated deflate stream");
  out.resize(produced);
  return out;
}

// ---- ZIP ----------------------------------------------------------------------

Result<ZipArchive> ZipArchive::from_bytes(std::string bytes) {
  constexpr std::uint32_t kEocdSig = 0x06054b50;
  constexpr std::uint32_t kCdSig = 0x02014b50;
  constexpr std::uint32_t kLocalSig = 0x04034b50;

  if (bytes.size() < 22) return fail(ErrorCode::kParse, "not a zip archive");

  // Find the end-of-central-directory record (scan back over any comment).
  std::size_t eocd = std::string::npos;
  const std::size_t max_back = bytes.size() < 66000 ? bytes.size() : 66000;
  for (std::size_t back = 22; back <= max_back; ++back) {
    const std::size_t pos = bytes.size() - back;
    if (rd32(bytes.data() + pos) == kEocdSig) {
      eocd = pos;
      break;
    }
  }
  if (eocd == std::string::npos)
    return fail(ErrorCode::kParse, "zip end-of-central-directory not found");

  const std::uint16_t entry_count = rd16(bytes.data() + eocd + 10);
  const std::uint32_t cd_size = rd32(bytes.data() + eocd + 12);
  const std::uint32_t cd_offset = rd32(bytes.data() + eocd + 16);
  if (entry_count == 0xFFFF || cd_offset == 0xFFFFFFFFu)
    return fail(ErrorCode::kUnsupported, "zip64 archives are not supported");
  if (static_cast<std::size_t>(cd_offset) + cd_size > bytes.size())
    return fail(ErrorCode::kParse, "zip central directory out of range");

  ZipArchive zip;
  std::size_t pos = cd_offset;
  for (std::uint16_t i = 0; i < entry_count; ++i) {
    if (pos + 46 > bytes.size() || rd32(bytes.data() + pos) != kCdSig)
      return fail(ErrorCode::kParse, "corrupt zip central directory");
    const std::uint16_t flags = rd16(bytes.data() + pos + 8);
    const std::uint16_t method = rd16(bytes.data() + pos + 10);
    const std::uint32_t csize = rd32(bytes.data() + pos + 20);
    const std::uint32_t usize = rd32(bytes.data() + pos + 24);
    const std::uint16_t name_len = rd16(bytes.data() + pos + 28);
    const std::uint16_t extra_len = rd16(bytes.data() + pos + 30);
    const std::uint16_t comment_len = rd16(bytes.data() + pos + 32);
    const std::uint32_t local_off = rd32(bytes.data() + pos + 42);
    if (pos + 46 + name_len > bytes.size())
      return fail(ErrorCode::kParse, "corrupt zip entry name");
    if (flags & 0x0001)
      return fail(ErrorCode::kUnsupported, "encrypted zip entries are not supported");

    std::string name(bytes.data() + pos + 46, name_len);
    // Some writers emit Windows separators; the spec says forward slashes.
    std::replace(name.begin(), name.end(), '\\', '/');
    if (!name.empty() && name.back() != '/') {  // skip directory entries
      Entry e;
      e.method = method;
      e.compressed_size = csize;
      e.uncompressed_size = usize;
      e.local_header_offset = local_off;
      zip.entries_.emplace(std::move(name), e);
    }
    pos += 46u + name_len + extra_len + comment_len;
  }

  // Validate that every entry's data range is inside the buffer.
  for (const auto& [name, e] : zip.entries_) {
    const std::size_t lh = e.local_header_offset;
    if (lh + 30 > bytes.size() || rd32(bytes.data() + lh) != kLocalSig)
      return fail(ErrorCode::kParse, "corrupt zip local header: " + name);
    const std::size_t data = lh + 30u + rd16(bytes.data() + lh + 26) +
                             rd16(bytes.data() + lh + 28);
    if (data + e.compressed_size > bytes.size())
      return fail(ErrorCode::kParse, "zip entry data out of range: " + name);
  }

  zip.bytes_ = std::move(bytes);
  return zip;
}

std::vector<std::string> ZipArchive::names() const {
  std::vector<std::string> out;
  out.reserve(entries_.size());
  for (const auto& [name, _] : entries_) out.push_back(name);
  return out;
}

bool ZipArchive::contains(std::string_view name) const {
  return entries_.find(name) != entries_.end();
}

Result<std::string> ZipArchive::read(std::string_view name) const {
  const auto it = entries_.find(name);
  if (it == entries_.end())
    return fail(ErrorCode::kNotFound, "not in archive: " + std::string(name));
  const Entry& e = it->second;
  const std::size_t lh = e.local_header_offset;
  const std::size_t data = lh + 30u + rd16(bytes_.data() + lh + 26) +
                           rd16(bytes_.data() + lh + 28);
  const std::string_view raw(bytes_.data() + data, e.compressed_size);

  if (e.method == 0) {  // stored
    if (e.compressed_size != e.uncompressed_size)
      return fail(ErrorCode::kParse, "stored entry size mismatch");
    return std::string(raw);
  }
  if (e.method == 8) {  // deflate
    auto out = inflate_raw(raw, e.uncompressed_size);
    if (!out) return out;
    if (out->size() != e.uncompressed_size)
      return fail(ErrorCode::kParse, "deflate size mismatch: " + std::string(name));
    return out;
  }
  return fail(ErrorCode::kUnsupported,
              "unsupported zip compression method for " + std::string(name));
}

}  // namespace mcdf
