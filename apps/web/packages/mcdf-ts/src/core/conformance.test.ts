// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The conformance kit IS this implementation's test suite. Every
// vector under conformance/vectors runs here; a divergence is either a bug in
// mcdf-ts or a gap in the spec.

import { readFileSync, readdirSync, existsSync } from 'node:fs';
import { join } from 'node:path';
import { fileURLToPath } from 'node:url';

import { describe, expect, it } from 'vitest';

import { openContainer } from '../node/fs-container.js';
import { loadDocument } from './document.js';
import { buildManifest, manifestToCanonicalJson } from './manifest.js';
import { renderCanonical } from './render.js';
import { isProfile, validate, type Profile } from './validate.js';

const HERE = fileURLToPath(new URL('.', import.meta.url));
const KIT = join(HERE, '..', '..', '..', '..', '..', '..', 'conformance');

interface Case {
  description?: string;
  profile?: string;
  expect?: 'pass' | 'reject';
  error?: string;
  check?: string;
}

function vectors(kind: 'valid' | 'invalid' | 'canonical'): { name: string; dir: string }[] {
  const root = join(KIT, 'vectors', kind);
  if (!existsSync(root)) return [];
  return readdirSync(root, { withFileTypes: true })
    .filter((e) => e.isDirectory() && existsSync(join(root, e.name, 'case.json')))
    .map((e) => ({ name: e.name, dir: join(root, e.name) }));
}

function readCase(dir: string): Case {
  return JSON.parse(readFileSync(join(dir, 'case.json'), 'utf8')) as Case;
}

/**
 * Both published serializations of every vector. Spec §3 makes TAR the one
 * REQUIRED form and the directory an OPTIONAL authoring convenience, so both
 * are scored here: `tar` is the path that has to work — it is the only one
 * available in a browser, where `DirectoryContainer` does not exist — and
 * `dir` is scored because this implementation supports it under node.
 */
const FORMS = [
  { form: 'tar', member: 'container.mcdf' },
  { form: 'dir', member: 'container' },
] as const;

/**
 * Profiles mcdf-ts claims — all five since spec §10.4 fixed the canonical render
 * and the kit published vectors for it. Render was the last holdout: it has to be
 * byte-identical to the reference, and until there was something to prove that
 * against, this implementation reported `E_UNIMPLEMENTED` rather than guess.
 */
const IMPLEMENTED: Profile[] = ['core', 'integrity', 'signed', 'encrypted', 'render'];

function isImplemented(profile: Profile): boolean {
  return IMPLEMENTED.includes(profile);
}

it('finds the conformance kit', () => {
  expect(existsSync(join(KIT, 'run.sh'))).toBe(true);
});

it('every vector is published in both serializations', () => {
  const missing: string[] = [];
  for (const kind of ['valid', 'invalid', 'canonical'] as const) {
    for (const { name, dir } of vectors(kind)) {
      for (const { form, member } of FORMS) {
        if (!existsSync(join(dir, member))) missing.push(`${kind}/${name} (${form})`);
      }
    }
  }
  // A packed form that has drifted out of existence would otherwise show up as
  // a pile of confusing per-vector failures; repack with
  // `cmake -DMCDF_CLI=<cli> -DMODE=pack -P cmake/vectors.cmake`.
  expect(missing).toEqual([]);
});

describe('valid vectors', () => {
  for (const { name, dir } of vectors('valid')) {
    const c = readCase(dir);
    const profile = (c.profile ?? 'integrity') as Profile;
    if (!isProfile(profile)) throw new Error(`vector ${name}: bad profile`);

    const run = isImplemented(profile) ? it : it.skip;
    for (const { form, member } of FORMS) {
      run(`${name} (${profile}, ${form})`, () => {
        const container = openContainer(join(dir, member));
        const report = validate(container, loadDocument(container), profile);
        expect(report.issues).toEqual([]);
        expect(report.ok).toBe(true);
      });
    }
  }
});

describe('invalid vectors', () => {
  for (const { name, dir } of vectors('invalid')) {
    const c = readCase(dir);
    const profile = (c.profile ?? 'integrity') as Profile;
    if (!isProfile(profile)) throw new Error(`vector ${name}: bad profile`);

    const run = isImplemented(profile) ? it : it.skip;
    for (const { form, member } of FORMS) {
      run(`${name} raises ${c.error ?? '(any error)'} (${form})`, () => {
        const container = openContainer(join(dir, member));
        const report = validate(container, loadDocument(container), profile);
        expect(report.ok).toBe(false);
        if (c.error !== undefined) {
          expect(report.issues.map((i) => i.code)).toContain(c.error);
        }
      });
    }
  }
});

describe('canonical byte vectors', () => {
  for (const { name, dir } of vectors('canonical')) {
    const check = readCase(dir).check ?? 'canonical-manifest';

    if (check === 'canonical-manifest') {
      for (const { form, member } of FORMS) {
        it(`${name} is byte-identical to the reference output (${form})`, () => {
          const container = openContainer(join(dir, member));
          const canonical = `${manifestToCanonicalJson(buildManifest(container))}\n`;
          const expected = readFileSync(join(dir, 'expected', 'manifest.json'), 'utf8');
          expect(canonical).toBe(expected);
        });
      }
      continue;
    }

    if (check === 'canonical-render') {
      // These bytes were produced by libmcdf and are reproduced here by a
      // different CommonMark parser: this is the check that makes
      // "independently implementable" a measured claim rather than a stated one.
      for (const [format, file] of [
        ['html', 'render.html'],
        ['text', 'render.txt'],
      ] as const) {
        for (const { form, member } of FORMS) {
          it(`${name} (${format}) is byte-identical to the reference output (${form})`, () => {
            const container = openContainer(join(dir, member));
            expect(renderCanonical(container, format)).toBe(
              readFileSync(join(dir, 'expected', file), 'utf8'),
            );
          });
        }
      }
      continue;
    }

    it(`${name} has a known check`, () => {
      throw new Error(`unknown check in case.json: ${check}`);
    });
  }
});
