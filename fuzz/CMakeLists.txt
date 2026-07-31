# SPDX-License-Identifier: Apache-2.0
# Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#
# Fuzz harnesses over the untrusted-input surfaces of both implementations.
#
# Two modes:
#   - MCDF_FUZZ_ENGINE=ON (Clang): links libFuzzer; build with
#       CC=clang CXX=clang++ cmake -DMCDF_BUILD_FUZZERS=ON -DMCDF_FUZZ_ENGINE=ON \
#         -DCMAKE_CXX_FLAGS="-fsanitize=fuzzer-no-link,address" ...
#     then run e.g.  ./fuzz_unpack -max_total_time=60 /tmp/scratch ../fuzz/corpus/unpack
#     (the FIRST corpus dir is writable output - pass a scratch dir so the
#     committed seeds stay read-only)
#   - default (any toolchain): standalone binaries that replay file arguments
#     (the committed corpus) - a compile check + regression runner.
#
# Targets:
#   unpack parsers markdown zip   libmcdf (C++)
#   micro_tar micro_member        mcdf_micro (C99)

option(MCDF_FUZZ_ENGINE "Link the harnesses against libFuzzer (Clang)" OFF)

function(mcdf_fuzz_target name)
  if(MCDF_FUZZ_ENGINE)
    target_compile_definitions(${name} PRIVATE MCDF_FUZZ_LIBFUZZER)
    target_compile_options(${name} PRIVATE -fsanitize=fuzzer,address)
    target_link_options(${name} PRIVATE -fsanitize=fuzzer,address)
  endif()
endfunction()

foreach(name unpack parsers markdown zip)
  add_executable(fuzz_${name} fuzz_${name}.cpp)
  target_compile_features(fuzz_${name} PRIVATE cxx_std_23)
  target_link_libraries(fuzz_${name} PRIVATE mcdf::mcdf)
  mcdf_fuzz_target(fuzz_${name})
endforeach()

# mcdf_micro's untrusted-input surfaces. Separate targets rather than more
# cases in the ones above, because they share no code with libmcdf - that is
# the point of an independent implementation - and because a reader on a part
# with no MMU turns an over-read into a silent corruption rather than a
# segfault, so it is the surface that most needs the coverage.
#
# The harnesses are C for the same reason the library is: what ships is the C99
# build, and a C++ harness would stop checking it.
#
# Seeds come from the existing corpora - fuzz/corpus/unpack for the archive
# walker, fuzz/corpus/parsers for the member scanners - rather than from copies,
# so the two implementations are fuzzed from the same inputs.
if(TARGET mcdf_micro)
  foreach(name micro_tar micro_member)
    add_executable(fuzz_${name} fuzz_${name}.c)
    set_target_properties(fuzz_${name} PROPERTIES
      C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_include_directories(fuzz_${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_link_libraries(fuzz_${name} PRIVATE mcdf_micro)
    mcdf_fuzz_target(fuzz_${name})
  endforeach()
endif()
