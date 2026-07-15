// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/core/encrypt_ops.hpp"

#include "mcdf/container/directory_container.hpp"
#include "mcdf/core/manifest_ops.hpp"
#include "mcdf/crypto/aead.hpp"
#include "mcdf/crypto/encoding.hpp"
#include "mcdf/serialize/policy_yaml.hpp"

namespace mcdf {
namespace {

constexpr std::string_view kHpkeInfo = "mcdf/hpke/cek/v1";
constexpr const char* kPolicyPath = "encryption/policy.yaml";

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

  EncryptionPolicy policy;
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

  auto removed = dir.remove(kPolicyPath);
  if (!removed) return std::unexpected(removed.error());
  return rebuild_manifest(dir);
}

}  // namespace mcdf
