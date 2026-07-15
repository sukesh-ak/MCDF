// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/serialize/parsers.hpp"

#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

#include <exception>
#include <string>

namespace nl = nlohmann;

namespace mcdf {
namespace {

std::string str_or(const YAML::Node& node, const char* key) {
  return node[key] ? node[key].as<std::string>() : std::string{};
}

}  // namespace

Result<Metadata> parse_metadata_yaml(std::string_view yaml) {
  try {
    const YAML::Node n = YAML::Load(std::string(yaml));
    Metadata m;
    m.title = str_or(n, "title");
    m.version = str_or(n, "version");
    if (n["authors"]) {
      for (const auto& a : n["authors"]) {
        Author author;
        author.name = str_or(a, "name");
        author.id = str_or(a, "id");
        m.authors.push_back(std::move(author));
      }
    }
    m.created_at = str_or(n, "created_at");
    m.classification = str_or(n, "classification");
    m.language = str_or(n, "language");
    if (n["generated_by"]) m.generated_by = n["generated_by"].as<std::string>();
    return m;
  } catch (const std::exception& e) {
    return fail(ErrorCode::kParse, std::string("metadata.yaml: ") + e.what());
  }
}

Result<Schema> parse_schema_yaml(std::string_view yaml) {
  try {
    const YAML::Node n = YAML::Load(std::string(yaml));
    Schema s;
    s.document_type = str_or(n, "document_type");
    if (n["sections"]) {
      for (const auto& sec : n["sections"]) {
        SchemaSection section;
        section.id = str_or(sec, "id");
        section.title = str_or(sec, "title");
        section.required = sec["required"] ? sec["required"].as<bool>() : false;
        s.sections.push_back(std::move(section));
      }
    }
    return s;
  } catch (const std::exception& e) {
    return fail(ErrorCode::kParse, std::string("schema.yaml: ") + e.what());
  }
}

Result<Manifest> parse_manifest_json(std::string_view json) {
  try {
    const auto j = nl::json::parse(json);
    Manifest m;
    if (j.contains("mcdf_version"))
      m.mcdf_version = j["mcdf_version"].get<std::string>();
    if (j.contains("hash_algorithm"))
      m.hash_algorithm = j["hash_algorithm"].get<std::string>();
    if (j.contains("files") && j["files"].is_object()) {
      for (const auto& [path, hash] : j["files"].items()) {
        m.files[path] = hash.get<std::string>();
      }
    }
    return m;
  } catch (const std::exception& e) {
    return fail(ErrorCode::kParse, std::string("manifest.json: ") + e.what());
  }
}

}  // namespace mcdf
