// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include "mcdf/core/html_import.hpp"

#include <pugixml.hpp>

#include <string>
#include <utility>

#include "core/html_normalize.hpp"
#include "core/xhtml_md.hpp"
#include "mcdf/container/directory_container.hpp"
#include "mcdf/core/create_ops.hpp"
#include "mcdf/core/manifest_ops.hpp"
#include "mcdf/model/metadata.hpp"
#include "mcdf/version.hpp"

namespace mcdf {

Result<HtmlImportReport> import_html(const DirectoryContainer& dir,
                                     std::string_view html,
                                     const AssetResolver& resolve_asset) {
  if (dir.contains("content.md"))
    return fail(ErrorCode::kInvalidContainer,
                "content.md already exists - refusing to overwrite");

  // Tag soup -> well-formed XML, then the shared converter.
  const std::string xml = internal::normalize_html(html);
  pugi::xml_document doc;
  if (!doc.load_string(xml.c_str(), pugi::parse_default))
    return fail(ErrorCode::kParse, "could not parse the HTML document");

  internal::ImageRefs images;
  std::string title;
  bool tables = false, math = false;
  std::string body =
      internal::document_to_markdown(doc, "", images, title, tables, math);
  body = internal::Converter::trim(body);
  if (body.empty() && title.empty())
    return fail(ErrorCode::kParse, "the HTML document has no readable content");

  HtmlImportReport report;
  report.title = title;

  std::string content;
  if (!title.empty()) content += "# " + title + "\n\n";
  content += body;
  content += "\n";

  // Copy referenced images, dropping links we cannot resolve.
  int missing = 0;
  for (const auto& [source, rel] : images) {
    std::optional<std::string> bytes;
    if (resolve_asset) bytes = resolve_asset(source);
    if (!bytes) {
      ++missing;
      continue;
    }
    if (auto w = dir.write(rel, *bytes); !w) {
      ++missing;
      continue;
    }
    ++report.images;
  }

  Metadata meta;
  meta.title = title;
  meta.generated_by = "mcdf html import " + std::string(version_string());
  if (auto r = create_document(dir, content, meta); !r)
    return std::unexpected(r.error());

  // Rebuild so the copied assets are covered.
  auto manifest = build_manifest(dir);
  if (!manifest) return std::unexpected(manifest.error());
  auto json = manifest_to_canonical_json(*manifest);
  if (!json) return std::unexpected(json.error());
  if (auto w = dir.write("manifest.json", *json); !w)
    return std::unexpected(w.error());

  report.notes.push_back("styling (CSS), scripts and page layout are not carried over");
  if (tables) report.notes.push_back("tables with merged cells were flattened to text");
  if (math) report.notes.push_back("MathML content was dropped");
  if (missing > 0)
    report.notes.push_back(std::to_string(missing) +
                           " referenced image(s) could not be read; links kept as-is");
  return report;
}

}  // namespace mcdf
