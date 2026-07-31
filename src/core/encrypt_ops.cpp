// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
#include "mcdf/core/encrypt_ops.hpp"

#include <algorithm>
#include <set>

#include "mcdf/container/directory_container.hpp"
#include "mcdf/core/manifest_ops.hpp"
#include "mcdf/crypto/aead.hpp"
#include "mcdf/crypto/encoding.hpp"
#include "mcdf/serialize/markdown.hpp"
#include "mcdf/serialize/parsers.hpp"
#include "mcdf/serialize/policy_yaml.hpp"

namespace mcdf {
namespace {

constexpr std::string_view kHpkeInfo = "mcdf/hpke/cek/v1";
constexpr const char* kPolicyPath = "encryption/policy.yaml";
constexpr const char* kContentPath = "content.md";
constexpr const char* kSchemaPath = "schema.yaml";

bool is_sealed(const std::vector<std::string>& files, std::string_view path) {
  return std::find(files.begin(), files.end(), path) != files.end();
}

// Which schema sections bind to a heading in the given content. This is the one
// computation the attestation records and the one decrypt re-runs, so both
// paths must call it rather than reimplementing 4.2.
Result<std::vector<std::string>> bound_sections(std::string_view schema_yaml,
                                                std::string_view content) {
  auto schema = parse_schema_yaml(schema_yaml);
  if (!schema) return std::unexpected(schema.error());
  auto headings = parse_headings(content);
  if (!headings) return std::unexpected(headings.error());

  // Top-level headings only, exactly as check_core binds them (spec 4.2).
  // These two must not drift: an encryptor that attested a nested anchor would
  // record a binding no validator would accept, and the document would fail the
  // moment it was sealed.
  std::set<std::string> anchors;
  for (const auto& h : *headings) {
    if (h.top_level && !h.id.empty()) anchors.insert(h.id);
  }

  std::vector<std::string> bound;
  for (const auto& s : schema->sections) {
    if (!s.id.empty() && anchors.contains(s.id)) bound.push_back(s.id);
  }
  return bound;
}

// True when spec 5.2.1 requires an attestation: content.md is being sealed and
// the container has a readable schema that declares sections.
Result<bool> attestation_required(const Container& container,
                                  const std::vector<std::string>& files) {
  if (!is_sealed(files, kContentPath)) return false;
  if (is_sealed(files, kSchemaPath)) return false;  // would leak the ids
  if (!container.contains(kSchemaPath)) return false;

  auto raw = container.read(kSchemaPath);
  if (!raw) return std::unexpected(raw.error());
  auto schema = parse_schema_yaml(*raw);
  if (!schema) return std::unexpected(schema.error());
  return !schema->sections.empty();
}

// After changing file bytes, rewrite manifest.json so it hashes the new
// (cipher/plain) content.
Result<void> rebuild_manifest(const DirectoryContainer& dir) {
  auto manifest = build_manifest(dir);
  if (!manifest) return std::unexpected(manifest.error());
  auto json = manifest_to_canonical_json(*manifest);
  if (!json) return std::unexpected(json.error());
  return dir.write("manifest.json", *json);
}

}  // namespace

Result<void> encrypt_container(const DirectoryContainer& dir,
                               const std::vector<std::string>& files,
                               const std::vector<EncPublicKey>& recipients) {
  if (files.empty()) return fail(ErrorCode::kUnsupported, "no files to encrypt");
  if (recipients.empty()) return fail(ErrorCode::kUnsupported, "no recipients");

  if (dir.contains(kPolicyPath)) {
    // Sealing twice would encrypt ciphertext under a second key and leave the
    // first policy unopenable - a silent way to destroy a document.
    return fail(ErrorCode::kUnsupported,
                "container is already encrypted; decrypt it before encrypting again");
  }

  // The attestation has to be computed BEFORE anything is sealed: once
  // content.md is ciphertext its headings are gone (spec 5.2.1).
  EncryptionPolicy policy;
  auto required = attestation_required(dir, files);
  if (!required) return std::unexpected(required.error());
  if (*required) {
    auto schema_yaml = dir.read(kSchemaPath);
    if (!schema_yaml) return std::unexpected(schema_yaml.error());
    auto content = dir.read(kContentPath);
    if (!content) return std::unexpected(content.error());
    auto bound = bound_sections(*schema_yaml, *content);
    if (!bound) return std::unexpected(bound.error());
    policy.structure.present = true;
    policy.structure.bound_sections = *bound;
  }

  const std::string cek = random_bytes(32);

  for (const auto& f : files) {
    if (!dir.contains(f)) return fail(ErrorCode::kNotFound, "no such file: " + f);
    auto plaintext = dir.read(f);
    if (!plaintext) return std::unexpected(plaintext.error());
    auto sealed = aes256gcm_seal(cek, f, *plaintext);  // aad binds path
    if (!sealed) return std::unexpected(sealed.error());
    auto written = dir.write(f, *sealed);
    if (!written) return std::unexpected(written.error());
  }

  policy.encrypted_files = files;
  for (const auto& recipient : recipients) {
    auto did = recipient.did_key();
    if (!did) return std::unexpected(did.error());
    auto sealed = hpke_seal(recipient, kHpkeInfo, cek);
    if (!sealed) return std::unexpected(sealed.error());
    Recipient rec;
    rec.id = *did;
    rec.enc = base64url_encode(sealed->enc);
    rec.wrapped_key = base64url_encode(sealed->ct);
    policy.recipients.push_back(std::move(rec));
  }

  auto written = dir.write(kPolicyPath, encryption_policy_to_yaml(policy));
  if (!written) return std::unexpected(written.error());

  return rebuild_manifest(dir);
}

Result<void> decrypt_container(const DirectoryContainer& dir,
                               const EncPrivateKey& key) {
  if (!dir.contains(kPolicyPath)) {
    return fail(ErrorCode::kNotFound, "encryption/policy.yaml not found");
  }
  auto raw = dir.read(kPolicyPath);
  if (!raw) return std::unexpected(raw.error());
  auto policy = parse_encryption_policy_yaml(*raw);
  if (!policy) return std::unexpected(policy.error());

  auto my_did = key.did_key();
  if (!my_did) return std::unexpected(my_did.error());

  const Recipient* mine = nullptr;
  for (const auto& r : policy->recipients) {
    if (r.id == *my_did) {
      mine = &r;
      break;
    }
  }
  if (!mine) return fail(ErrorCode::kUnsupported, "key is not a recipient");

  auto enc = base64url_decode(mine->enc);
  if (!enc) return std::unexpected(enc.error());
  auto wrapped = base64url_decode(mine->wrapped_key);
  if (!wrapped) return std::unexpected(wrapped.error());
  auto cek = key.hpke_open(*enc, *wrapped, kHpkeInfo);
  if (!cek) return std::unexpected(cek.error());

  for (const auto& f : policy->encrypted_files) {
    auto sealed = dir.read(f);
    if (!sealed) return std::unexpected(sealed.error());
    auto plaintext = aes256gcm_open(*cek, f, *sealed);
    if (!plaintext) return std::unexpected(plaintext.error());
    auto written = dir.write(f, *plaintext);
    if (!written) return std::unexpected(written.error());
  }

  // Now that the headings are readable, hold the attestation to account. This
  // is what makes it more than a note to self: a claim that does not match the
  // content it sealed fails at the first recipient who opens the document
  // (spec 5.2.1).
  if (policy->structure.present && dir.contains(kSchemaPath) &&
      dir.contains(kContentPath)) {
    auto schema_yaml = dir.read(kSchemaPath);
    if (!schema_yaml) return std::unexpected(schema_yaml.error());
    auto content = dir.read(kContentPath);
    if (!content) return std::unexpected(content.error());
    auto actual = bound_sections(*schema_yaml, *content);
    if (!actual) return std::unexpected(actual.error());
    if (*actual != policy->structure.bound_sections) {
      return fail(ErrorCode::kParse,
                  "E_STRUCTURE_ATTESTATION_INVALID: the sealed content does not "
                  "match the structure recorded in encryption/policy.yaml");
    }
  }

  auto removed = dir.remove(kPolicyPath);
  if (!removed) return std::unexpected(removed.error());
  return rebuild_manifest(dir);
}

}  // namespace mcdf
