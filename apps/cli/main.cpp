// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include <mcdf/mcdf.hpp>

#include <nlohmann/json.hpp>

#include <iostream>
#include <string>
#include <vector>

namespace nl = nlohmann;

namespace {

void print_usage() {
  std::cout <<
      "mcdf - Markdown Container Document Format tool\n"
      "\n"
      "Usage:\n"
      "  mcdf <command> [options]\n"
      "\n"
      "Commands:\n"
      "  inspect <container> [--json]   Summarize a container's structure\n"
      "\n"
      "Global:\n"
      "  -h, --help                     Show this help\n"
      "  -v, --version                  Show version\n"
      "\n"
      "Planned: validate manifest sign verify pack unpack encrypt decrypt "
      "audit render\n";
}

void print_human(const std::string& path, const mcdf::DirectoryContainer& c,
                 const mcdf::Document& d) {
  std::cout << "MCDF container: " << path << "\n";
  std::cout << "  format: directory\n";
  if (auto files = c.list()) {
    std::cout << "  files:  " << files->size() << "\n";
    for (const auto& f : *files) std::cout << "    - " << f << "\n";
  }

  std::cout << "\nMetadata:\n";
  if (d.has_metadata) {
    std::cout << "  title:          " << d.metadata.title << "\n";
    std::cout << "  version:        " << d.metadata.version << "\n";
    std::cout << "  classification: " << d.metadata.classification << "\n";
    std::cout << "  language:       " << d.metadata.language << "\n";
    if (!d.metadata.authors.empty()) {
      std::cout << "  authors:        ";
      for (std::size_t i = 0; i < d.metadata.authors.size(); ++i) {
        if (i) std::cout << ", ";
        std::cout << d.metadata.authors[i].name;
        if (!d.metadata.authors[i].id.empty())
          std::cout << " <" << d.metadata.authors[i].id << ">";
      }
      std::cout << "\n";
    }
    if (d.metadata.generated_by)
      std::cout << "  generated_by:   " << *d.metadata.generated_by << "\n";
  } else {
    std::cout << "  (no metadata.yaml)\n";
  }

  std::cout << "\nSchema:\n";
  if (d.has_schema) {
    std::cout << "  document_type: " << d.schema.document_type << "\n";
    std::cout << "  sections (" << d.schema.sections.size() << "):\n";
    for (const auto& s : d.schema.sections) {
      std::cout << "    - " << (s.required ? "[required] " : "           ")
                << s.id;
      if (!s.title.empty()) std::cout << " - " << s.title;
      std::cout << "\n";
    }
  } else {
    std::cout << "  (no schema.yaml)\n";
  }

  std::cout << "\nContent headings (" << d.headings.size() << "):\n";
  for (const auto& h : d.headings) {
    std::cout << "  " << std::string(static_cast<std::size_t>(h.level), '#')
              << " " << h.text;
    if (!h.id.empty()) std::cout << "  {#" << h.id << "}";
    std::cout << "\n";
  }

  std::cout << "\nManifest:\n";
  if (d.has_manifest) {
    std::cout << "  mcdf_version:   " << d.manifest.mcdf_version << "\n";
    std::cout << "  hash_algorithm: " << d.manifest.hash_algorithm << "\n";
    std::cout << "  files:          " << d.manifest.files.size() << "\n";
  } else {
    std::cout << "  (no manifest.json)\n";
  }
}

void print_json(const mcdf::DirectoryContainer& c, const mcdf::Document& d) {
  nl::json j;
  j["format"] = "directory";
  if (auto files = c.list()) j["files"] = *files;

  if (d.has_metadata) {
    j["metadata"] = {
        {"title", d.metadata.title},
        {"version", d.metadata.version},
        {"classification", d.metadata.classification},
        {"language", d.metadata.language},
        {"created_at", d.metadata.created_at},
    };
    for (const auto& a : d.metadata.authors)
      j["metadata"]["authors"].push_back({{"name", a.name}, {"id", a.id}});
    if (d.metadata.generated_by)
      j["metadata"]["generated_by"] = *d.metadata.generated_by;
  }

  if (d.has_schema) {
    j["schema"]["document_type"] = d.schema.document_type;
    for (const auto& s : d.schema.sections)
      j["schema"]["sections"].push_back(
          {{"id", s.id}, {"title", s.title}, {"required", s.required}});
  }

  for (const auto& h : d.headings)
    j["headings"].push_back(
        {{"level", h.level}, {"text", h.text}, {"id", h.id}});

  if (d.has_manifest) {
    j["manifest"] = {
        {"mcdf_version", d.manifest.mcdf_version},
        {"hash_algorithm", d.manifest.hash_algorithm},
        {"file_count", d.manifest.files.size()},
    };
  }

  std::cout << j.dump(2) << "\n";
}

int cmd_inspect(const std::vector<std::string>& args) {
  std::string path;
  bool json = false;
  for (std::size_t i = 1; i < args.size(); ++i) {
    if (args[i] == "--json") {
      json = true;
    } else if (!args[i].empty() && args[i][0] == '-') {
      std::cerr << "unknown option: " << args[i] << "\n";
      return 2;
    } else if (path.empty()) {
      path = args[i];
    }
  }
  if (path.empty()) {
    std::cerr << "usage: mcdf inspect <container> [--json]\n";
    return 2;
  }

  auto container = mcdf::DirectoryContainer::open(path);
  if (!container) {
    std::cerr << "error: " << container.error().message << "\n";
    return 1;
  }
  auto doc = mcdf::load_document(**container);
  if (!doc) {
    std::cerr << "error: " << doc.error().message << "\n";
    return 1;
  }

  if (json)
    print_json(**container, *doc);
  else
    print_human(path, **container, *doc);
  return 0;
}

}  // namespace

int main(int argc, char** argv) {
  const std::vector<std::string> args(argv + 1, argv + argc);

  if (args.empty() || args[0] == "-h" || args[0] == "--help") {
    print_usage();
    return 0;
  }
  if (args[0] == "-v" || args[0] == "--version") {
    std::cout << "mcdf " << mcdf::version_string() << "\n";
    return 0;
  }
  if (args[0] == "inspect") {
    return cmd_inspect(args);
  }

  std::cerr << "unknown command: " << args[0] << "\n\n";
  print_usage();
  return 2;
}
