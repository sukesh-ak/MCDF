// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <cxxopts.hpp>
#include <mcdf/mcdf.hpp>

#include <iostream>

int main(int argc, char** argv) {
  try {
    cxxopts::Options options("mcdf",
        "Markdown Container Document Format - document tool");
    options.add_options()
        ("h,help", "Show help")
        ("v,version", "Show version");

    const auto args = options.parse(argc, argv);

    if (args.count("version")) {
      std::cout << "mcdf " << mcdf::version_string() << '\n';
      return 0;
    }

    if (args.count("help") || argc == 1) {
      std::cout << options.help() << '\n'
                << "Planned subcommands:\n"
                   "  inspect  validate  manifest  sign   verify\n"
                   "  pack     unpack    encrypt   decrypt audit  render\n";
      return 0;
    }

    std::cerr << "Unrecognized arguments. Try 'mcdf --help'.\n";
    return 2;
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << '\n';
    return 1;
  }
}
