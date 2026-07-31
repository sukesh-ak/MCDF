// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include "mcdf/core/create_ops.hpp"

#include <utility>

#include "mcdf/container/directory_container.hpp"
#include "mcdf/core/manifest_ops.hpp"
#include "mcdf/serialize/markdown.hpp"
#include "mcdf/serialize/writers.hpp"

namespace mcdf {

Result<void> create_document(const DirectoryContainer& dir,
                             std::string_view markdown, Metadata metadata) {
  if (dir.contains("content.md")) {
    return fail(ErrorCode::kInvalidContainer,
                "content.md already exists - refusing to overwrite");
  }
  const std::string content = canonicalize_content(markdown);
  if (auto w = dir.write("content.md", content); !w) return w;

  if (metadata.title.empty()) {
    if (auto headings = parse_headings(content))
      if (!headings->empty()) metadata.title = (*headings)[0].text;
  }
  if (auto w = dir.write("metadata.yaml", metadata_to_yaml(metadata)); !w)
    return w;

  auto manifest = build_manifest(dir);
  if (!manifest) return std::unexpected(manifest.error());
  auto json = manifest_to_canonical_json(*manifest);
  if (!json) return std::unexpected(json.error());
  return dir.write("manifest.json", *json);
}

}  // namespace mcdf
