// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project

/**
 * This library's own version, as distinct from `MCDF_VERSION` (the format
 * version written into manifests).
 *
 * It appears in `--version` and in the `generated_by` provenance stamp an
 * importer writes, so the two must not drift — which is the only reason it is a
 * shared constant rather than a literal in each.
 *
 * Deliberately absent from anything canonical: spec §10.4 fixes the render
 * format's identity as `mcdf-render/1` precisely because stamping a library
 * version into canonical bytes made them unreproducible by anyone else.
 * Provenance metadata is the opposite case — naming the exact tool is the
 * point.
 */
export const MCDF_TS_VERSION = '0.5.0';
