/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* The Core profile (spec 4.2, conformance/errors.md).
 *
 * Two claims, and they are the whole of Core: content.md exists, and every
 * section schema.yaml declares binds to a heading anchor in it. No hashing, no
 * keys - which is the profile ladder's central claim, and this file is what
 * makes it true on a part with no cryptography at all.
 *
 * The verdict is reported through a callback rather than accumulated, because
 * accumulating means owning storage and the core never allocates. It also
 * means a device can react to the first issue and stop. */

#include "internal.h"

static void mm_report(mcdf_micro_issue_fn on_issue, void *ctx,
                      mcdf_micro_status code, const char *detail,
                      size_t *issues) {
  if (issues != NULL) *issues += 1u;
  if (on_issue != NULL) on_issue(ctx, code, detail);
}

int mcdf_micro_manifest_excluded(const char *path) {
  static const char kSignatures[] = "signatures/";
  size_t i;

  if (path == NULL) return 0;
  if (mm_streq(path, "manifest.json")) return 1;
  if (mm_streq(path, "audit.log")) return 1;
  if (mm_streq(path, "audit.checkpoint")) return 1;
  for (i = 0; i + 1 < sizeof kSignatures; ++i) {
    if (path[i] != kSignatures[i]) return 0;
  }
  return 1;
}

mcdf_micro_status mcdf_micro_validate_core(mcdf_micro_reader *reader,
                                           mcdf_micro_issue_fn on_issue,
                                           void *ctx, size_t *out_issues) {
  mcdf_micro_member_info info;
  size_t sections = 0, i;
  mcdf_micro_status st;

  if (reader == NULL) return MCDF_MICRO_E_INVAL;
  if (out_issues != NULL) *out_issues = 0;

  if (mcdf_micro_member(reader, "content.md", &info) != MCDF_MICRO_OK) {
    mm_report(on_issue, ctx, MCDF_MICRO_E_MISSING_CONTENT, "content.md",
              out_issues);
  }

  /* No schema, no structural claims to check - a valid thing for a document
   * to do (spec 4.2), not an omission. */
  st = mcdf_micro_schema_count(reader, &sections);
  if (st == MCDF_MICRO_E_NOT_FOUND) return MCDF_MICRO_OK;
  if (st != MCDF_MICRO_OK) return st;
  if (sections == 0) return MCDF_MICRO_OK;

  /* Over a sealed content.md the headings are unreadable, and spec 5.2.1 puts
   * the binding result in the policy's structure attestation instead. This
   * reader does not evaluate that, so it says so once. Reporting the sections
   * as missing would blame the document for something that may well be fine,
   * which conformance/errors.md names as the wrong answer. */
  if (mcdf_micro_is_sealed(reader, "content.md")) {
    mm_report(on_issue, ctx, MCDF_MICRO_E_UNIMPLEMENTED,
              "schema binding over a sealed content.md (spec 5.2.1)",
              out_issues);
    return MCDF_MICRO_OK;
  }

  for (i = 0; i < sections; ++i) {
    char id[MCDF_MICRO_ID_MAX];
    size_t id_len = 0;
    int required = 0;

    st = mcdf_micro_schema_at(reader, i, id, sizeof id, &id_len, &required);
    if (st != MCDF_MICRO_OK) return st;
    if (id_len == 0) continue; /* a section with no id binds to nothing */

    if (mcdf_micro_has_anchor(reader, id) == MCDF_MICRO_OK) continue;
    mm_report(on_issue, ctx,
              required ? MCDF_MICRO_E_REQUIRED_SECTION_MISSING
                       : MCDF_MICRO_E_SCHEMA_UNBOUND,
              id, out_issues);
  }
  return MCDF_MICRO_OK;
}
