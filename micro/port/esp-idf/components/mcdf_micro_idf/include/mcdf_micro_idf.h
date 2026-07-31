/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */
#ifndef MCDF_MICRO_IDF_H
#define MCDF_MICRO_IDF_H

/*
 * mcdf_micro_idf - the ESP-IDF adapter for mcdf_micro.
 *
 * The core reads bytes through a callback and knows nothing about where they
 * come from. This component is the only place in the tree that knows ESP-IDF
 * exists: it binds that callback to the two places a document actually lives on
 * an ESP32 - a file on a mounted VFS (SD card, FAT, SPIFFS, LittleFS) and a
 * flash partition - and it allocates nothing doing it, because the core does
 * not either. The handle you pass in is the storage; it can be static, or on
 * the stack of a function that outlives the reader.
 *
 * It speaks both vocabularies at the boundary, deliberately. esp_err_t goes out
 * to the caller, because that is what an IDF application already checks, and
 * mcdf_micro_status comes back from the reader. Nothing here folds one into the
 * other: "the SD card is not mounted" and "this document has an unbound schema
 * section" are not the same kind of fact, and a single error space would lose
 * which of them happened.
 *
 * Read-only, like everything else here. No writing, no packing, no signing.
 *
 * Lifetime, once, because it is the one rule that bites: the handle and
 * whatever it refers to must outlive the mcdf_micro_reader built on it. The
 * reader holds the source by pointer and reads through it lazily - closing the
 * file underneath it is a use-after-close, not a no-op.
 */

#include <stdint.h>

#include "esp_err.h"
#include "esp_partition.h"
#include "mcdf_micro/mcdf_micro.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------- file */

/* A container on a mounted filesystem. The fields are an implementation
 * detail; only the two calls below may touch them.
 *
 * Initialise with MCDF_MICRO_IDF_FILE_INIT and not with `{0}`: zero is a valid
 * file descriptor, so an all-zero handle would look open to the close below and
 * take stdin down with it. The macro is the only safe way to declare one you
 * have not opened yet. */
typedef struct mcdf_micro_idf_file {
  int fd; /* negative when closed */
} mcdf_micro_idf_file;

#define MCDF_MICRO_IDF_FILE_INIT {-1}

/* Opens `path` read-only and fills in a source over it. `path` is a VFS path,
 * so it carries the mount point the caller registered ("/sdcard/book.mcdf").
 *
 * ESP_ERR_NOT_FOUND if there is no such file, ESP_ERR_INVALID_ARG for a NULL
 * argument, ESP_FAIL if the VFS refused the open or could not size the file.
 * On any failure nothing is left open and `*out_src` is untouched.
 *
 * Documents are limited to 2 GiB here rather than the 16 EiB the container
 * format allows: off_t is 32 bits in this toolchain's newlib, so a seek past
 * that cannot be expressed. The limit is checked, not assumed - a read beyond
 * it fails rather than wrapping onto the wrong block. */
esp_err_t mcdf_micro_idf_file_open(mcdf_micro_idf_file *file, const char *path,
                                   mcdf_micro_source *out_src);

/* Closes the descriptor. Passing NULL is a no-op, and so is closing a handle
 * that a failed open() left behind or that a successful one already closed -
 * open() marks the handle closed before it can fail, so every handle it has
 * touched is safe to pass here exactly once or many times. Any reader built on
 * this handle must be closed first. */
void mcdf_micro_idf_file_close(mcdf_micro_idf_file *file);

/* -------------------------------------------------------------- partition */

/* A container written directly to a flash partition - the shape a device that
 * ships with its documents baked in wants, since it needs no filesystem at all.
 * There is nothing to release, so there is no close: a partition handle is
 * borrowed from the partition table, not owned. */
typedef struct mcdf_micro_idf_partition {
  const esp_partition_t *part;
  uint64_t               size;
} mcdf_micro_idf_partition;

/* Fills in a source reading through `part`.
 *
 * `size` may be 0, meaning "the whole partition", which is the usual case and
 * is safe even when the document is much smaller: a USTAR archive ends with two
 * zero blocks and the index walk stops there, so the erased tail beyond the
 * document is never parsed and never read. Pass a real size only when something
 * else lives in the same partition after the container.
 *
 * Reads go through esp_partition_read(), so a partition covered by flash
 * encryption is decrypted transparently and the reader sees the plaintext
 * container - the container's own encryption (spec section 5) is a separate
 * layer above this one and is not what this decrypts.
 *
 * ESP_ERR_INVALID_ARG for a NULL argument, ESP_ERR_INVALID_SIZE if `size` is
 * larger than the partition. */
esp_err_t mcdf_micro_idf_partition_open(mcdf_micro_idf_partition *partition,
                                        const esp_partition_t *part,
                                        uint64_t size,
                                        mcdf_micro_source *out_src);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* MCDF_MICRO_IDF_H */
