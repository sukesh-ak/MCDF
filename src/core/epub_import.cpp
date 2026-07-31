// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include "mcdf/core/epub_import.hpp"

#include <pugixml.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/xhtml_md.hpp"
#include "mcdf/container/directory_container.hpp"
#include "mcdf/container/zip.hpp"
#include "mcdf/core/create_ops.hpp"
#include "mcdf/core/manifest_ops.hpp"
#include "mcdf/model/schema.hpp"
#include "mcdf/serialize/writers.hpp"
#include "mcdf/version.hpp"

namespace mcdf {
namespace {

using internal::Converter;
using internal::dir_of;
using internal::ImageRefs;
using internal::is_external;
using internal::resolve;
using internal::slugify;
using internal::strip_fragment;

std::string chapter_markdown(const std::string& xhtml, const std::string& base_dir,
                             ImageRefs& images, std::string& title_out,
                             bool& tables, bool& math) {
  pugi::xml_document doc;
  // EPUB content is XML by spec; tolerate stray DOCTYPE/entities.
  const pugi::xml_parse_result res = doc.load_string(
      xhtml.c_str(), pugi::parse_default | pugi::parse_fragment);
  if (!res) return {};
  return internal::document_to_markdown(doc, base_dir, images, title_out, tables, math);
}

}  // namespace

Result<EpubImportReport> import_epub(const DirectoryContainer& dir,
                                     std::string_view epub_bytes) {
  if (dir.contains("content.md"))
    return fail(ErrorCode::kInvalidContainer,
                "content.md already exists - refusing to overwrite");

  auto zip = ZipArchive::from_bytes(std::string(epub_bytes));
  if (!zip) return std::unexpected(zip.error());

  // 1. container.xml -> OPF path
  auto container_xml = zip->read("META-INF/container.xml");
  if (!container_xml)
    return fail(ErrorCode::kParse, "not an EPUB: META-INF/container.xml missing");
  pugi::xml_document cdoc;
  if (!cdoc.load_string(container_xml->c_str()))
    return fail(ErrorCode::kParse, "malformed META-INF/container.xml");
  const pugi::xml_node rootfile = cdoc.select_node("//rootfile").node();
  const std::string opf_path = rootfile ? rootfile.attribute("full-path").value() : "";
  if (opf_path.empty())
    return fail(ErrorCode::kParse, "EPUB container.xml has no rootfile");

  auto opf_bytes = zip->read(opf_path);
  if (!opf_bytes) return fail(ErrorCode::kParse, "EPUB package file missing: " + opf_path);
  pugi::xml_document opf;
  if (!opf.load_string(opf_bytes->c_str()))
    return fail(ErrorCode::kParse, "malformed EPUB package file");
  const std::string opf_dir = dir_of(opf_path);

  EpubImportReport report;
  Metadata meta;

  // 2. Dublin Core metadata
  for (const pugi::xpath_node& n : opf.select_nodes("//*[local-name()='title']")) {
    meta.title = Converter::trim(Converter::collapse(n.node().text().get()));
    if (!meta.title.empty()) break;
  }
  for (const pugi::xpath_node& n : opf.select_nodes("//*[local-name()='creator']")) {
    const std::string who = Converter::trim(Converter::collapse(n.node().text().get()));
    if (!who.empty()) {
      meta.authors.push_back({who, ""});
      report.authors.push_back(who);
    }
  }
  for (const pugi::xpath_node& n : opf.select_nodes("//*[local-name()='language']")) {
    meta.language = Converter::trim(Converter::collapse(n.node().text().get()));
    if (!meta.language.empty()) break;
  }
  for (const pugi::xpath_node& n : opf.select_nodes("//*[local-name()='date']")) {
    meta.created_at = Converter::trim(Converter::collapse(n.node().text().get()));
    if (!meta.created_at.empty()) break;
  }
  meta.generated_by = "mcdf epub import " + std::string(version_string());

  // 3. manifest (id -> href/media-type) and spine (reading order)
  struct Item {
    std::string href;
    std::string media_type;
  };
  std::map<std::string, Item, std::less<>> items;
  for (const pugi::xpath_node& n : opf.select_nodes("//*[local-name()='item']")) {
    const pugi::xml_node item = n.node();
    items.emplace(item.attribute("id").value(),
                  Item{item.attribute("href").value(),
                       item.attribute("media-type").value()});
  }
  std::vector<std::string> spine;
  for (const pugi::xpath_node& n : opf.select_nodes("//*[local-name()='itemref']")) {
    const std::string idref = n.node().attribute("idref").value();
    const auto it = items.find(idref);
    if (it == items.end()) continue;
    const std::string& mt = it->second.media_type;
    if (!mt.empty() && mt.find("html") == std::string::npos) continue;
    spine.push_back(resolve(opf_dir, strip_fragment(it->second.href)));
  }
  if (spine.empty())
    return fail(ErrorCode::kParse, "EPUB spine has no readable documents");

  // 4. chapters -> markdown, in reading order
  std::vector<std::pair<std::string, std::string>> images;  // archive -> container
  std::string content;
  std::vector<SchemaSection> sections;
  std::set<std::string> used_ids;
  bool dropped_tables = false, dropped_math = false;

  for (std::size_t i = 0; i < spine.size(); ++i) {
    auto xhtml = zip->read(spine[i]);
    if (!xhtml) continue;
    std::string title;
    const std::string base = dir_of(spine[i]);
    const std::string body =
        chapter_markdown(*xhtml, base, images, title, dropped_tables, dropped_math);
    if (Converter::trim(body).empty() && title.empty()) continue;

    if (title.empty()) title = "Chapter " + std::to_string(i + 1);
    std::string id = slugify(title, "chapter-" + std::to_string(i + 1));
    while (used_ids.contains(id)) id += "-" + std::to_string(i + 1);
    used_ids.insert(id);

    if (!content.empty()) content += "\n";
    content += "# " + title + " {#" + id + "}\n\n";
    content += Converter::trim(body);
    content += "\n";
    sections.push_back({id, title, false});
    ++report.chapters;
  }
  if (report.chapters == 0)
    return fail(ErrorCode::kParse, "EPUB produced no readable content");

  // 5. images
  for (const auto& [from, to] : images) {
    auto data = zip->read(from);
    if (!data) continue;  // a broken reference is not fatal
    if (auto w = dir.write(to, *data); !w) continue;
    ++report.images;
  }

  // 6. compose the document
  if (auto r = create_document(dir, content, meta); !r)
    return std::unexpected(r.error());

  Schema schema;
  schema.document_type = "book";
  schema.sections = std::move(sections);
  if (auto w = dir.write("schema.yaml", schema_to_yaml(schema)); !w)
    return std::unexpected(w.error());

  // Rebuild the manifest so schema.yaml and the assets are covered.
  auto manifest = build_manifest(dir);
  if (!manifest) return std::unexpected(manifest.error());
  auto json = manifest_to_canonical_json(*manifest);
  if (!json) return std::unexpected(json.error());
  if (auto w = dir.write("manifest.json", *json); !w)
    return std::unexpected(w.error());

  report.title = meta.title;
  report.notes.push_back("styling (CSS), fonts and page layout are not carried over");
  if (dropped_tables)
    report.notes.push_back("tables with merged cells were flattened to text");
  if (dropped_math) report.notes.push_back("MathML content was dropped");
  report.notes.push_back(
      "internal cross-references between chapters became plain text");
  return report;
}

}  // namespace mcdf
