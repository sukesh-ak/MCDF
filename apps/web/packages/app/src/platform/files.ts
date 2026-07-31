// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Open/save. The File System Access API gives a real "Save" (write back to the
// same file) but is Chromium-only, so the `<input type=file>` + download
// fallback is mandatory rather than optional. Nothing is
// uploaded anywhere: every byte stays in the tab.
//
// The fallback covers two distinct situations, and conflating them is a bug:
//   1. the API is absent (Firefox, Safari), and
//   2. the API is present but the platform refuses the request.
// Case 2 is common and easy to miss — Chromium throws NotAllowedError from
// `getFile()`/`createWritable()` when the site's file-editing permission is
// blocked, when the chosen path is on its blocklist (drive roots, system
// directories), or when a policy disallows it. Presence of the API is not
// permission to use it, so every call site degrades to the fallback instead of
// surfacing a DOMException the user can do nothing about.

export interface OpenedFile {
  name: string;
  bytes: Uint8Array;
  /** Present only when the File System Access API was used (enables Save). */
  handle: FileSystemFileHandle | null;
  /** Set when the picker API failed and the input fallback was used instead. */
  fellBackBecause?: string;
}

export interface SaveResult {
  /** Non-null when the file can be written back to directly next time. */
  handle: FileSystemFileHandle | null;
  cancelled: boolean;
  /** Set when the picker API failed and the file was downloaded instead. */
  fellBackBecause?: string;
}

interface PickerType {
  description: string;
  accept: Record<string, string[]>;
}

interface OpenPickerOptions {
  types?: PickerType[];
  multiple?: boolean;
  excludeAcceptAllOption?: boolean;
}

interface SavePickerOptions extends OpenPickerOptions {
  suggestedName?: string;
}

type PermissionMode = 'read' | 'readwrite';

/** The permission methods are part of the FSA spec but absent from lib.dom. */
type PermissionCapableHandle = FileSystemFileHandle & {
  queryPermission?: (options: { mode: PermissionMode }) => Promise<PermissionState>;
  requestPermission?: (options: { mode: PermissionMode }) => Promise<PermissionState>;
};

type PickerWindow = Window & {
  showOpenFilePicker?: (options?: OpenPickerOptions) => Promise<FileSystemFileHandle[]>;
  showSaveFilePicker?: (options?: SavePickerOptions) => Promise<FileSystemFileHandle>;
};

const MCDF_TYPE: PickerType = {
  description: 'MCDF document',
  accept: { 'application/x-mcdf': ['.mcdf'] },
};

export function hasFileSystemAccess(): boolean {
  return typeof window !== 'undefined' && 'showSaveFilePicker' in window;
}

/** True when the page can use WebCrypto — signing needs a secure context. */
export function isSecureContextAvailable(): boolean {
  return typeof window !== 'undefined' && window.isSecureContext;
}

function isAbort(e: unknown): boolean {
  return (e as DOMException | undefined)?.name === 'AbortError';
}

/**
 * Turns a DOMException into something a person can act on. The raw text —
 * "The request is not allowed by the user agent or the platform in the current
 * context" — is accurate and useless.
 */
function explain(e: unknown): string {
  const name = (e as DOMException | undefined)?.name;
  if (name === 'NotAllowedError' || name === 'SecurityError') {
    return (
      'this browser blocked direct file access. That is usually the file-editing ' +
      'site permission being denied, or a file in a location the browser protects ' +
      '(a drive root or a system folder)'
    );
  }
  if (name === 'NotFoundError') return 'the file could no longer be found';
  return (e as Error | undefined)?.message ?? 'the browser refused the request';
}

/**
 * Makes sure we hold the permission before using a handle. Chromium grants read
 * access implicitly when the user picks a file, but a handle can lose it later,
 * and asking is the difference between a prompt and an exception.
 */
async function ensurePermission(
  handle: FileSystemFileHandle,
  mode: PermissionMode,
): Promise<boolean> {
  const h = handle as PermissionCapableHandle;
  if (h.queryPermission === undefined || h.requestPermission === undefined) return true;
  if ((await h.queryPermission({ mode })) === 'granted') return true;
  return (await h.requestPermission({ mode })) === 'granted';
}

async function readHandle(handle: FileSystemFileHandle): Promise<OpenedFile> {
  const file = await handle.getFile();
  return { name: file.name, bytes: new Uint8Array(await file.arrayBuffer()), handle };
}

export async function readBlob(file: File): Promise<OpenedFile> {
  return { name: file.name, bytes: new Uint8Array(await file.arrayBuffer()), handle: null };
}

/**
 * Opens a document. Resolves to `null` when the user cancels — a cancelled
 * picker is a normal outcome, not an error to report.
 */
export async function openDocument(): Promise<OpenedFile | null> {
  const w = window as PickerWindow;
  if (w.showOpenFilePicker === undefined) return openViaInput();

  let handle: FileSystemFileHandle | undefined;
  try {
    [handle] = await w.showOpenFilePicker({ types: [MCDF_TYPE], multiple: false });
  } catch (e) {
    if (isAbort(e)) return null;
    // The picker itself was refused — go straight to the input element.
    return withFallbackReason(await openViaInput(), explain(e));
  }
  if (handle === undefined) return null;

  try {
    if (!(await ensurePermission(handle, 'read'))) {
      return withFallbackReason(await openViaInput(), 'read permission was declined');
    }
    return await readHandle(handle);
  } catch (e) {
    if (isAbort(e)) return null;
    // The handle is unusable (blocked path, revoked permission, policy). The
    // user still gets their file; they just lose in-place Save for it.
    return withFallbackReason(await openViaInput(), explain(e));
  }
}

function withFallbackReason(file: OpenedFile | null, reason: string): OpenedFile | null {
  return file === null ? null : { ...file, fellBackBecause: reason };
}

/** `<input type=file>` fallback: works everywhere, gives no writable handle. */
export function openViaInput(accept = '.mcdf'): Promise<OpenedFile | null> {
  return new Promise((resolve) => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = accept;
    input.style.display = 'none';
    // Fires when the dialog closes with no selection in browsers that support
    // it; `change` covers the rest.
    input.addEventListener('cancel', () => {
      input.remove();
      resolve(null);
    });
    input.addEventListener('change', () => {
      const file = input.files?.[0];
      input.remove();
      if (file === undefined) {
        resolve(null);
        return;
      }
      void readBlob(file).then(resolve);
    });
    document.body.append(input);
    input.click();
  });
}

/**
 * Writes back to an already-opened file. Returns false when the platform
 * refuses, so the caller can fall back to Save-as rather than lose the edit.
 */
export async function writeHandle(
  handle: FileSystemFileHandle,
  bytes: Uint8Array,
): Promise<boolean> {
  try {
    if (!(await ensurePermission(handle, 'readwrite'))) return false;
    const writable = await handle.createWritable();
    await writable.write(bytes as unknown as BufferSource);
    await writable.close();
    return true;
  } catch (e) {
    if (isAbort(e)) return false;
    return false;
  }
}

/** Saves under a new name, downloading instead if the picker is unavailable. */
export async function saveDocumentAs(
  bytes: Uint8Array,
  suggestedName: string,
): Promise<SaveResult> {
  const w = window as PickerWindow;
  if (w.showSaveFilePicker !== undefined) {
    try {
      const handle = await w.showSaveFilePicker({ suggestedName, types: [MCDF_TYPE] });
      if (await writeHandle(handle, bytes)) return { handle, cancelled: false };
      download(bytes, suggestedName, 'application/x-mcdf');
      return { handle: null, cancelled: false, fellBackBecause: 'the file could not be written' };
    } catch (e) {
      if (isAbort(e)) return { handle: null, cancelled: true };
      download(bytes, suggestedName, 'application/x-mcdf');
      return { handle: null, cancelled: false, fellBackBecause: explain(e) };
    }
  }
  download(bytes, suggestedName, 'application/x-mcdf');
  return { handle: null, cancelled: false };
}

export function download(bytes: Uint8Array, name: string, type: string): void {
  const blob = new Blob([bytes as unknown as BlobPart], { type });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = name;
  a.style.display = 'none';
  document.body.append(a);
  a.click();
  a.remove();
  // Revoking immediately can race the download in some browsers.
  setTimeout(() => URL.revokeObjectURL(url), 10_000);
}
