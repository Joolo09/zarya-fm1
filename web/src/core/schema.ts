// SPDX-License-Identifier: GPL-3.0-only
// Data model shared with the firmware. Names, ranges and labels come from
// schema.json, which is generated from the firmware's own parameter tables
// (make schema) — the editor never invents a parameter.
import raw from './schema.json';

export interface ParamDesc { id: number; name: string; min: number; max: number; def: number; fmt: number; labels: string[] }
export interface Schema {
  schema: number; protocol: number; version: string; tracks: number; steps: number; scenes: number;
  patchParams: number; globals: number; opBase: number; opFields: number; trackBase: number; trackFields: number;
  patch: ParamDesc[]; kit: ParamDesc[]; global: ParamDesc[];
  scales: { name: string; mask: number }[]; chords: string[]; drums: string[]; categories: string[]; gateEighths: number[];
}
export const schema = raw as Schema;

export const NAME_LEN = 12;
export const PATCH_BYTES = 4 + NAME_LEN + schema.patchParams; // 100
export const PATTERN_BYTES = schema.steps * 4 + 4;            // 260
export const DRUM_TRACK = 3;
export const BLOB = { PATCH: 1, PATTERN: 2, GLOBALS: 3 } as const;
export const BLOB_HEADER = 8;

export interface Patch { type: number; category: number; name: string; p: number[] }
export interface Step { note: number; vel: number; gate: number; fx: number }
export interface Pattern { steps: Step[]; length: number }

export const isKit = (track: number) => track === DRUM_TRACK;
export function patchDesc(track: number, id: number): ParamDesc | undefined {
  return isKit(track) ? schema.kit[id] : schema.patch[id];
}
export const opParam = (op: number, field: number) => schema.opBase + op * schema.opFields + field;
export const trackGlobal = (track: number, field: number) => schema.trackBase + track * schema.trackFields + field;
export function globalId(name: string): number {
  const d = schema.global.find((g) => g.name === name);
  if (!d) throw new Error(`unknown global ${name}`);
  return d.id;
}
export function patchId(name: string): number {
  const d = schema.patch.find((g, i) => g.name === name && i < schema.opBase);
  if (!d) throw new Error(`unknown patch parameter ${name}`);
  return d.id;
}
export const clampParam = (d: ParamDesc, v: number) => Math.min(d.max, Math.max(d.min, Math.round(v)));
export const label = (d: ParamDesc, v: number) => d.labels[clampParam(d, v) - d.min] ?? String(v);

export function crc16(data: Uint8Array, crc = 0xffff): number {
  for (const b of data) {
    crc ^= b << 8;
    for (let i = 0; i < 8; i++) crc = crc & 0x8000 ? ((crc << 1) ^ 0x1021) & 0xffff : (crc << 1) & 0xffff;
  }
  return crc;
}
export function blobPack(kind: number, payload: Uint8Array): Uint8Array {
  const out = new Uint8Array(BLOB_HEADER + payload.length);
  const crc = crc16(payload);
  out.set([0x5a, 0x52, kind, schema.schema, payload.length & 255, payload.length >> 8, crc & 255, crc >> 8]);
  out.set(payload, BLOB_HEADER);
  return out;
}
/** Validates a blob and returns its payload. Throws with a precise reason. */
export function blobUnpack(blob: Uint8Array, kind: number, expectLen?: number): Uint8Array {
  if (blob.length < BLOB_HEADER) throw new Error('blob: truncated header');
  if (blob[0] !== 0x5a || blob[1] !== 0x52) throw new Error('blob: bad magic');
  if (blob[2] !== kind) throw new Error(`blob: kind ${blob[2]}, expected ${kind}`);
  if (blob[3] !== schema.schema) throw new Error(`blob: schema ${blob[3]}, this editor supports ${schema.schema}`);
  const len = blob[4] | (blob[5] << 8);
  if (len !== blob.length - BLOB_HEADER || (expectLen !== undefined && len !== expectLen)) throw new Error('blob: bad length');
  const payload = blob.subarray(BLOB_HEADER);
  if (crc16(payload) !== (blob[6] | (blob[7] << 8))) throw new Error('blob: checksum mismatch');
  return payload;
}

export function decodePatch(b: Uint8Array): Patch {
  if (b.length !== PATCH_BYTES) throw new Error('patch: bad size');
  let name = '';
  for (let i = 0; i < NAME_LEN; i++) name += String.fromCharCode(b[4 + i] >= 32 && b[4 + i] < 127 ? b[4 + i] : 32);
  return { type: b[0], category: b[1], name: name.trimEnd(), p: Array.from(b.subarray(4 + NAME_LEN)) };
}
export function encodePatch(p: Patch): Uint8Array {
  const b = new Uint8Array(PATCH_BYTES);
  b[0] = p.type; b[1] = p.category & 7;
  const name = p.name.toUpperCase().padEnd(NAME_LEN).slice(0, NAME_LEN);
  for (let i = 0; i < NAME_LEN; i++) { const c = name.charCodeAt(i); b[4 + i] = c >= 32 && c < 127 ? c : 32; }
  const descs = p.type === 1 ? schema.kit : schema.patch;
  for (let i = 0; i < schema.patchParams; i++) b[4 + NAME_LEN + i] = descs[i] ? clampParam(descs[i], p.p[i] ?? descs[i].def) : 0;
  return b;
}
export function decodePattern(b: Uint8Array): Pattern {
  if (b.length !== PATTERN_BYTES) throw new Error('pattern: bad size');
  const steps: Step[] = [];
  for (let i = 0; i < schema.steps; i++) steps.push({ note: b[i * 4], vel: b[i * 4 + 1], gate: b[i * 4 + 2], fx: b[i * 4 + 3] });
  const length = b[schema.steps * 4];
  return { steps, length: length >= 1 && length <= schema.steps ? length : 16 };
}
export function encodePattern(p: Pattern): Uint8Array {
  const b = new Uint8Array(PATTERN_BYTES);
  p.steps.slice(0, schema.steps).forEach((s, i) => b.set([s.note & 255, s.vel & 127, s.gate & 0x3f, s.fx & 0x3f], i * 4));
  b[schema.steps * 4] = Math.min(schema.steps, Math.max(1, p.length));
  return b;
}
export function sanitizeGlobals(g: number[]): number[] {
  return Array.from({ length: schema.globals }, (_, i) => (schema.global[i] ? clampParam(schema.global[i], g[i] ?? schema.global[i].def) : 0));
}
export const emptyPattern = (): Pattern => ({ steps: Array.from({ length: schema.steps }, () => ({ note: 0, vel: 0, gate: 0, fx: 0 })), length: 16 });
export const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
export const noteName = (n: number) => `${NOTE_NAMES[n % 12]}${Math.floor(n / 12) - 1}`;
