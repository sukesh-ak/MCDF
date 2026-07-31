// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Node-only filesystem bindings. Deliberately kept out of `index.ts` so the
// browser bundle never pulls `node:fs` in — the core stays platform-free and
// this file is the one place that is not.

import {
  readFileSync,
  readdirSync,
  statSync,
  mkdirSync,
  writeFileSync,
  appendFileSync,
  rmSync,
} from 'node:fs';
import { join, dirname, relative, sep } from 'node:path';

import {
  ContainerError,
  MemoryContainer,
  checkMemberPath,
  type Container,
  type WritableContainer,
} from '../container/container.js';
import { compareUtf8, utf8Decode, utf8Encode } from '../util/bytes.js';

function toMemberPath(p: string): string {
  return sep === '/' ? p : p.split(sep).join('/');
}

function walk(root: string, dir: string, out: string[]): void {
  for (const entry of readdirSync(dir, { withFileTypes: true })) {
    const full = join(dir, entry.name);
    if (entry.isDirectory()) {
      walk(root, full, out);
    } else if (entry.isFile()) {
      out.push(toMemberPath(relative(root, full)));
    }
  }
}

/** A container backed by a directory on disk — the unpacked `.mcdf` form. */
export class DirectoryContainer implements WritableContainer {
  readonly root: string;

  constructor(root: string) {
    this.root = root;
  }

  #resolve(path: string): string {
    checkMemberPath(path);
    return join(this.root, path);
  }

  list(): string[] {
    const out: string[] = [];
    walk(this.root, this.root, out);
    return out.sort(compareUtf8);
  }

  has(path: string): boolean {
    try {
      return statSync(this.#resolve(path)).isFile();
    } catch {
      return false;
    }
  }

  read(path: string): Uint8Array {
    const resolved = this.#resolve(path);
    try {
      return new Uint8Array(readFileSync(resolved));
    } catch {
      throw new ContainerError(`member not found: ${path}`);
    }
  }

  readText(path: string): string {
    return utf8Decode(this.read(path));
  }

  write(path: string, data: Uint8Array): void {
    const resolved = this.#resolve(path);
    mkdirSync(dirname(resolved), { recursive: true });
    writeFileSync(resolved, data);
  }

  writeText(path: string, text: string): void {
    this.write(path, utf8Encode(text));
  }

  append(path: string, text: string): void {
    const resolved = this.#resolve(path);
    mkdirSync(dirname(resolved), { recursive: true });
    appendFileSync(resolved, utf8Encode(text));
  }

  remove(path: string): boolean {
    if (!this.has(path)) return false;
    rmSync(this.#resolve(path));
    return true;
  }
}

/**
 * Opens a container from a path: a directory becomes a `DirectoryContainer`, a
 * regular file is unpacked from USTAR into memory.
 */
export function openContainer(path: string): Container {
  let stat;
  try {
    stat = statSync(path);
  } catch {
    throw new ContainerError(`no such container: ${path}`);
  }
  if (stat.isDirectory()) return new DirectoryContainer(path);
  if (stat.isFile()) return MemoryContainer.fromTar(new Uint8Array(readFileSync(path)));
  throw new ContainerError(`no such container: ${path}`);
}

/** Copies a container's members into memory (so they can be edited/packed). */
export function toMemoryContainer(container: Container): MemoryContainer {
  if (container instanceof MemoryContainer) return container;
  const c = new MemoryContainer();
  for (const path of container.list()) c.write(path, container.read(path));
  return c;
}
