// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Service-worker registration.
//
// The update policy is the interesting part. A worker that calls `skipWaiting`
// on install swaps the running app's modules out from under it — in an editor
// holding an unsaved document, that is data loss dressed up as a feature. So a
// new version waits, the app surfaces a notice, and the user chooses when.

export interface ServiceWorkerStatus {
  /** A newer version is installed and waiting for permission to take over. */
  updateReady: boolean;
  /** Applies the waiting version and reloads. */
  applyUpdate: () => void;
}

type Listener = (status: { updateReady: boolean }) => void;

let waiting: ServiceWorker | null = null;
let reloading = false;

function apply(): void {
  if (waiting === null) return;
  // `controllerchange` fires once the waiting worker takes over; reloading
  // there (rather than immediately) means the new page is served by the new
  // worker rather than racing it.
  navigator.serviceWorker.addEventListener('controllerchange', () => {
    if (reloading) return;
    reloading = true;
    window.location.reload();
  });
  waiting.postMessage('SKIP_WAITING');
}

/**
 * Registers the worker and reports when an update is waiting.
 *
 * Safe to call unconditionally: it does nothing where service workers are
 * unavailable (no support, insecure context, some private modes), which is also
 * every case where offline support could not have worked anyway.
 */
export function registerServiceWorker(onStatus: Listener): void {
  if (!('serviceWorker' in navigator) || !window.isSecureContext) return;
  // Vite's dev server serves a self-unregistering worker; registering in dev
  // would otherwise leave a stale cache behind after the session.
  if (import.meta.env.DEV) return;

  const url = `${import.meta.env.BASE_URL}sw.js`;

  void navigator.serviceWorker
    .register(url, { scope: import.meta.env.BASE_URL })
    .then((registration) => {
      const track = (worker: ServiceWorker | null): void => {
        if (worker === null) return;
        const check = (): void => {
          // "installed" with an existing controller means an update; without
          // one it is the very first install, which is not worth a notice.
          if (worker.state === 'installed' && navigator.serviceWorker.controller !== null) {
            waiting = worker;
            onStatus({ updateReady: true });
          }
        };
        check();
        worker.addEventListener('statechange', check);
      };

      track(registration.waiting);
      registration.addEventListener('updatefound', () => track(registration.installing));
    })
    .catch(() => {
      // An unavailable offline cache is not worth interrupting anyone over —
      // the app works, it just needs the network next time.
    });
}

export function applyServiceWorkerUpdate(): void {
  apply();
}
