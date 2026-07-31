// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The `mcdf-ts` CLI: a thin Node wrapper exposing the verbs the conformance kit
// scores, so this implementation can be run through `conformance/run.sh` next to
// the reference C++ CLI.
//
// Verb names, flag names and output wording deliberately mirror `apps/cli` —
// two implementations that cannot be swapped in a script are two implementations
// nobody will compare.

import { writeFileSync, mkdirSync, readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';

import { MemoryContainer, type WritableContainer } from '../container/container.js';
import { EncPrivateKey, EncPublicKey } from '../crypto/enc-keys.js';
import { SigningKey, type SignatureAlgorithm } from '../crypto/keys.js';
import {
  auditAppend,
  auditCheckpoint,
  auditVerify,
  auditVerifyCheckpoint,
  readAuditLog,
} from '../core/audit.js';
import { loadDocument } from '../core/document.js';
import { decryptContainer, encryptContainer } from '../core/encrypt.js';
import {
  buildManifest,
  manifestToCanonicalJson,
  parseManifestJson,
  verifyManifest,
} from '../core/manifest.js';
import { renderCanonical, isRenderFormat, type RenderError } from '../core/render.js';
import { signContainer, signaturePath, verifyContainer } from '../core/sign.js';
import { isProfile, validate, PROFILES, type Profile } from '../core/validate.js';
import { DirectoryContainer, openContainer, toMemoryContainer } from '../node/fs-container.js';

const VERSION = '0.4.0';

const USAGE = `mcdf-ts ${VERSION} — independent TypeScript implementation of MCDF

usage:
  mcdf-ts validate <container> [--profile ${PROFILES.join('|')}]
  mcdf-ts manifest <container> [--verify]
  mcdf-ts inspect  <container>
  mcdf-ts pack     <directory> <out.mcdf>
  mcdf-ts unpack   <in.mcdf> <directory>
  mcdf-ts keygen   --out <key.pem> [--type ed25519|ecdsa-p256|x25519]
  mcdf-ts sign     <container> --key <key.pem> [--name author]
  mcdf-ts verify   <container>
  mcdf-ts encrypt  <container> --recipient <did:key> [--recipient …] [--file <path> …]
  mcdf-ts decrypt  <container> --key <x25519.pem>
  mcdf-ts audit    <container> [--append <ACTION> --actor <name>] [--checkpoint --key <key.pem>]
  mcdf-ts render   <html|text> <container> [-o <file>]
  mcdf-ts --version

A container is either a directory or a .mcdf (USTAR) file. Verbs that write
(sign, encrypt, decrypt, audit --append/--checkpoint) need a directory.
`;

/** Flags that consume the following argument; everything else is a switch. */
const VALUE_FLAGS = new Set([
  '--profile',
  '--out',
  '-o',
  '--type',
  '--key',
  '--name',
  '--recipient',
  '--file',
  '--append',
  '--actor',
]);

interface Args {
  positional: string[];
  /** Last value seen for each flag. */
  values: Map<string, string>;
  /** Every value seen, for repeatable flags like --recipient. */
  repeated: Map<string, string[]>;
  switches: Set<string>;
}

function fail(message: string, code = 1): never {
  process.stderr.write(`error: ${message}\n`);
  process.exit(code);
}

function parseArgs(argv: string[]): Args {
  const args: Args = {
    positional: [],
    values: new Map(),
    repeated: new Map(),
    switches: new Set(),
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i] as string;
    if (!a.startsWith('-')) {
      args.positional.push(a);
      continue;
    }
    if (VALUE_FLAGS.has(a)) {
      const value = argv[++i];
      if (value === undefined) fail(`${a} needs a value`, 2);
      args.values.set(a, value);
      args.repeated.set(a, [...(args.repeated.get(a) ?? []), value]);
      continue;
    }
    args.switches.add(a);
  }
  return args;
}

function requirePositional(args: Args, usage: string): string {
  const first = args.positional[0];
  if (first === undefined) fail(`usage: ${usage}`, 2);
  return first;
}

function requireValue(args: Args, flag: string, usage: string): string {
  const value = args.values.get(flag);
  if (value === undefined) fail(`${flag} is required\nusage: ${usage}`, 2);
  return value;
}

/** Opens a container that will be written to; only a directory qualifies. */
function openWritable(path: string): DirectoryContainer & WritableContainer {
  const container = openContainer(path);
  if (!(container instanceof DirectoryContainer)) {
    fail(`${path} is a packed .mcdf; unpack it first (writing verbs need a directory)`);
  }
  return container;
}

function readTextFile(path: string, what: string): string {
  try {
    return readFileSync(path, 'utf8');
  } catch {
    return fail(`cannot read ${what} ${path}`);
  }
}

/** RFC 3339 in UTC, the format `audit.log` records. */
function nowRfc3339(): string {
  return `${new Date().toISOString().slice(0, 19)}Z`;
}

function profileOf(args: Args): Profile {
  const name = args.values.get('--profile');
  if (name === undefined) return 'integrity';
  if (!isProfile(name)) fail(`unknown profile: ${name}`, 2);
  return name;
}

function cmdValidate(args: Args): number {
  const path = requirePositional(args, 'mcdf-ts validate <container> [--profile <p>]');
  const profile = profileOf(args);

  const container = openContainer(path);
  const report = validate(container, loadDocument(container), profile);

  if (report.ok) {
    process.stdout.write(`valid (${profile} profile)\n`);
    return 0;
  }
  for (const issue of report.issues) {
    process.stdout.write(`${issue.code}: ${issue.message}\n`);
  }
  return 1;
}

function cmdManifest(args: Args): number {
  const path = requirePositional(args, 'mcdf-ts manifest <container> [--verify]');
  const container = openContainer(path);

  if (!args.switches.has('--verify')) {
    process.stdout.write(`${manifestToCanonicalJson(buildManifest(container))}\n`);
    return 0;
  }

  if (!container.has('manifest.json')) fail('manifest.json not found in container');
  const manifest = parseManifestJson(container.readText('manifest.json'));
  const result = verifyManifest(container, manifest);
  if (result.ok) {
    const count = Object.keys(manifest.files).length;
    process.stdout.write(`manifest OK (${count} files, ${manifest.hash_algorithm})\n`);
    return 0;
  }
  process.stdout.write('manifest FAILED\n');
  for (const p of result.mismatched) process.stdout.write(`  mismatch: ${p}\n`);
  for (const p of result.missing) process.stdout.write(`  missing:  ${p}\n`);
  for (const p of result.extra) process.stdout.write(`  extra:    ${p}\n`);
  return 1;
}

function cmdRender(args: Args): number {
  const usage = 'mcdf-ts render <html|text> <container> [-o <file>]';
  const [format, path] = args.positional;
  if (format === undefined || path === undefined) fail(`usage: ${usage}`, 2);
  if (!isRenderFormat(format)) {
    // pdf and docx are named rather than lumped in with typos: the reference
    // CLI defers them explicitly, and a user who asks deserves to know which
    // it is.
    const known = format === 'pdf' || format === 'docx';
    fail(known ? `${format} rendering is not available in v1` : `unknown format: ${format}`);
  }

  let out: string;
  try {
    out = renderCanonical(openContainer(path), format);
  } catch (e) {
    const error = e as RenderError;
    fail(error.code === undefined ? error.message : `${error.code}: ${error.message}`);
  }

  const file = args.values.get('-o') ?? args.values.get('--out');
  if (file === undefined) {
    process.stdout.write(out);
    return 0;
  }
  mkdirSync(dirname(file), { recursive: true });
  writeFileSync(file, out);
  process.stdout.write(`rendered -> ${file}\n`);
  return 0;
}

function cmdInspect(args: Args): number {
  const path = requirePositional(args, 'mcdf-ts inspect <container>');
  const container = openContainer(path);
  const doc = loadDocument(container);

  process.stdout.write(`container: ${path}\n`);
  process.stdout.write(`title:     ${doc.metadata.title || '(none)'}\n`);
  process.stdout.write(`version:   ${doc.metadata.version || '(none)'}\n`);
  process.stdout.write(`type:      ${doc.schema.document_type || '(none)'}\n`);
  process.stdout.write(`headings:  ${doc.headings.length}\n`);
  for (const h of doc.headings) {
    const anchor = h.id === '' ? '' : ` {#${h.id}}`;
    process.stdout.write(`  ${'  '.repeat(Math.max(0, h.level - 1))}${h.text}${anchor}\n`);
  }
  process.stdout.write('members:\n');
  for (const m of container.list()) process.stdout.write(`  ${m}\n`);
  return 0;
}

function cmdPack(args: Args): number {
  const [source, out] = args.positional;
  if (source === undefined || out === undefined) {
    fail('usage: mcdf-ts pack <directory> <out.mcdf>', 2);
  }
  const memory = toMemoryContainer(openContainer(source));
  mkdirSync(dirname(out), { recursive: true });
  writeFileSync(out, memory.toTar());
  process.stdout.write(`packed ${memory.list().length} members -> ${out}\n`);
  return 0;
}

function cmdUnpack(args: Args): number {
  const [source, out] = args.positional;
  if (source === undefined || out === undefined) {
    fail('usage: mcdf-ts unpack <in.mcdf> <directory>', 2);
  }
  const container = openContainer(source);
  if (!(container instanceof MemoryContainer)) fail(`not a .mcdf archive: ${source}`);
  for (const path of container.list()) {
    const target = join(out, path);
    mkdirSync(dirname(target), { recursive: true });
    writeFileSync(target, container.read(path));
  }
  process.stdout.write(`unpacked ${container.list().length} members -> ${out}\n`);
  return 0;
}

function cmdKeygen(args: Args): number {
  const usage = 'mcdf-ts keygen --out <key.pem> [--type ed25519|ecdsa-p256|x25519]';
  const out = requireValue(args, '--out', usage);
  const type = args.values.get('--type') ?? 'ed25519';

  let pem: string;
  let did: string;
  if (type === 'ed25519' || type === 'ecdsa-p256') {
    const algorithm: SignatureAlgorithm = type === 'ed25519' ? 'EdDSA' : 'ES256';
    const key = SigningKey.generate(algorithm);
    pem = key.toPkcs8Pem();
    did = key.didKey();
  } else if (type === 'x25519') {
    const key = EncPrivateKey.generate();
    pem = key.toPkcs8Pem();
    did = key.didKey();
  } else {
    return fail(`unknown key type '${type}' (ed25519|ecdsa-p256|x25519)`, 2);
  }

  mkdirSync(dirname(out) || '.', { recursive: true });
  // 0600: a private key readable by everyone on the box is not a private key.
  writeFileSync(out, pem, { mode: 0o600 });
  process.stdout.write(`${did}\n`);
  return 0;
}

function cmdSign(args: Args): number {
  const usage = 'mcdf-ts sign <container> --key <key.pem> [--name author]';
  const path = requirePositional(args, usage);
  const keyFile = requireValue(args, '--key', usage);
  const name = args.values.get('--name') ?? 'author';

  const container = openWritable(path);
  if (!container.has('manifest.json')) {
    return fail("manifest.json not found; run 'mcdf-ts manifest' first");
  }
  // Signing a container whose manifest is already stale would produce a
  // signature that is invalid the moment it is written.
  const manifest = parseManifestJson(container.readText('manifest.json'));
  if (!verifyManifest(container, manifest).ok) {
    return fail("manifest does not match content; re-run 'mcdf-ts manifest'");
  }

  const key = SigningKey.fromPkcs8Pem(readTextFile(keyFile, 'key'));
  const target = signaturePath(name);
  container.writeText(target, signContainer(container, key));

  process.stdout.write(`signed by ${key.didKey()}\n  -> ${target}\n`);
  return 0;
}

function cmdVerify(args: Args): number {
  const path = requirePositional(args, 'mcdf-ts verify <container>');
  const container = openContainer(path);
  let ok = true;

  if (!container.has('manifest.json')) {
    process.stdout.write('manifest: MISSING\n');
    ok = false;
  } else {
    const manifest = parseManifestJson(container.readText('manifest.json'));
    const integrity = verifyManifest(container, manifest);
    if (integrity.ok) {
      process.stdout.write(`manifest: OK (${Object.keys(manifest.files).length} files)\n`);
    } else {
      process.stdout.write('manifest: FAILED\n');
      ok = false;
      for (const p of integrity.mismatched) process.stdout.write(`  mismatch: ${p}\n`);
      for (const p of integrity.missing) process.stdout.write(`  missing:  ${p}\n`);
      for (const p of integrity.extra) process.stdout.write(`  extra:    ${p}\n`);
    }
  }

  if (container.has('manifest.json')) {
    const checks = verifyContainer(container);
    if (checks.length === 0) {
      process.stdout.write('signatures: NONE\n');
      ok = false;
    } else {
      for (const c of checks) {
        if (c.valid) {
          process.stdout.write(`signature ${c.file}: VALID (${c.alg}, ${c.kid})\n`);
        } else {
          process.stdout.write(`signature ${c.file}: INVALID${c.error === '' ? '' : ` (${c.error})`}\n`);
          ok = false;
        }
      }
    }
  }

  process.stdout.write(ok ? 'verify: OK\n' : 'verify: FAILED\n');
  return ok ? 0 : 1;
}

async function cmdEncrypt(args: Args): Promise<number> {
  const usage = 'mcdf-ts encrypt <container> --recipient <did:key> [--file <path>]';
  const path = requirePositional(args, usage);
  const dids = args.repeated.get('--recipient') ?? [];
  if (dids.length === 0) fail(`--recipient is required\nusage: ${usage}`, 2);
  const files = args.repeated.get('--file') ?? ['content.md'];

  const container = openWritable(path);
  const recipients = dids.map((did) => EncPublicKey.fromDidKey(did));
  await encryptContainer(container, files, recipients);

  process.stdout.write(`encrypted ${files.length} file(s) for ${recipients.length} recipient(s)\n`);
  return 0;
}

async function cmdDecrypt(args: Args): Promise<number> {
  const usage = 'mcdf-ts decrypt <container> --key <x25519.pem>';
  const path = requirePositional(args, usage);
  const keyFile = requireValue(args, '--key', usage);

  const container = openWritable(path);
  const key = EncPrivateKey.fromPkcs8Pem(readTextFile(keyFile, 'key'));
  const files = await decryptContainer(container, key);

  process.stdout.write(`decrypted ${files.length} file(s)\n`);
  return 0;
}

function cmdAudit(args: Args): number {
  const usage =
    'mcdf-ts audit <container> [--append <ACTION> --actor <name>] [--checkpoint --key <key.pem>]';
  const path = requirePositional(args, usage);
  const actor = args.values.get('--actor') ?? 'unknown';

  const action = args.values.get('--append');
  if (action !== undefined) {
    auditAppend(openWritable(path), action, actor, nowRfc3339());
    process.stdout.write(`appended: ${action} by ${actor}\n`);
    return 0;
  }

  if (args.switches.has('--checkpoint')) {
    const keyFile = requireValue(args, '--key', `${usage}\n--checkpoint requires --key`);
    const key = SigningKey.fromPkcs8Pem(readTextFile(keyFile, 'key'));
    auditCheckpoint(openWritable(path), key);
    process.stdout.write(`checkpoint written (head signed by ${key.didKey()})\n`);
    return 0;
  }

  const container = openContainer(path);
  for (const e of readAuditLog(container)) {
    process.stdout.write(`  ${e.timestamp}  ${e.action}  ${e.actor}\n`);
  }
  const v = auditVerify(container);
  process.stdout.write(`audit: ${v.ok ? 'OK' : 'FAILED'} (${v.entries} entries)`);
  if (!v.ok) process.stdout.write(` - ${v.error}`);
  process.stdout.write('\n');

  const checkpoint = auditVerifyCheckpoint(container);
  if (checkpoint.present) {
    process.stdout.write(
      `checkpoint: ${checkpoint.valid ? 'VALID' : 'INVALID'}` +
        `${checkpoint.kid === '' ? '' : ` (${checkpoint.kid})`}` +
        `${checkpoint.valid || checkpoint.error === '' ? '' : ` - ${checkpoint.error}`}\n`,
    );
  }
  return v.ok && (!checkpoint.present || checkpoint.valid) ? 0 : 1;
}

export async function main(argv: string[]): Promise<number> {
  const [verb, ...rest] = argv;
  const args = parseArgs(rest);

  switch (verb) {
    case undefined:
    case '-h':
    case '--help':
    case 'help':
      process.stdout.write(USAGE);
      return verb === undefined ? 2 : 0;
    case '-v':
    case '--version':
      process.stdout.write(`${VERSION}\n`);
      return 0;
    case 'validate':
      return cmdValidate(args);
    case 'manifest':
      return cmdManifest(args);
    case 'inspect':
      return cmdInspect(args);
    case 'pack':
      return cmdPack(args);
    case 'unpack':
      return cmdUnpack(args);
    case 'keygen':
      return cmdKeygen(args);
    case 'sign':
      return cmdSign(args);
    case 'verify':
      return cmdVerify(args);
    case 'encrypt':
      return await cmdEncrypt(args);
    case 'decrypt':
      return await cmdDecrypt(args);
    case 'render':
      return cmdRender(parseArgs(rest));
    case 'audit':
      return cmdAudit(args);
    default:
      process.stderr.write(`unknown command: ${verb}\n\n${USAGE}`);
      return 2;
  }
}

export function run(argv: string[]): void {
  main(argv).then(
    (code) => {
      process.exitCode = code;
    },
    (e: unknown) => fail((e as Error).message),
  );
}
