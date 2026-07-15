// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 The MCDF Project
#include "mcdf/serialize/audit_log.hpp"

namespace mcdf {

std::string audit_entry_to_line(const AuditEntry& entry) {
  return entry.timestamp + "\t" + entry.action + "\t" + entry.actor + "\t" +
         entry.prev_hash;
}

Result<std::vector<AuditEntry>> parse_audit_log(std::string_view text) {
  std::vector<AuditEntry> out;
  std::size_t start = 0;
  while (start < text.size()) {
    std::size_t nl = text.find('\n', start);
    const std::string_view line =
        (nl == std::string_view::npos) ? text.substr(start)
                                       : text.substr(start, nl - start);
    start = (nl == std::string_view::npos) ? text.size() : nl + 1;
    if (line.empty()) continue;

    std::string fields[4];
    std::size_t field = 0;
    std::size_t pos = 0;
    bool too_many = false;
    while (pos <= line.size()) {
      const std::size_t tab = line.find('\t', pos);
      const std::string_view part =
          (tab == std::string_view::npos) ? line.substr(pos)
                                          : line.substr(pos, tab - pos);
      if (field >= 4) { too_many = true; break; }
      fields[field++] = std::string(part);
      if (tab == std::string_view::npos) break;
      pos = tab + 1;
    }
    if (too_many || field != 4) {
      return fail(ErrorCode::kParse, "malformed audit.log entry");
    }
    out.push_back({fields[0], fields[1], fields[2], fields[3]});
  }
  return out;
}

}  // namespace mcdf
