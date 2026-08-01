// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// A minimal, read-only ZIP reader — enough to ingest a foreign EPUB, and
// deliberately no more.
//
// In-house rather than a dependency, for the same reason the TAR writer and the
// reference implementation's `container/zip.hpp` are: the decompression is the
// only hard part and the platform already has it. `DecompressionStream` is in
// every current browser and in Node 18+, so `deflate` costs nothing and
// `inflate` never had to be written twice.
//
// This never reads an MCDF container. ZIP is not an MCDF container form — it
// was removed as one precisely because two parsers can disagree about the
// contents of a single file (spec §3), and that is exactly the property this
// reader has to work around below rather than the one it can rely on.

const EOCD_SIGNATURE = 0x06054b50;
const CENTRAL_SIGNATURE = 0x02014b50;
const LOCAL_SIGNATURE = 0x04034b50;

const EOCD_MIN_SIZE = 22;
const CENTRAL_MIN_SIZE = 46;
const LOCAL_MIN_SIZE = 30;

/** Sentinel meaning "the real value is in a ZIP64 extra field". */
const ZIP64_MARKER = 0xffffffff;

const METHOD_STORED = 0;
const METHOD_DEFLATE = 8;

export class ZipError extends Error {
  constructor(message: string) {
    super(message);
    this.name = 'ZipError';
  }
}

export interface ZipEntry {
  name: string;
  compressedSize: number;
  uncompressedSize: number;
  method: number;
  /** Offset of the local file header, from the start of the archive. */
  headerOffset: number;
}

export interface ZipReadLimits {
  /** Refuse any single member larger than this once decompressed. */
  maxMemberBytes?: number;
  /** Refuse an archive whose members sum to more than this once decompressed. */
  maxTotalBytes?: number;
}

const DEFAULT_LIMITS: Required<ZipReadLimits> = {
  maxMemberBytes: 64 * 1024 * 1024,
  maxTotalBytes: 256 * 1024 * 1024,
};

/**
 * A parsed ZIP central directory, with members read on demand.
 *
 * Untrusted input in a browser tab, so the declared sizes are treated as claims
 * rather than facts: a member is refused before it is inflated when the header
 * says it is too big, and the inflated bytes are counted as they arrive so a
 * lying header cannot exhaust memory either.
 */
export class ZipArchive {
  readonly #bytes: Uint8Array;
  readonly #entries: Map<string, ZipEntry>;
  readonly #limits: Required<ZipReadLimits>;
  #spent = 0;

  /** Names that appeared more than once; the first occurrence is the one kept. */
  readonly duplicates: readonly string[];

  private constructor(
    bytes: Uint8Array,
    entries: Map<string, ZipEntry>,
    duplicates: string[],
    limits: Required<ZipReadLimits>,
  ) {
    this.#bytes = bytes;
    this.#entries = entries;
    this.#limits = limits;
    this.duplicates = duplicates;
  }

  static open(bytes: Uint8Array, limits: ZipReadLimits = {}): ZipArchive {
    const merged = { ...DEFAULT_LIMITS, ...limits };
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const eocd = findEocd(view, bytes.byteLength);

    const total = view.getUint16(eocd + 10, true);
    const size = view.getUint32(eocd + 12, true);
    const offset = view.getUint32(eocd + 16, true);
    if (offset === ZIP64_MARKER || size === ZIP64_MARKER || total === 0xffff) {
      // Refused rather than guessed at. A ZIP64 archive is a different format
      // in the places that matter, and half-reading one would mean reading the
      // wrong bytes with full confidence.
      throw new ZipError('ZIP64 archives are not supported');
    }
    if (offset + size > bytes.byteLength) {
      throw new ZipError('ZIP central directory extends past the end of the archive');
    }

    const entries = new Map<string, ZipEntry>();
    const duplicates: string[] = [];
    let cursor = offset;

    for (let i = 0; i < total; i++) {
      if (cursor + CENTRAL_MIN_SIZE > bytes.byteLength) {
        throw new ZipError('truncated ZIP central directory');
      }
      if (view.getUint32(cursor, true) !== CENTRAL_SIGNATURE) {
        throw new ZipError('malformed ZIP central directory entry');
      }
      const method = view.getUint16(cursor + 10, true);
      const compressedSize = view.getUint32(cursor + 20, true);
      const uncompressedSize = view.getUint32(cursor + 24, true);
      const nameLen = view.getUint16(cursor + 28, true);
      const extraLen = view.getUint16(cursor + 30, true);
      const commentLen = view.getUint16(cursor + 32, true);
      const headerOffset = view.getUint32(cursor + 42, true);

      const nameStart = cursor + CENTRAL_MIN_SIZE;
      if (nameStart + nameLen > bytes.byteLength) {
        throw new ZipError('truncated ZIP entry name');
      }
      if (
        compressedSize === ZIP64_MARKER ||
        uncompressedSize === ZIP64_MARKER ||
        headerOffset === ZIP64_MARKER
      ) {
        throw new ZipError('ZIP64 archives are not supported');
      }

      // ZIP stores names as bytes; the spec allows CP437 or UTF-8 (flagged in
      // bit 11). EPUB requires UTF-8, and decoding a CP437 name as UTF-8 is
      // recoverable — an unmatched image reference — while the reverse is not.
      const name = new TextDecoder('utf-8').decode(bytes.subarray(nameStart, nameStart + nameLen));

      // Directory markers carry no data and only get in the way of lookups.
      if (!name.endsWith('/')) {
        if (entries.has(name)) {
          if (!duplicates.includes(name)) duplicates.push(name);
        } else {
          entries.set(name, {
            name,
            compressedSize,
            uncompressedSize,
            method,
            headerOffset,
          });
        }
      }
      cursor = nameStart + nameLen + extraLen + commentLen;
    }

    return new ZipArchive(bytes, entries, duplicates, merged);
  }

  list(): string[] {
    return [...this.#entries.keys()];
  }

  has(name: string): boolean {
    return this.#entries.has(name);
  }

  /** Bytes of a member, or null when the archive has no such entry. */
  async read(name: string): Promise<Uint8Array | null> {
    const entry = this.#entries.get(name);
    if (entry === undefined) return null;

    if (entry.uncompressedSize > this.#limits.maxMemberBytes) {
      throw new ZipError(
        `ZIP member ${name} declares ${entry.uncompressedSize} bytes, over the ` +
          `${this.#limits.maxMemberBytes} limit`,
      );
    }

    const bytes = this.#bytes;
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const header = entry.headerOffset;
    if (header + LOCAL_MIN_SIZE > bytes.byteLength) {
      throw new ZipError(`truncated local header for ${name}`);
    }
    if (view.getUint32(header, true) !== LOCAL_SIGNATURE) {
      throw new ZipError(`malformed local header for ${name}`);
    }
    // The local header's own name/extra lengths, not the central directory's:
    // the two are allowed to differ, and the data starts after the local ones.
    const nameLen = view.getUint16(header + 26, true);
    const extraLen = view.getUint16(header + 28, true);
    const start = header + LOCAL_MIN_SIZE + nameLen + extraLen;
    const end = start + entry.compressedSize;
    if (end > bytes.byteLength) {
      throw new ZipError(`ZIP member ${name} extends past the end of the archive`);
    }

    const raw = bytes.subarray(start, end);
    const out =
      entry.method === METHOD_STORED
        ? raw.slice()
        : entry.method === METHOD_DEFLATE
          ? await this.#inflate(raw, name)
          : null;
    if (out === null) {
      throw new ZipError(`ZIP member ${name} uses unsupported compression method ${entry.method}`);
    }

    this.#spend(out.byteLength, name);
    return out;
  }

  /** Convenience: a member decoded as UTF-8, or null when absent. */
  async readText(name: string): Promise<string | null> {
    const data = await this.read(name);
    return data === null ? null : new TextDecoder('utf-8').decode(data);
  }

  #spend(bytes: number, name: string): void {
    this.#spent += bytes;
    if (this.#spent > this.#limits.maxTotalBytes) {
      throw new ZipError(
        `ZIP archive expands past the ${this.#limits.maxTotalBytes} byte total limit ` +
          `while reading ${name}`,
      );
    }
  }

  /**
   * Raw DEFLATE via the platform.
   *
   * The output is counted as it streams rather than after it lands, so a member
   * whose header understates its size — the ordinary shape of a zip bomb —
   * is stopped mid-inflate instead of after it has already been allocated.
   */
  async #inflate(raw: Uint8Array, name: string): Promise<Uint8Array> {
    const limit = this.#limits.maxMemberBytes;
    let stream: ReadableStream<Uint8Array>;
    try {
      const source = new ReadableStream<Uint8Array>({
        start(controller) {
          controller.enqueue(raw);
          controller.close();
        },
      });
      stream = source.pipeThrough(
        new DecompressionStream('deflate-raw'),
      ) as ReadableStream<Uint8Array>;
    } catch (e) {
      throw new ZipError(
        `this environment cannot decompress ZIP members (${(e as Error).message})`,
      );
    }

    const reader = stream.getReader();
    const chunks: Uint8Array[] = [];
    let size = 0;
    try {
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        size += value.byteLength;
        if (size > limit) {
          throw new ZipError(`ZIP member ${name} inflates past the ${limit} byte limit`);
        }
        chunks.push(value);
      }
    } catch (e) {
      if (e instanceof ZipError) throw e;
      throw new ZipError(`ZIP member ${name} is not valid DEFLATE data`);
    } finally {
      reader.releaseLock();
    }

    const out = new Uint8Array(size);
    let at = 0;
    for (const chunk of chunks) {
      out.set(chunk, at);
      at += chunk.byteLength;
    }
    return out;
  }
}

/**
 * Locates the end-of-central-directory record.
 *
 * Scanned backwards because the record sits at the end but is followed by a
 * variable-length comment, and the comment may itself contain the signature —
 * so a match is only accepted when its declared comment length actually reaches
 * the end of the file.
 */
function findEocd(view: DataView, length: number): number {
  if (length < EOCD_MIN_SIZE) throw new ZipError('not a ZIP archive: too short');
  const earliest = Math.max(0, length - EOCD_MIN_SIZE - 0xffff);
  for (let at = length - EOCD_MIN_SIZE; at >= earliest; at--) {
    if (view.getUint32(at, true) !== EOCD_SIGNATURE) continue;
    const commentLen = view.getUint16(at + 20, true);
    if (at + EOCD_MIN_SIZE + commentLen === length) return at;
  }
  throw new ZipError('not a ZIP archive: no end-of-central-directory record');
}
