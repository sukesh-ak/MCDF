// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// License gate for the npm side of the project.
//
// The house rule is MIT/Apache-2.0 only, with maintainer-approved exceptions
// recorded here rather than in someone's memory (constraint: ask before adding
// any dependency). Anything that ships in the browser bundle is audited
// strictly; dev/CI-only packages are audited against a wider list because they
// never reach a user.
//
//   node scripts/license-audit.mjs           # audit, exit 1 on a violation
//   node scripts/license-audit.mjs --list    # print every license found

import { execFileSync } from 'node:child_process';
import { readFileSync, existsSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = join(dirname(fileURLToPath(import.meta.url)), '..');

/**
 * Licenses acceptable for anything that ships to a user. MIT-0, 0BSD and ISC
 * are here because each is MIT with *fewer* obligations, not more.
 */
const SHIPPED_ALLOWED = new Set([
  'MIT',
  'MIT-0',
  'Apache-2.0',
  'ISC',
  'BSD-2-Clause',
  'BSD-3-Clause',
  '0BSD',
]);

/** Additionally acceptable for dev/CI-only packages. */
const DEV_ALLOWED = new Set([
  ...SHIPPED_ALLOWED,
  'CC0-1.0',
  'CC-BY-4.0',
  'Unlicense',
  'BlueOak-1.0.0',
  'Python-2.0',
  'MPL-2.0',
  'Artistic-2.0',
]);

/**
 * Explicit, reviewed exceptions. Each entry names the package and why it is
 * allowed — an unexplained exception is how a license policy quietly dies.
 */
const EXCEPTIONS = new Map([
  [
    'axe-core',
    'MPL-2.0. Dev/CI only — accessibility assertions in tests. Never imported by ' +
      'application code and never bundled, so no MPL file is distributed.',
  ],
  [
    'argparse',
    'Python-2.0 (permissive, OSI-approved). Transitive via js-yaml, which declares ' +
      'it as a runtime dependency but only uses it in its own `js-yaml` CLI binary — ' +
      "js-yaml's library code never references argparse, and it appears zero times in " +
      'the built app bundle. Approved by the maintainer 2026-07-29 as a documented ' +
      'exception rather than widening the MIT/Apache rule.',
  ],
]);

function tree(omitDev) {
  const args = ['ls', '--all', '--json', '--long', '--workspaces', '--include-workspace-root'];
  if (omitDev) args.push('--omit=dev');
  const out = execFileSync('npm', args, {
    cwd: ROOT,
    encoding: 'utf8',
    maxBuffer: 128 * 1024 * 1024,
    // `npm ls` exits non-zero on peer-dependency gripes that do not affect the
    // license question; the JSON is still complete.
    stdio: ['ignore', 'pipe', 'ignore'],
  });
  return JSON.parse(out);
}

function collect(node, into) {
  for (const [name, dep] of Object.entries(node.dependencies ?? {})) {
    if (dep.version === undefined) continue; // unmet/link placeholder
    const key = `${name}@${dep.version}`;
    if (!into.has(key)) {
      into.set(key, { name, version: dep.version, license: normalize(dep.license), path: dep.path });
    }
    collect(dep, into);
  }
  return into;
}

function normalize(license) {
  if (license === undefined || license === null) return 'UNKNOWN';
  if (typeof license === 'string') return license;
  if (typeof license === 'object' && typeof license.type === 'string') return license.type;
  return 'UNKNOWN';
}

/** Reads the license straight from disk when `npm ls` did not report one. */
function licenseFromDisk(pkgPath) {
  if (pkgPath === undefined) return 'UNKNOWN';
  const manifest = join(pkgPath, 'package.json');
  if (!existsSync(manifest)) return 'UNKNOWN';
  try {
    return normalize(JSON.parse(readFileSync(manifest, 'utf8')).license);
  } catch {
    return 'UNKNOWN';
  }
}

/** Accepts SPDX expressions like "(MIT OR Apache-2.0)" when any branch passes. */
function satisfies(license, allowed) {
  return license
    .replace(/[()]/g, ' ')
    .split(/\s+OR\s+/i)
    .some((branch) => branch.trim().split(/\s+AND\s+/i).every((t) => allowed.has(t.trim())));
}

function run() {
  const listOnly = process.argv.includes('--list');

  const shipped = collect(tree(true), new Map());
  const everything = collect(tree(false), new Map());

  for (const pkg of everything.values()) {
    if (pkg.license === 'UNKNOWN') pkg.license = licenseFromDisk(pkg.path);
  }
  for (const [key, pkg] of shipped) {
    const full = everything.get(key);
    if (full !== undefined) pkg.license = full.license;
  }

  if (listOnly) {
    const byLicense = new Map();
    for (const pkg of everything.values()) {
      const list = byLicense.get(pkg.license) ?? [];
      list.push(`${pkg.name}@${pkg.version}`);
      byLicense.set(pkg.license, list);
    }
    for (const [license, pkgs] of [...byLicense].sort()) {
      console.log(`${license} (${pkgs.length})`);
      for (const p of pkgs.sort()) console.log(`  ${p}`);
    }
    return 0;
  }

  const violations = [];
  const noted = [];

  for (const pkg of everything.values()) {
    const isShipped = shipped.has(`${pkg.name}@${pkg.version}`);
    const allowed = isShipped ? SHIPPED_ALLOWED : DEV_ALLOWED;
    if (satisfies(pkg.license, allowed)) continue;

    // A reviewed exception clears the package wherever it sits in the tree, but
    // the report always states which — an exception that quietly covers shipped
    // code is the one worth noticing.
    const exception = EXCEPTIONS.get(pkg.name);
    if (exception !== undefined) {
      const where = isShipped ? 'in the production tree' : 'dev/CI only';
      noted.push(`${pkg.name}@${pkg.version} (${pkg.license}, ${where}) — ${exception}`);
      continue;
    }
    violations.push(
      `${pkg.name}@${pkg.version}: ${pkg.license}` +
        (isShipped ? ' [SHIPPED to users]' : ' [dev/CI only]'),
    );
  }

  console.log(`audited ${everything.size} packages (${shipped.size} reach the bundle)`);
  if (noted.length > 0) {
    console.log('\napproved exceptions:');
    for (const n of noted.sort()) console.log(`  ${n}`);
  }
  if (violations.length === 0) {
    console.log('\nlicense audit: OK');
    return 0;
  }
  console.error('\nlicense audit FAILED — unapproved licenses:');
  for (const v of violations.sort()) console.error(`  ${v}`);
  console.error(
    '\nThe project ships MIT/Apache-2.0 only. Get maintainer approval and record it in\n' +
      'EXCEPTIONS in this script, or remove the dependency.',
  );
  return 1;
}

process.exitCode = run();
