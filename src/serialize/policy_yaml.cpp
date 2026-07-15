// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/serialize/policy_yaml.hpp"

#include <yaml-cpp/yaml.h>

#include <exception>
#include <sstream>
#include <string>

namespace mcdf {

Result<EncryptionPolicy> parse_encryption_policy_yaml(std::string_view yaml) {
  try {
    const YAML::Node n = YAML::Load(std::string(yaml));
    EncryptionPolicy p;
    if (n["method"]) p.method = n["method"].as<std::string>();
    if (n["key_management"]) p.key_management = n["key_management"].as<std::string>();
    if (n["encrypted_files"]) {
      for (const auto& f : n["encrypted_files"])
        p.encrypted_files.push_back(f.as<std::string>());
    }
    if (n["recipients"]) {
      for (const auto& r : n["recipients"]) {
        Recipient recipient;
        recipient.id = r["id"] ? r["id"].as<std::string>() : "";
        recipient.enc = r["enc"] ? r["enc"].as<std::string>() : "";
        recipient.wrapped_key =
            r["wrapped_key"] ? r["wrapped_key"].as<std::string>() : "";
        p.recipients.push_back(std::move(recipient));
      }
    }
    return p;
  } catch (const std::exception& e) {
    return fail(ErrorCode::kParse, std::string("policy.yaml: ") + e.what());
  }
}

std::string encryption_policy_to_yaml(const EncryptionPolicy& policy) {
  std::ostringstream out;
  out << "method: " << policy.method << "\n";
  out << "key_management: " << policy.key_management << "\n";
  out << "encrypted_files:\n";
  for (const auto& f : policy.encrypted_files) out << "  - " << f << "\n";
  out << "recipients:\n";
  for (const auto& r : policy.recipients) {
    out << "  - id: " << r.id << "\n";
    out << "    enc: " << r.enc << "\n";
    out << "    wrapped_key: " << r.wrapped_key << "\n";
  }
  return out.str();
}

}  // namespace mcdf
