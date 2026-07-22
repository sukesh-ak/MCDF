// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
//
// Fuzz harness entry point. With MCDF_FUZZ_ENGINE the target links libFuzzer
// (Clang -fsanitize=fuzzer) which provides main(); otherwise this header
// supplies a standalone driver that replays every file argument through the
// harness - used to compile-check harnesses on any toolchain and to run the
// committed corpus as a regression test.
#pragma once

#include <cstddef>
#include <cstdint>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

#ifndef MCDF_FUZZ_LIBFUZZER
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
  int ran = 0;
  for (int i = 1; i < argc; ++i) {
    std::ifstream in(argv[i], std::ios::binary);
    if (!in) {
      std::fprintf(stderr, "cannot read %s\n", argv[i]);
      return 1;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)),
                            std::istreambuf_iterator<char>());
    LLVMFuzzerTestOneInput(reinterpret_cast<const uint8_t*>(bytes.data()),
                           bytes.size());
    ++ran;
  }
  std::printf("ok - %d input(s)\n", ran);
  return 0;
}
#endif
