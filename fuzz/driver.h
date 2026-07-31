/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* The C twin of driver.hpp, for the mcdf_micro harnesses.
 *
 * They are C because the library is: the whole claim about mcdf_micro is that
 * it compiles and runs as C99 on a part with no C++ runtime, and a C++ harness
 * would quietly stop checking the configuration that actually ships. With
 * MCDF_FUZZ_LIBFUZZER the target links libFuzzer, which provides main();
 * otherwise this supplies a standalone driver that replays every file argument
 * through the harness. */

#ifndef MCDF_FUZZ_DRIVER_H
#define MCDF_FUZZ_DRIVER_H

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

#ifndef MCDF_FUZZ_LIBFUZZER
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
  static unsigned char buffer[4u * 1024u * 1024u];
  int ran = 0;
  int i;

  for (i = 1; i < argc; ++i) {
    size_t n;
    FILE *fp = fopen(argv[i], "rb");
    if (fp == NULL) {
      fprintf(stderr, "cannot read %s\n", argv[i]);
      return 1;
    }
    n = fread(buffer, 1, sizeof buffer, fp);
    fclose(fp);
    LLVMFuzzerTestOneInput(buffer, n);
    ++ran;
  }
  printf("ok - %d input(s)\n", ran);
  return 0;
}
#endif

#endif /* MCDF_FUZZ_DRIVER_H */
