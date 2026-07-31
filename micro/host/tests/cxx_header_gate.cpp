// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

// The C++ gate.
//
// Writing the library in C is only worth something if C++ can consume it, and
// `extern "C"` guards rot silently: the first designated initialiser in an
// inline helper, the first bare `restrict`, the first implicit void*
// conversion in a macro, and the header stops compiling as C++ - which nobody
// notices until the platform repo's C++ app layer fails to build, long after
// the commit that did it.
//
// So this translation unit includes every public header, compiles at the
// repo's C++23 with warnings as errors, and uses the API the way a C++ caller
// actually would.

#include <cstdio>
#include <cstring>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mcdf_micro/mcdf_micro.h"
#include "mcdf_micro/mcdf_micro_verify.h"

namespace {

// The three-line RAII wrap that "opaque handle + explicit destroy" is for.
struct ReaderDeleter {
  void operator()(mcdf_micro_reader* r) const noexcept { mcdf_micro_close(r); }
};
using ReaderPtr = std::unique_ptr<mcdf_micro_reader, ReaderDeleter>;

// A source over a byte span. No callback may throw across the boundary, so
// this one is noexcept and returns a code like its C counterpart.
struct SpanSource {
  const unsigned char* bytes;
  std::size_t          len;

  static int read(void* ctx, std::uint64_t off, void* dst,
                  std::size_t len) noexcept {
    const auto* self = static_cast<const SpanSource*>(ctx);
    if (off > self->len || len > self->len - off) return -1;
    std::memcpy(dst, self->bytes + off, len);
    return 0;
  }
};

// Enum constants must be usable as the enum type, not as ints, or C++ callers
// end up casting at every call site.
constexpr mcdf_micro_status kOk = MCDF_MICRO_OK;

// The arena macro has to be a constant expression: firmware declares a static
// buffer with it, and so does this.
constexpr std::size_t kArenaBytes = MCDF_MICRO_ARENA_SIZE(16, 512);
alignas(std::max_align_t) unsigned char g_arena[kArenaBytes];

std::optional<std::string> member_text(mcdf_micro_reader* r,
                                       std::string_view path) {
  const std::string key(path);
  mcdf_micro_member_info info{};
  if (mcdf_micro_member(r, key.c_str(), &info) != kOk) return std::nullopt;
  std::string out(static_cast<std::size_t>(info.size), '\0');
  if (mcdf_micro_read_at(r, key.c_str(), 0, out.data(), out.size()) != kOk) {
    return std::nullopt;
  }
  return out;
}

}  // namespace

// A C++ callback crossing the C boundary: C language linkage, noexcept, and it
// never re-enters the reader. That is the contract mcdf_micro_issue_fn states,
// and stating it is worth nothing if nothing compiles against it.
extern "C" void mcdf_micro_gate_on_issue(void* ctx, mcdf_micro_status code,
                                         const char* detail) noexcept {
  auto* seen = static_cast<std::vector<std::string>*>(ctx);
  seen->emplace_back(std::string(mcdf_micro_status_str(code)) + ": " + detail);
}

int main() {
  // An empty but well-formed archive: two zero blocks.
  std::vector<unsigned char> archive(1024, 0);
  SpanSource span{archive.data(), archive.size()};

  mcdf_micro_source src{};
  src.ctx = &span;
  src.read = &SpanSource::read;
  src.size = archive.size();

  mcdf_micro_reader* raw = nullptr;
  const mcdf_micro_status st =
      mcdf_micro_open(&src, g_arena, sizeof g_arena, &raw);
  if (st != kOk) {
    std::printf("open failed: %s\n", mcdf_micro_status_str(st));
    return 1;
  }
  ReaderPtr reader(raw);

  if (mcdf_micro_count(reader.get()) != 0) return 1;
  if (member_text(reader.get(), "content.md").has_value()) return 1;
  if ((mcdf_micro_features() & MCDF_MICRO_FEATURE_CORE) == 0) return 1;
  if (mcdf_micro_path_is_safe("../nope") != 0) return 1;
  if (mcdf_micro_manifest_excluded("signatures/author.sig") == 0) return 1;
  if (mcdf_micro_is_sealed(reader.get(), "content.md") != 0) return 1;

  // An empty container has no content.md, so Core has exactly one thing to say
  // about it - and the callback shape has to survive being handed a C++ lambda
  // context without the caller casting at the call site.
  std::vector<std::string> issues;
  if (mcdf_micro_validate_core(reader.get(), &mcdf_micro_gate_on_issue, &issues,
                               nullptr) != kOk) {
    return 1;
  }
  if (issues.size() != 1 || issues[0].rfind("E_MISSING_CONTENT", 0) != 0) {
    std::printf("core validation reported %zu issue(s)\n", issues.size());
    return 1;
  }

  // The Integrity surface must compile in every gate configuration: with the
  // gate on it runs, with it off it returns E_DISABLED, and either way one
  // piece of caller code builds against both.
  mcdf_micro_sha256 sha{};
  unsigned char digest[MCDF_MICRO_SHA256_SIZE]{};
  mcdf_micro_status hashed = mcdf_micro_sha256_init(&sha);
  if (hashed == kOk) hashed = mcdf_micro_sha256_update(&sha, "abc", 3);
  if (hashed == kOk) hashed = mcdf_micro_sha256_final(&sha, digest);
#if defined(MCDF_MICRO_HAS_INTEGRITY)
  // FIPS 180-4: SHA-256("abc") begins ba 78 16 bf.
  if (hashed != kOk || digest[0] != 0xBA || digest[1] != 0x78) return 1;
  const char* build = "core+integrity";
#else
  if (hashed != MCDF_MICRO_E_DISABLED) return 1;
  const char* build = "core";
#endif

  std::printf("mcdf_micro C++23 header gate: ok (v%d.%d.%d, mcdf %s, %s)\n",
              MCDF_MICRO_VERSION_MAJOR, MCDF_MICRO_VERSION_MINOR,
              MCDF_MICRO_VERSION_PATCH, MCDF_MICRO_MCDF_VERSION, build);
  return 0;
}
