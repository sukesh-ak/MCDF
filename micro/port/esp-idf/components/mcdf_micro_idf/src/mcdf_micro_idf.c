/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */

/* The ESP-IDF adapter: two sources, no allocation, no state beyond the handle
 * the caller supplied. Every ESP-IDF header in this library is included from
 * this file and its own header. */

#include "mcdf_micro_idf.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <sys/stat.h>
#include <unistd.h>

/* ------------------------------------------------------------------- file */

static int mcdf_micro_idf_file_read(void *ctx, uint64_t off, void *dst,
                                    size_t len) {
  mcdf_micro_idf_file *file = (mcdf_micro_idf_file *)ctx;
  unsigned char       *out = (unsigned char *)dst;

  /* off_t is 32 bits here, so an offset past 2 GiB cannot be seeked to and
   * would silently truncate on the cast. The container format allows far more
   * than a part like this will ever hold; refusing is the honest answer. */
  if (off > (uint64_t)LONG_MAX) return -1;
  if (lseek(file->fd, (off_t)off, SEEK_SET) < 0) return -1;

  while (len > 0u) {
    ssize_t got = read(file->fd, out, len);
    /* A short read is not a failure - a VFS driver may return less than asked
     * for and expect to be called again - but a zero-byte one is. The reader
     * never asks for bytes it has not already established are inside the
     * container, so end-of-file here means the container is not the size the
     * filesystem said it was. */
    if (got <= 0) return -1;
    out += (size_t)got;
    len -= (size_t)got;
  }
  return 0;
}

esp_err_t mcdf_micro_idf_file_open(mcdf_micro_idf_file *file, const char *path,
                                   mcdf_micro_source *out_src) {
  struct stat st;
  int         fd;

  if (file == NULL || path == NULL || out_src == NULL) {
    return ESP_ERR_INVALID_ARG;
  }

  /* Marked closed before anything can fail, so a caller that unconditionally
   * closes on the error path - which is the shape ESP_ERROR_CHECK pushes you
   * towards - does not hand close() an uninitialised descriptor. */
  file->fd = -1;

  fd = open(path, O_RDONLY);
  if (fd < 0) return errno == ENOENT ? ESP_ERR_NOT_FOUND : ESP_FAIL;

  /* Sizing the container is not optional: the index walk needs the total length
   * to bound every read, and a VFS that cannot stat cannot be read this way. */
  if (fstat(fd, &st) != 0 || st.st_size < 0) {
    close(fd);
    return ESP_FAIL;
  }

  file->fd = fd;
  out_src->ctx = file;
  out_src->read = mcdf_micro_idf_file_read;
  out_src->size = (uint64_t)st.st_size;
  return ESP_OK;
}

void mcdf_micro_idf_file_close(mcdf_micro_idf_file *file) {
  if (file == NULL || file->fd < 0) return;
  close(file->fd);
  file->fd = -1;
}

/* -------------------------------------------------------------- partition */

static int mcdf_micro_idf_partition_read(void *ctx, uint64_t off, void *dst,
                                         size_t len) {
  const mcdf_micro_idf_partition *partition =
      (const mcdf_micro_idf_partition *)ctx;

  /* Written as a subtraction rather than `off + len > size` because that sum
   * can wrap, and a wrapped bound check on untrusted input is how a reader ends
   * up reading someone else's flash. */
  if (off > partition->size || (uint64_t)len > partition->size - off) return -1;

  return esp_partition_read(partition->part, (size_t)off, dst, len) == ESP_OK
             ? 0
             : -1;
}

esp_err_t mcdf_micro_idf_partition_open(mcdf_micro_idf_partition *partition,
                                        const esp_partition_t *part,
                                        uint64_t size,
                                        mcdf_micro_source *out_src) {
  if (partition == NULL || part == NULL || out_src == NULL) {
    return ESP_ERR_INVALID_ARG;
  }
  if (size == 0u) size = (uint64_t)part->size;
  if (size > (uint64_t)part->size) return ESP_ERR_INVALID_SIZE;

  partition->part = part;
  partition->size = size;
  out_src->ctx = partition;
  out_src->read = mcdf_micro_idf_partition_read;
  out_src->size = size;
  return ESP_OK;
}
