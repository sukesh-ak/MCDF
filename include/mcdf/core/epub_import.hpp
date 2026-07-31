// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"

namespace mcdf {

class DirectoryContainer;

// What an EPUB import produced, and what it could not carry over. Imports are
// best-effort conversions, so the caller MUST be able to tell the user what
// was dropped.
struct EpubImportReport {
  std::string title;
  std::vector<std::string> authors;
  int chapters = 0;
  int images = 0;
  std::vector<std::string> notes;  // human-readable "dropped/changed" notes
};

// Converts an EPUB 2/3 archive into MCDF members inside `dir`: chapters are
// concatenated into content.md in spine (reading) order, each with a heading
// carrying a {#id} that a generated schema.yaml binds to; referenced images
// are copied into assets/; OPF Dublin Core metadata becomes metadata.yaml
// (with `generated_by` provenance). The manifest is built last.
//
// The directory must not already contain content.md.
Result<EpubImportReport> import_epub(const DirectoryContainer& dir,
                                     std::string_view epub_bytes);

}  // namespace mcdf
