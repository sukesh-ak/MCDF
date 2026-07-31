/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* Opening an .mcdf container held in a flash partition, on a bare board.
 *
 * A partition rather than an SD card on purpose: it needs no wiring, no pin
 * assignment and no board-specific mount, so this runs on any part with a spare
 * data partition. Swapping in mcdf_micro_idf_file_open("/sdcard/book.mcdf", ...)
 * is the only change needed to read from a card instead - everything below the
 * source is identical, which is the point of the callback.
 *
 * It also calls every gated entry point unconditionally, because that is the
 * library's contract: a disabled feature keeps its declaration and returns
 * MCDF_MICRO_E_DISABLED, so one piece of caller code compiles against every
 * build configuration. What changes between configurations is the answer, not
 * the source - and this app is compiled in all four of them.
 */

#include <inttypes.h>
#include <stdio.h>

#include "esp_log.h"
#include "esp_partition.h"
#include "mcdf_micro/mcdf_micro.h"
#include "mcdf_micro/mcdf_micro_render.h"
#include "mcdf_micro/mcdf_micro_verify.h"
#include "mcdf_micro_idf.h"

static const char *TAG = "mcdf";

/* The partition label to read from - see partitions.csv. */
#define MCDF_PARTITION_LABEL "mcdf"

/* All reader state, sized for the documents this firmware carries: 64 members
 * and 2 KB of paths between them. There is no allocator hook and no runtime
 * sizing query, so this number is the memory budget, stated once and honestly.
 * Under-estimating returns MCDF_MICRO_E_ARENA - never a partial index. */
static uint8_t s_arena[MCDF_MICRO_ARENA_SIZE(64, 2048)];

/* The render window. Sized by the largest single top-level block in the
 * documents carried, NOT by the document: a window ends only at a block
 * boundary, so this is what the guarantee costs. A Core-only firmware deletes
 * this line along with the call below - it is the one place the render gate
 * costs RAM rather than just returning E_DISABLED. */
static uint8_t s_window[4096];

/* ------------------------------------------------------------------ issues */

static void on_issue(void *ctx, mcdf_micro_status code, const char *detail) {
  (void)ctx;
  ESP_LOGW(TAG, "  %s: %s", mcdf_micro_status_str(code),
           detail != NULL ? detail : "(no detail)");
}

/* ------------------------------------------------------------------ events */

/* A layout engine lives here. This one counts, so the example stays about the
 * reader rather than about drawing. */
typedef struct {
  unsigned blocks;
  unsigned headings;
  unsigned member_links; /* links and images naming a member of this container */
} event_counts;

static int on_enter_block(void *ctx, mcdf_micro_block type, const void *detail) {
  event_counts *counts = (event_counts *)ctx;
  (void)detail;
  counts->blocks++;
  if (type == MCDF_MICRO_BLOCK_H) counts->headings++;
  return 0;
}

static int on_enter_span(void *ctx, mcdf_micro_span type, const void *detail) {
  event_counts *counts = (event_counts *)ctx;
  if ((type == MCDF_MICRO_SPAN_LINK || type == MCDF_MICRO_SPAN_IMAGE) &&
      detail != NULL) {
    const mcdf_micro_link_detail *link = (const mcdf_micro_link_detail *)detail;
    /* A destination that names a member is read with mcdf_micro_read_at();
     * anything else is external and a renderer MUST NOT fetch it (spec 4.1). */
    if (link->is_member) counts->member_links++;
  }
  return 0;
}

/* -------------------------------------------------------------------- app */

static void report_features(void) {
  const uint32_t features = mcdf_micro_features();
  ESP_LOGI(TAG, "mcdf_micro %d.%d.%d - core%s%s", MCDF_MICRO_VERSION_MAJOR,
           MCDF_MICRO_VERSION_MINOR, MCDF_MICRO_VERSION_PATCH,
           (features & MCDF_MICRO_FEATURE_INTEGRITY) ? " integrity" : "",
           (features & MCDF_MICRO_FEATURE_RENDER) ? " render" : "");
}

static void read_document(mcdf_micro_reader *reader) {
  char             value[128];
  size_t           len;
  size_t           count;
  size_t           issues;
  mcdf_micro_status status;

  if (mcdf_micro_meta(reader, "title", value, sizeof value, &len) ==
      MCDF_MICRO_OK) {
    ESP_LOGI(TAG, "title: %s", value);
  }
  if (mcdf_micro_meta_author(reader, 0, value, sizeof value, &len) ==
      MCDF_MICRO_OK) {
    ESP_LOGI(TAG, "author: %s", value);
  }
  ESP_LOGI(TAG, "members: %u", (unsigned)mcdf_micro_count(reader));

  /* Core: content.md is present and every schema section binds to a heading
   * anchor. No cryptography is involved, which is the profile ladder's central
   * claim - this is a complete, conforming Core check on a part with no crypto
   * accelerator and no room for one. */
  status = mcdf_micro_validate_core(reader, on_issue, NULL, &issues);
  ESP_LOGI(TAG, "core:      %s, %u issue(s)", mcdf_micro_status_str(status),
           (unsigned)issues);

  /* Integrity: every file the manifest lists hashes to its recorded digest.
   * E_DISABLED in a build with the gate off - which is not "the document is
   * fine", and a caller must not report it as a pass. */
  status = mcdf_micro_verify_manifest(reader, on_issue, NULL, &issues);
  ESP_LOGI(TAG, "integrity: %s, %u issue(s)", mcdf_micro_status_str(status),
           (unsigned)issues);

  /* Render: walk content.md a window at a time. The events are identical to
   * parsing the whole document at once - that is the guarantee the windowed
   * form makes, and it is why a reader sized by its screen can use it. */
  {
    event_counts counts = {0, 0, 0};
    const mcdf_micro_render_callbacks callbacks = {
        on_enter_block, NULL, on_enter_span, NULL, NULL};
    mcdf_micro_render_iter iter;
    int                    done = 0;

    status = mcdf_micro_render_begin(reader, s_window, sizeof s_window, &iter);
    while (status == MCDF_MICRO_OK && !done) {
      status = mcdf_micro_render_next(&iter, &callbacks, &counts, &done);
    }
    ESP_LOGI(TAG, "render:    %s, %u blocks, %u headings, %u member links",
             mcdf_micro_status_str(status), counts.blocks, counts.headings,
             counts.member_links);
  }

  if (mcdf_micro_schema_count(reader, &count) == MCDF_MICRO_OK) {
    ESP_LOGI(TAG, "schema:    %u section(s)", (unsigned)count);
  }
}

void app_main(void) {
  const esp_partition_t   *part;
  mcdf_micro_idf_partition partition;
  mcdf_micro_source        source;
  mcdf_micro_reader       *reader;
  mcdf_micro_status        status;
  esp_err_t                err;

  report_features();

  part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                  ESP_PARTITION_SUBTYPE_ANY,
                                  MCDF_PARTITION_LABEL);
  if (part == NULL) {
    ESP_LOGE(TAG, "no '%s' partition in the partition table",
             MCDF_PARTITION_LABEL);
    return;
  }
  ESP_LOGI(TAG, "partition '%s': %" PRIu32 " bytes at 0x%" PRIx32, part->label,
           part->size, part->address);

  /* Size 0 means the whole partition. Safe even for a document far smaller:
   * the archive's own end-of-archive marker stops the walk, so the erased tail
   * is never parsed. */
  err = mcdf_micro_idf_partition_open(&partition, part, 0, &source);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "partition source: %s", esp_err_to_name(err));
    return;
  }

  status = mcdf_micro_open(&source, s_arena, sizeof s_arena, &reader);
  if (status != MCDF_MICRO_OK) {
    /* E_PARSE here usually means the partition is empty - flash a container
     * into it first; the README has the parttool.py line. */
    ESP_LOGE(TAG, "open: %s", mcdf_micro_status_str(status));
    return;
  }

  read_document(reader);
  mcdf_micro_close(reader);
}
