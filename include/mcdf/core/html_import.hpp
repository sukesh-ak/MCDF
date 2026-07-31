// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#pragma once

#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "mcdf/error.hpp"

namespace mcdf {

class DirectoryContainer;

struct HtmlImportReport {
  std::string title;
  int images = 0;
  std::vector<std::string> notes;  // what the conversion could not carry over
};

// Supplies the bytes of an asset referenced by the document, given its path
// relative to the document. Return std::nullopt when it cannot be found - a
// broken image reference is reported, never fatal. Keeping this a callback
// leaves libmcdf free of ambient filesystem access: the CLI reads from the
// HTML file's directory, Studio from its working copy, a web client from
// whatever the user dropped in.
using AssetResolver =
    std::function<std::optional<std::string>(std::string_view rel_path)>;

// Converts an HTML document into MCDF members inside `dir`: content.md (the
// title as H1, then the converted body), referenced images copied into
// assets/ with rewritten links, metadata.yaml carrying the title and
// `generated_by` provenance, and a manifest built last.
//
// Input is real-world HTML (tag soup tolerated), not necessarily XHTML.
// The directory must not already contain content.md.
Result<HtmlImportReport> import_html(const DirectoryContainer& dir,
                                     std::string_view html,
                                     const AssetResolver& resolve_asset = {});

}  // namespace mcdf
