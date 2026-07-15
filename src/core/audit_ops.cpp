// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/core/audit_ops.hpp"

#include <nlohmann/json.hpp>

#include <exception>

#include "mcdf/container/container.hpp"
#include "mcdf/container/directory_container.hpp"
#include "mcdf/crypto/hash.hpp"
#include "mcdf/crypto/jws.hpp"
#include "mcdf/serialize/audit_log.hpp"

namespace nl = nlohmann;

namespace mcdf {
namespace {

constexpr const char* kAuditLog = "audit.log";
constexpr const char* kCheckpoint = "audit.checkpoint";

std::string genesis() { return std::string(64, '0'); }

std::string entry_hash(const AuditEntry& entry) {
  return sha256_hex(audit_entry_to_line(entry));
}

Result<std::string> current_head(const Container& container) {
  auto entries = read_audit_log(container);
  if (!entries) return std::unexpected(entries.error());
  return entries->empty() ? genesis() : entry_hash(entries->back());
}

}  // namespace

Result<std::vector<AuditEntry>> read_audit_log(const Container& container) {
  if (!container.contains(kAuditLog)) return std::vector<AuditEntry>{};
  auto raw = container.read(kAuditLog);
  if (!raw) return std::unexpected(raw.error());
  return parse_audit_log(*raw);
}

Result<void> audit_append(const DirectoryContainer& dir, std::string_view action,
                          std::string_view actor, std::string_view timestamp) {
  auto head = current_head(dir);
  if (!head) return std::unexpected(head.error());

  AuditEntry entry{std::string(timestamp), std::string(action),
                   std::string(actor), *head};
  return dir.append(kAuditLog, audit_entry_to_line(entry) + "\n");
}

Result<AuditVerification> audit_verify(const Container& container) {
  auto entries = read_audit_log(container);
  if (!entries) return std::unexpected(entries.error());

  AuditVerification v;
  v.entries = entries->size();
  std::string expected_prev = genesis();
  for (std::size_t i = 0; i < entries->size(); ++i) {
    if ((*entries)[i].prev_hash != expected_prev) {
      v.ok = false;
      v.error = "hash chain broken at entry " + std::to_string(i);
      return v;
    }
    expected_prev = entry_hash((*entries)[i]);
  }
  v.ok = true;
  return v;
}

Result<void> audit_checkpoint(const DirectoryContainer& dir,
                              const PrivateKey& key, std::string_view kid) {
  auto head = current_head(dir);
  if (!head) return std::unexpected(head.error());
  auto jws = jws_sign_detached(key, *head, kid);
  if (!jws) return std::unexpected(jws.error());

  nl::json j;
  j["head"] = *head;
  j["signature"] = *jws;
  return dir.write(kCheckpoint, j.dump(2));
}

Result<CheckpointResult> audit_verify_checkpoint(const Container& container) {
  CheckpointResult r;
  if (!container.contains(kCheckpoint)) return r;
  r.present = true;

  auto raw = container.read(kCheckpoint);
  if (!raw) return std::unexpected(raw.error());

  std::string head, signature;
  try {
    const auto j = nl::json::parse(*raw);
    head = j.value("head", std::string{});
    signature = j.value("signature", std::string{});
  } catch (const std::exception& e) {
    return fail(ErrorCode::kParse, std::string("audit.checkpoint: ") + e.what());
  }
  r.head = head;

  auto verified = jws_verify_detached(signature, head);
  if (!verified) return std::unexpected(verified.error());
  r.kid = verified->kid;

  // The chain must be intact from genesis THROUGH the entry the checkpoint
  // committed to. A break anywhere up to the head invalidates the checkpoint,
  // even if the head entry itself was untouched.
  auto entries = read_audit_log(container);
  if (!entries) return std::unexpected(entries.error());
  bool head_intact = (head == genesis());
  std::string expected_prev = genesis();
  for (const auto& e : *entries) {
    if (e.prev_hash != expected_prev) break;  // chain broke before the head
    const std::string h = entry_hash(e);
    if (h == head) {
      head_intact = true;
      break;
    }
    expected_prev = h;
  }

  r.valid = verified->valid && head_intact;
  return r;
}

}  // namespace mcdf
