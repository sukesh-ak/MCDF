# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 The MCDF Project
#
# Fuzz harnesses over libmcdf's untrusted-input surfaces (M8 hardening).
#
# Two modes:
#   - MCDF_FUZZ_ENGINE=ON (Clang): links libFuzzer; build with
#       CC=clang CXX=clang++ cmake -DMCDF_BUILD_FUZZERS=ON -DMCDF_FUZZ_ENGINE=ON \
#         -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address" ...
#     then run e.g.  ./fuzz_unpack -max_total_time=60 ../fuzz/corpus/unpack
#   - default (any toolchain): standalone binaries that replay file arguments
#     (the committed corpus) - a compile check + regression runner.

option(MCDF_FUZZ_ENGINE "Link the harnesses against libFuzzer (Clang)" OFF)

foreach(name unpack parsers markdown)
  add_executable(fuzz_${name} fuzz_${name}.cpp)
  target_compile_features(fuzz_${name} PRIVATE cxx_std_23)
  target_link_libraries(fuzz_${name} PRIVATE mcdf::mcdf)
  if(MCDF_FUZZ_ENGINE)
    target_compile_definitions(fuzz_${name} PRIVATE MCDF_FUZZ_LIBFUZZER)
    target_compile_options(fuzz_${name} PRIVATE -fsanitize=fuzzer,address)
    target_link_options(fuzz_${name} PRIVATE -fsanitize=fuzzer,address)
  endif()
endforeach()
