/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project */
#ifndef MCDF_MICRO_HOST_JCS_H
#define MCDF_MICRO_HOST_JCS_H

/*
 * The canonical manifest emitter - host only, and the placement is the point.
 *
 * `conformance/run.sh` scores `manifest <container>` byte-for-byte against
 * RFC 8785 canonical JSON. Producing those bytes is authoring work a reader
 * would otherwise never need, and the alternative to writing it was a
 * permanently unscored corner of the kit carrying an exemption note.
 *
 * So it exists, and it lives here: `micro/host/` never ships to a device, so
 * the target pays nothing for it. The discipline is placement and it is not
 * negotiable - nothing under `micro/src/` ever learns to write. The emitter
 * exists to be scored, not to author.
 *
 * It builds the manifest the same way the reference does: hash every member
 * the membership rule does not exempt, sort by key, emit. It never reads the
 * container's stored manifest.json - a canonical manifest is a statement about
 * the bytes that are there, not a copy of a claim about them.
 */

#include <stdio.h>

#include "mcdf_micro/mcdf_micro.h"

/* The most members one canonical manifest may carry. Static storage, because
 * keeping the no-allocation discipline on the host is free and it keeps the
 * emitter honest about being a fixed-cost thing. */
#define MCDF_MICRO_JCS_MAX_FILES 512u

/* Writes the canonical manifest for `reader`'s container to `out`, with no
 * trailing newline (the caller decides, as the kit expects exactly one).
 * `out` must be in binary mode: this is a byte-exact format, and a CRLF
 * translation on stdout is the defect the kit exists to catch.
 *
 * Returns MCDF_MICRO_OK, E_DISABLED when the Integrity gate is off (there is
 * no hashing to build a manifest from), or E_ARENA past
 * MCDF_MICRO_JCS_MAX_FILES members. */
mcdf_micro_status mcdf_micro_jcs_manifest(mcdf_micro_reader *reader, FILE *out);

#endif /* MCDF_MICRO_HOST_JCS_H */
