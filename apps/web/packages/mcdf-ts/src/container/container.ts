// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// A container is a flat, path-keyed bag of members — a directory on disk, a
// USTAR archive, or (here) an in-memory map. Everything above this layer works
// against the interface, exactly as the C++ side does.

import { compareUtf8, utf8Decode, utf8Encode } from '../util/bytes.js';
import { tarRead, tarWrite, type TarMember } from './tar.js';

export class ContainerError extends Error {
  /** Normative MCDF error code, when one applies. */
  readonly code: string | undefined;

  constructor(message: string, code?: string) {
    super(message);
    this.name = 'ContainerError';
    this.code = code;
  }
}

/**
 * Rejects member paths that could escape the container when written to a real
 * filesystem: absolute paths, drive letters, and any `..` segment.
 *
 * Raises `E_ASSET_PATH_ESCAPE`.
 */
export function checkMemberPath(path: string): void {
  if (path === '') {
    throw new ContainerError('empty member path', 'E_ASSET_PATH_ESCAPE');
  }
  if (path.startsWith('/') || path.startsWith('\\')) {
    throw new ContainerError(
      `absolute member path is not allowed: ${path}`,
      'E_ASSET_PATH_ESCAPE',
    );
  }
  if (/^[A-Za-z]:/.test(path)) {
    throw new ContainerError(
      `drive-qualified member path is not allowed: ${path}`,
      'E_ASSET_PATH_ESCAPE',
    );
  }
  for (const part of path.split(/[/\\]/)) {
    if (part === '..') {
      throw new ContainerError(
        `'..' not allowed in member path: ${path}`,
        'E_ASSET_PATH_ESCAPE',
      );
    }
  }
}

export interface Container {
  /** Member paths, in UTF-8 byte order. */
  list(): string[];
  has(path: string): boolean;
  /** Raw bytes of a member. Throws `ContainerError` when absent. */
  read(path: string): Uint8Array;
  /** Convenience: the member decoded as UTF-8 text. */
  readText(path: string): string;
}

/**
 * A container that can be modified in place.
 *
 * Operations that rewrite members — encrypt, decrypt, appending to the audit
 * log — take this rather than `Container`, so a read-only view cannot be handed
 * to them by mistake. Both `MemoryContainer` and the Node `DirectoryContainer`
 * implement it; the reference implementation draws the same line by taking a
 * `DirectoryContainer` for exactly these operations.
 */
export interface WritableContainer extends Container {
  write(path: string, data: Uint8Array): void;
  writeText(path: string, text: string): void;
  /** Appends to a member, creating it when absent. */
  append(path: string, text: string): void;
  remove(path: string): boolean;
}

/**
 * A fully in-memory container. This is the browser's working copy of a
 * document: opening a `.mcdf` unpacks into one, edits mutate it, and saving
 * packs it back out deterministically.
 */
export class MemoryContainer implements WritableContainer {
  #members = new Map<string, Uint8Array>();

  static fromEntries(entries: Iterable<TarMember>): MemoryContainer {
    const c = new MemoryContainer();
    for (const { path, data } of entries) c.write(path, data);
    return c;
  }

  /** Unpacks a `.mcdf` USTAR archive. */
  static fromTar(archive: Uint8Array): MemoryContainer {
    return MemoryContainer.fromEntries(tarRead(archive));
  }

  list(): string[] {
    return [...this.#members.keys()].sort(compareUtf8);
  }

  has(path: string): boolean {
    return this.#members.has(path);
  }

  read(path: string): Uint8Array {
    const data = this.#members.get(path);
    if (data === undefined) {
      throw new ContainerError(`no such member: ${path}`);
    }
    return data;
  }

  readText(path: string): string {
    return utf8Decode(this.read(path));
  }

  write(path: string, data: Uint8Array): void {
    checkMemberPath(path);
    this.#members.set(path, data);
  }

  writeText(path: string, text: string): void {
    this.write(path, utf8Encode(text));
  }

  append(path: string, text: string): void {
    const addition = utf8Encode(text);
    const existing = this.#members.get(path);
    if (existing === undefined) {
      this.write(path, addition);
      return;
    }
    const combined = new Uint8Array(existing.length + addition.length);
    combined.set(existing, 0);
    combined.set(addition, existing.length);
    this.write(path, combined);
  }

  remove(path: string): boolean {
    return this.#members.delete(path);
  }

  /** Packs the container into the deterministic `.mcdf` single-file form. */
  toTar(): Uint8Array {
    return tarWrite(this.list().map((path) => ({ path, data: this.read(path) })));
  }

  clone(): MemoryContainer {
    const c = new MemoryContainer();
    for (const path of this.list()) c.write(path, this.read(path).slice());
    return c;
  }
}
