// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/core/validate.hpp"

#include <set>
#include <string>

#include "mcdf/container/container.hpp"
#include "mcdf/core/manifest_ops.hpp"
#include "mcdf/model/document.hpp"

namespace mcdf {
namespace {

bool profile_at_least(Profile p, Profile floor) {
  return static_cast<int>(p) >= static_cast<int>(floor);
}

void check_core(const Document& d, ValidationReport& r) {
  if (!d.has_content) {
    r.issues.push_back({"E_MISSING_CONTENT", "content.md is missing"});
  }

  std::set<std::string> heading_ids;
  for (const auto& h : d.headings) {
    if (!h.id.empty()) heading_ids.insert(h.id);
  }

  if (d.has_schema) {
    for (const auto& s : d.schema.sections) {
      if (s.id.empty() || heading_ids.contains(s.id)) continue;
      if (s.required) {
        r.issues.push_back(
            {"E_REQUIRED_SECTION_MISSING",
             "required section has no matching heading in content: " + s.id});
      } else {
        r.issues.push_back(
            {"E_SCHEMA_UNBOUND",
             "schema section has no matching heading in content: " + s.id});
      }
    }
  }
}

void check_integrity(const Container& c, const Document& d,
                     ValidationReport& r) {
  if (!d.has_manifest) {
    r.issues.push_back({"E_MISSING_MANIFEST", "manifest.json is missing"});
    return;
  }
  if (d.manifest.hash_algorithm != kDefaultHashAlgorithm) {
    r.issues.push_back({"E_ALGO_NOT_ALLOWED",
                        "hash algorithm not allowed: " +
                            d.manifest.hash_algorithm});
    return;
  }

  auto v = verify_manifest(c, d.manifest);
  if (!v) {
    r.issues.push_back({"E_IO", v.error().message});
    return;
  }
  for (const auto& p : v->missing)
    r.issues.push_back(
        {"E_MANIFEST_MISSING_FILE", "file listed in manifest is missing: " + p});
  for (const auto& p : v->mismatched)
    r.issues.push_back({"E_MANIFEST_HASH_MISMATCH", "content hash mismatch: " + p});
  for (const auto& p : v->extra)
    r.issues.push_back(
        {"E_MANIFEST_EXTRA_FILE", "file present but not listed in manifest: " + p});
}

}  // namespace

Result<Profile> parse_profile(std::string_view name) {
  if (name == "core") return Profile::kCore;
  if (name == "integrity") return Profile::kIntegrity;
  if (name == "signed") return Profile::kSigned;
  if (name == "encrypted") return Profile::kEncrypted;
  if (name == "render") return Profile::kRender;
  return fail(ErrorCode::kUnsupported, "unknown profile: " + std::string(name));
}

std::string_view to_string(Profile profile) {
  switch (profile) {
    case Profile::kCore:      return "core";
    case Profile::kIntegrity: return "integrity";
    case Profile::kSigned:    return "signed";
    case Profile::kEncrypted: return "encrypted";
    case Profile::kRender:    return "render";
  }
  return "unknown";
}

Result<ValidationReport> validate(const Container& container,
                                  const Document& document, Profile profile) {
  ValidationReport r;
  r.profile = profile;

  check_core(document, r);
  if (profile_at_least(profile, Profile::kIntegrity)) {
    check_integrity(container, document, r);
  }
  if (profile_at_least(profile, Profile::kSigned)) {
    r.issues.push_back({"E_PROFILE_UNSUPPORTED",
                        std::string(to_string(profile)) +
                            " validation is not yet implemented (M3+)"});
  }

  r.ok = r.issues.empty();
  return r;
}

}  // namespace mcdf
