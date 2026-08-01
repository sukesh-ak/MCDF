// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2026 Sukesh Ashok Kumar and The MCDF Project
//
// The handful of DOM shapes the converter touches, declared structurally.
//
// This package compiles with `lib: ["ES2022"]` and no DOM types, and that
// absence is not an oversight — it is what enforces the promise that the same
// build runs in Node, a browser and a worker. Adding `"DOM"` to satisfy four
// method signatures would switch the enforcement off everywhere, for every
// module, permanently.
//
// So the nodes turndown hands to a replacement rule are typed by what is
// actually called on them. Both implementations satisfy this: the browser's own
// DOM, and the `@mixmark-io/domino` document turndown builds under Node.

export interface DomNode {
  readonly nodeName: string;
  readonly textContent: string | null;
}

export interface DomElement extends DomNode {
  getAttribute(name: string): string | null | undefined;
  /**
   * Returns `null` in a browser and `undefined` in domino when nothing matches.
   *
   * Checked, not assumed — and it is why `first()` exists. Writing the obvious
   * `el.querySelector('table') !== null` produces code that is correct in the
   * browser and wrong under Node, where every miss reads as a hit. It cost one
   * silently flattened table to find.
   */
  querySelector(selectors: string): DomElement | null | undefined;
  querySelectorAll(selectors: string): ArrayLike<DomElement>;
}

/** Normalises a `querySelector` result across the two implementations. */
export function first(match: DomElement | null | undefined): DomElement | null {
  return match ?? null;
}

/** Normalises a possibly-absent attribute to a plain string. */
export function attr(element: DomElement, name: string): string {
  return element.getAttribute(name) ?? '';
}

/**
 * Materialises a node list.
 *
 * By index rather than by iteration: a browser `NodeList` is iterable, domino's
 * is not (verified — spreading one yields nothing under Node), and the
 * intersection of the two is `length` plus indexing.
 */
export function toArray(list: ArrayLike<DomElement>): DomElement[] {
  const out: DomElement[] = [];
  for (let i = 0; i < list.length; i++) {
    const item = list[i];
    if (item !== undefined && item !== null) out.push(item);
  }
  return out;
}
