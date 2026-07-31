// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// Screen-reader announcements. Status that is only conveyed visually — a
// signature breaking, the manifest going stale — has to reach assistive
// technology too: the tamper demo is *audible*, not just visible.

export type Politeness = 'polite' | 'assertive';

export class Announcer {
  polite = $state('');
  assertive = $state('');

  /**
   * A repeated identical string is not re-announced by screen readers, so a
   * counter is appended out of view to force a fresh announcement.
   */
  #tick = 0;

  say(message: string, politeness: Politeness = 'polite'): void {
    this.#tick++;
    // Alternate an invisible zero-width space so the text always differs.
    const text = this.#tick % 2 === 0 ? message : `${message}​`;
    if (politeness === 'assertive') {
      this.assertive = text;
    } else {
      this.polite = text;
    }
  }

  clear(): void {
    this.polite = '';
    this.assertive = '';
  }
}

export const announcer = new Announcer();
