// SPDX-License-Identifier: GPL-3.0-only
// Sounds and phrases used by the live demos on the project site (and by the
// test that measures what the PURE demo claims).
import { opParam, patchId, schema, type Patch } from '../core/schema';

/** A plain sustained sawtooth: every harmonic is present, so mistuned
 *  intervals beat audibly — the clearest way to hear PURE. */
export function pureDemoPatch(base: Patch): Patch {
  const p: Patch = { type: 0, category: 3, name: 'PURE DEMO', p: schema.patch.map((d) => d.def) };
  const set = (name: string, v: number) => { p.p[patchId(name)] = v; };
  const op = (o: number, field: number, v: number) => { p.p[opParam(o, field)] = v; };
  void base;
  set('ALGO', 0); set('LEVEL', 92); set('FILTER', 1); set('CUTOFF', 96); set('RESO', 8); set('REV SEND', 30);
  op(0, 3, 127); op(0, 4, 2); op(0, 5, 20); op(0, 6, 70); op(0, 7, 127); op(0, 8, 52); op(0, 9, 30);
  for (let o = 1; o < 4; o++) op(o, 3, 0);
  return p;
}
/** C dominant seventh, close position plus a bass note. */
export const PURE_CHORD = [48, 60, 64, 67, 70];
/** The call for the DUET demo, in A minor: one note every two steps. */
export const DUET_PHRASE = [69, 72, 76, 74];
