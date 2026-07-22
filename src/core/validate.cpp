// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/core/validate.hpp"

#include <set>
#include <string>

#include "mcdf/container/container.hpp"
#include "mcdf/core/manifest_ops.hpp"
#include "mcdf/core/render.hpp"
#include "mcdf/core/sign_ops.hpp"
#include "mcdf/crypto/enc_keys.hpp"
#include "mcdf/model/document.hpp"
#include "mcdf/serialize/policy_yaml.hpp"

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

// Verifies every signatures/*.sig over the canonical manifest, mapping each
// failure to its normative code. Classification keys off this library's own
// verifier messages (same translation unit family, kept in sync).
void check_signed(const Container& c, ValidationReport& r) {
  auto checks = verify_container(c);
  if (!checks) {
    r.issues.push_back({"E_IO", checks.error().message});
    return;
  }
  if (checks->empty()) {
    r.issues.push_back(
        {"E_SIG_MISSING", "no signatures present (signatures/*.sig)"});
    return;
  }
  for (const auto& s : *checks) {
    if (s.valid) continue;
    const std::string& e = s.error;
    if (e.find("unsupported JWS alg") != std::string::npos) {
      r.issues.push_back({"E_SIG_ALG_NOT_ALLOWED", s.file + ": " + e});
    } else if (e.find("does not match key type") != std::string::npos) {
      r.issues.push_back({"E_SIG_ALG_MISMATCH", s.file + ": " + e});
    } else if (e.find("did:key") != std::string::npos ||
               e.find("did key") != std::string::npos) {
      r.issues.push_back({"E_KID_UNRESOLVABLE", s.file + ": " + e});
    } else {
      r.issues.push_back(
          {"E_SIG_INVALID",
           s.file + ": " +
               (e.empty() ? "signature does not verify over the canonical manifest"
                          : e)});
    }
  }
}

// Static soundness of the encryption layer (no key available here, so the
// decrypt-time codes E_NOT_A_RECIPIENT / E_DECRYPT_FAILED stay out of scope).
// An unencrypted container has nothing to assert and passes.
void check_encrypted(const Container& c, ValidationReport& r) {
  if (!c.contains("encryption/policy.yaml")) return;
  auto raw = c.read("encryption/policy.yaml");
  if (!raw) {
    r.issues.push_back({"E_IO", raw.error().message});
    return;
  }
  auto p = parse_encryption_policy_yaml(*raw);
  if (!p) {
    r.issues.push_back({"E_POLICY_INVALID", p.error().message});
    return;
  }
  if (p->method != "aes-256-gcm")
    r.issues.push_back(
        {"E_ALGO_NOT_ALLOWED", "encryption method not allowed: " + p->method});
  if (p->key_management != "hpke")
    r.issues.push_back({"E_ALGO_NOT_ALLOWED",
                        "key management not allowed: " + p->key_management});
  if (p->encrypted_files.empty())
    r.issues.push_back({"E_POLICY_INVALID", "policy lists no encrypted files"});
  for (const auto& f : p->encrypted_files) {
    if (!c.contains(f))
      r.issues.push_back(
          {"E_POLICY_INVALID",
           "encrypted file listed in policy is missing: " + f});
  }
  if (p->recipients.empty())
    r.issues.push_back({"E_POLICY_INVALID", "policy has no recipients"});
  for (const auto& rec : p->recipients) {
    if (rec.id.empty() || rec.enc.empty() || rec.wrapped_key.empty()) {
      r.issues.push_back({"E_POLICY_INVALID", "recipient entry is incomplete"});
      continue;
    }
    if (!EncPublicKey::from_did_key(rec.id))
      r.issues.push_back(
          {"E_KID_UNRESOLVABLE",
           "recipient id is not a resolvable X25519 did:key: " + rec.id});
  }
}

// The canonical renders must be producible.
void check_render(const Container& c, ValidationReport& r) {
  for (RenderFormat f : {RenderFormat::kHtml, RenderFormat::kText}) {
    if (auto out = render(c, f); !out)
      r.issues.push_back({"E_IO", "render " + std::string(to_string(f)) +
                                      " failed: " + out.error().message});
  }
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
    check_signed(container, r);
  }
  if (profile_at_least(profile, Profile::kEncrypted)) {
    check_encrypted(container, r);
  }
  if (profile_at_least(profile, Profile::kRender)) {
    check_render(container, r);
  }

  r.ok = r.issues.empty();
  return r;
}

}  // namespace mcdf
