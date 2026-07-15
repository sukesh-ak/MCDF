// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/container/tar.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace mcdf {
namespace {

constexpr std::size_t kBlock = 512;

// Writes an octal number right-justified in a field of `width` bytes, with the
// last byte a NUL terminator (USTAR numeric convention).
void put_octal(char* field, std::size_t width, std::uint64_t value) {
  field[width - 1] = '\0';
  for (std::size_t i = width - 1; i > 0; --i) {
    field[i - 1] = static_cast<char>('0' + (value & 7));
    value >>= 3;
  }
}

// Splits a path into ustar name (<=100) and prefix (<=155) at a '/' boundary.
bool split_path(const std::string& path, std::string& name, std::string& prefix) {
  if (path.size() <= 100) {
    name = path;
    prefix.clear();
    return true;
  }
  if (path.size() > 255) return false;
  for (std::size_t i = 0; i < path.size(); ++i) {
    if (path[i] != '/') continue;
    const std::string head = path.substr(0, i);
    const std::string tail = path.substr(i + 1);
    if (!tail.empty() && head.size() <= 155 && tail.size() <= 100) {
      prefix = head;
      name = tail;
      return true;
    }
  }
  return false;
}

Result<void> write_header(char* block, const std::string& path,
                          std::uint64_t size) {
  std::memset(block, 0, kBlock);
  std::string name, prefix;
  if (!split_path(path, name, prefix)) {
    return fail(ErrorCode::kUnsupported, "path too long for tar: " + path);
  }
  std::memcpy(block + 0, name.data(), name.size());
  put_octal(block + 100, 8, 0644);   // mode
  put_octal(block + 108, 8, 0);      // uid
  put_octal(block + 116, 8, 0);      // gid
  put_octal(block + 124, 12, size);  // size
  put_octal(block + 136, 12, 0);     // mtime (fixed -> deterministic)
  block[156] = '0';                  // typeflag: regular file
  std::memcpy(block + 257, "ustar", 5);  // magic "ustar\0"
  block[263] = '0';
  block[264] = '0';  // version "00"
  if (!prefix.empty()) std::memcpy(block + 345, prefix.data(), prefix.size());

  // Checksum: sum all bytes with the checksum field treated as spaces.
  std::memset(block + 148, ' ', 8);
  unsigned sum = 0;
  for (std::size_t i = 0; i < kBlock; ++i)
    sum += static_cast<unsigned char>(block[i]);
  std::snprintf(block + 148, 7, "%06o", sum);
  block[148 + 6] = '\0';
  block[148 + 7] = ' ';
  return {};
}

std::string read_field(const char* p, std::size_t n) {
  std::size_t len = 0;
  while (len < n && p[len] != '\0') ++len;
  return std::string(p, len);
}

std::uint64_t read_octal(const char* p, std::size_t n) {
  std::uint64_t value = 0;
  std::size_t i = 0;
  while (i < n && (p[i] == ' ' || p[i] == '\0')) ++i;
  for (; i < n && p[i] >= '0' && p[i] <= '7'; ++i)
    value = (value << 3) + static_cast<std::uint64_t>(p[i] - '0');
  return value;
}

bool is_zero_block(const char* p) {
  for (std::size_t i = 0; i < kBlock; ++i)
    if (p[i] != 0) return false;
  return true;
}

}  // namespace

Result<std::string> tar_write(const std::map<std::string, std::string>& members) {
  std::string out;
  char block[kBlock];
  for (const auto& [path, data] : members) {
    auto header = write_header(block, path, data.size());
    if (!header) return std::unexpected(header.error());
    out.append(block, kBlock);
    out.append(data);
    const std::size_t rem = data.size() % kBlock;
    if (rem != 0) out.append(kBlock - rem, '\0');
  }
  out.append(2 * kBlock, '\0');  // two zero blocks mark end-of-archive
  return out;
}

Result<std::vector<std::pair<std::string, std::string>>> tar_read(
    std::string_view archive) {
  std::vector<std::pair<std::string, std::string>> out;
  std::size_t pos = 0;
  while (pos + kBlock <= archive.size()) {
    const char* block = archive.data() + pos;
    if (is_zero_block(block)) break;

    const unsigned stored = static_cast<unsigned>(read_octal(block + 148, 8));
    char tmp[kBlock];
    std::memcpy(tmp, block, kBlock);
    std::memset(tmp + 148, ' ', 8);
    unsigned sum = 0;
    for (std::size_t i = 0; i < kBlock; ++i)
      sum += static_cast<unsigned char>(tmp[i]);
    if (sum != stored) return fail(ErrorCode::kParse, "bad tar checksum");

    const std::string name = read_field(block + 0, 100);
    const std::string prefix = read_field(block + 345, 155);
    const std::string full = prefix.empty() ? name : prefix + "/" + name;
    const std::uint64_t size = read_octal(block + 124, 12);
    const char type = block[156];
    pos += kBlock;

    if (type == '0' || type == '\0') {
      if (pos + size > archive.size())
        return fail(ErrorCode::kParse, "truncated tar data");
      out.emplace_back(full, std::string(archive.data() + pos,
                                         static_cast<std::size_t>(size)));
    }
    pos += ((size + kBlock - 1) / kBlock) * kBlock;
  }
  return out;
}

}  // namespace mcdf
