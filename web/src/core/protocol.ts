// SPDX-License-Identifier: GPL-3.0-only
// ZARYA editor protocol client (docs/PROTOCOL.md). Only documented commands
// exist here; there is no firmware-update or persistent-write command.
import { schema } from './schema';

export const CMD = { HELLO: 1, GET_BLOB: 2, PUT_BLOB: 3, SET_PARAM: 4, TRANSPORT: 5, NOTE: 6, GET_STATUS: 7, GENERATE: 8, SELECT: 9, LIVE: 10 } as const;
export const RSP = { HELLO: 0x41, BLOB: 0x42, PUT_ACK: 0x43, STATUS: 0x47, GENERATED: 0x48, EVT_PARAM: 0x4a, EVT_PATCH: 0x4b, ERROR: 0x4f } as const;
export const CAP = { PARAM: 1, BLOB_GET: 2, BLOB_PUT: 4, TRANSPORT: 8, GENERATE: 16, PERSIST: 32, FLASH: 64, LIVE: 128 } as const;
export const FRAME_MAX = 128;
export const CHUNK = 96;

export function pack7(data: Uint8Array): number[] {
  const out: number[] = [];
  for (let i = 0; i < data.length; i += 7) {
    const n = Math.min(7, data.length - i);
    let msb = 0;
    for (let k = 0; k < n; k++) msb |= (data[i + k] >> 7) << k;
    out.push(msb);
    for (let k = 0; k < n; k++) out.push(data[i + k] & 0x7f);
  }
  return out;
}
export function unpack7(data: Uint8Array): Uint8Array {
  const out: number[] = [];
  for (let i = 0; i < data.length;) {
    const msb = data[i++];
    const n = Math.min(7, data.length - i);
    for (let k = 0; k < n; k++) out.push(data[i + k] | (((msb >> k) & 1) << 7));
    i += n;
  }
  return Uint8Array.from(out);
}
export function encodeFrame(cmd: number, seq: number, payload: ArrayLike<number> = []): Uint8Array {
  const body = pack7(Uint8Array.from(payload));
  const f = Uint8Array.from([0xf0, 0x7d, 0x5a, 0x52, cmd & 0x7f, seq & 0x7f, ...body, 0xf7]);
  if (f.length > FRAME_MAX) throw new Error(`frame too long: ${f.length}`);
  return f;
}
export interface Frame { cmd: number; seq: number; payload: Uint8Array }
/** Returns null for anything that is not a well-formed ZARYA frame. */
export function decodeFrame(f: Uint8Array): Frame | null {
  if (f.length < 7 || f.length > FRAME_MAX || f[0] !== 0xf0 || f[f.length - 1] !== 0xf7) return null;
  if (f[1] !== 0x7d || f[2] !== 0x5a || f[3] !== 0x52) return null;
  for (let i = 4; i < f.length - 1; i++) if (f[i] & 0x80) return null;
  return { cmd: f[4], seq: f[5], payload: unpack7(f.subarray(6, f.length - 1)) };
}

export interface Transport {
  readonly kind: 'sim' | 'midi';
  readonly label: string;
  send(frame: Uint8Array): void;
  onFrame: ((frame: Uint8Array) => void) | null;
  onClose: (() => void) | null;
  close(): void;
}
export interface Identity {
  protocol: number; firmware: string; schema: number; tracks: number; voices: number; steps: number; scenes: number;
  caps: number; storage: number; hardware: boolean;
}
export interface Status { playing: boolean; scene: number; step: number[]; voices: number; load: number; track: number; page: number; storage: number; recording: boolean; peak: number; degree: number; pureRoot: number; duet: boolean; captureSeq: number; captureCount: number }
export type DeviceEvent =
  | { type: 'param'; scope: number; track: number; id: number; value: number }
  | { type: 'patch'; track: number }
  | { type: 'error'; cmd: number; code: number };

interface Waiter { match: (f: Frame) => boolean; resolve: (f: Frame) => void; reject: (e: Error) => void; timer: ReturnType<typeof setTimeout> }

export class Client {
  identity: Identity | null = null;
  onEvent: ((e: DeviceEvent) => void) | null = null;
  log: ((dir: 'tx' | 'rx', f: Uint8Array) => void) | null = null;
  private seq = 1;
  private waiters: Waiter[] = [];
  private pendingParams = new Map<string, number[]>();
  private flushTimer: ReturnType<typeof setTimeout> | null = null;
  private chain: Promise<unknown> = Promise.resolve();
  closed = false;

  constructor(readonly transport: Transport, private timeoutMs = 1500) {
    transport.onFrame = (bytes) => this.receive(bytes);
    transport.onClose = () => this.fail(new Error('device disconnected'));
  }
  private receive(bytes: Uint8Array) {
    const f = decodeFrame(bytes);
    if (!f) return; // foreign or malformed SysEx: ignored, never interpreted
    this.log?.('rx', bytes);
    if (f.cmd === RSP.EVT_PARAM && f.payload.length >= 4) { this.onEvent?.({ type: 'param', scope: f.payload[0], track: f.payload[1], id: f.payload[2], value: f.payload[3] }); return; }
    if (f.cmd === RSP.EVT_PATCH && f.payload.length >= 1) { this.onEvent?.({ type: 'patch', track: f.payload[0] }); return; }
    const i = this.waiters.findIndex((w) => w.match(f));
    if (i >= 0) { const [w] = this.waiters.splice(i, 1); clearTimeout(w.timer); w.resolve(f); return; }
    if (f.cmd === RSP.ERROR && f.payload.length >= 2) this.onEvent?.({ type: 'error', cmd: f.payload[0], code: f.payload[1] });
  }
  private fail(e: Error) {
    this.closed = true;
    for (const w of this.waiters.splice(0)) { clearTimeout(w.timer); w.reject(e); }
    this.pendingParams.clear();
  }
  private tx(cmd: number, payload: ArrayLike<number>, seq = this.nextSeq()): number {
    if (this.closed) throw new Error('not connected');
    const f = encodeFrame(cmd, seq, payload);
    this.log?.('tx', f);
    this.transport.send(f);
    return seq;
  }
  private nextSeq() { this.seq = (this.seq % 126) + 1; return this.seq; }
  private wait(match: (f: Frame) => boolean): Promise<Frame> {
    return new Promise((resolve, reject) => {
      const w: Waiter = { match, resolve, reject, timer: setTimeout(() => { this.waiters = this.waiters.filter((x) => x !== w); reject(new Error('device did not answer (timeout)')); }, this.timeoutMs) };
      this.waiters.push(w);
    });
  }
  /** One request/response exchange at a time, in order. */
  private exchange<T>(fn: () => Promise<T>): Promise<T> {
    const run = this.chain.then(fn, fn);
    this.chain = run.catch(() => undefined);
    return run;
  }
  private request(cmd: number, payload: ArrayLike<number>, rsp: number): Promise<Frame> {
    return this.exchange(() => {
      const seq = this.nextSeq();
      const p = this.wait((f) => f.seq === seq && (f.cmd === rsp || f.cmd === RSP.ERROR));
      this.tx(cmd, payload, seq);
      return p.then((f) => { if (f.cmd === RSP.ERROR) throw new Error(`device error ${f.payload[1]} for command ${f.payload[0]}`); return f; });
    });
  }

  async hello(): Promise<Identity> {
    const f = await this.request(CMD.HELLO, [], RSP.HELLO);
    const d = f.payload;
    if (d.length < 15) throw new Error('identity reply too short');
    const id: Identity = { protocol: d[0], firmware: `${d[1]}.${d[2]}.${d[3]}`, schema: d[4], tracks: d[5], voices: d[6], steps: d[7], scenes: d[8], caps: d[9] | (d[10] << 8), storage: d[11], hardware: d[14] === 1 };
    if (id.protocol !== schema.protocol) throw new Error(`protocol ${id.protocol} is not supported (editor speaks ${schema.protocol})`);
    if (id.schema !== schema.schema || d[12] !== schema.patchParams || d[13] !== schema.globals) throw new Error(`data schema ${id.schema} does not match this editor (${schema.schema})`);
    this.identity = id;
    return id;
  }
  has(cap: number) { return !!this.identity && (this.identity.caps & cap) !== 0; }

  getBlob(kind: number, a = 0, b = 0): Promise<Uint8Array> {
    return this.exchange(async () => {
      const seq = this.nextSeq();
      const parts: number[] = [];
      let total = -1;
      const next = () => this.wait((f) => f.seq === seq && (f.cmd === RSP.BLOB || f.cmd === RSP.ERROR));
      let p = next();
      this.tx(CMD.GET_BLOB, [kind, a, b], seq);
      for (;;) {
        const f = await p;
        if (f.cmd === RSP.ERROR) throw new Error(`device error ${f.payload[1]} reading blob`);
        const d = f.payload;
        const off = d[3] | (d[4] << 8);
        total = d[5] | (d[6] << 8);
        if (d[0] !== kind || off !== parts.length || total > 4096) throw new Error('blob stream out of order');
        for (let i = 7; i < d.length; i++) parts.push(d[i]);
        if (parts.length >= total) break;
        p = next();
      }
      return Uint8Array.from(parts.slice(0, total));
    });
  }
  /** Volatile transfer into the device's edit buffer. Stop-and-wait per chunk. */
  putBlob(kind: number, a: number, b: number, blob: Uint8Array): Promise<void> {
    if (!this.has(CAP.BLOB_PUT)) return Promise.reject(new Error('device does not accept data'));
    return this.exchange(async () => {
      for (let off = 0; off < blob.length;) {
        const n = Math.min(CHUNK, blob.length - off);
        const seq = this.nextSeq();
        const p = this.wait((f) => f.seq === seq && (f.cmd === RSP.PUT_ACK || f.cmd === RSP.ERROR));
        this.tx(CMD.PUT_BLOB, [kind, a, b, off & 255, off >> 8, blob.length & 255, blob.length >> 8, ...blob.subarray(off, off + n)], seq);
        const f = await p;
        if (f.cmd === RSP.ERROR) throw new Error(`device rejected the transfer (error ${f.payload[1]})`);
        if (f.payload[3] !== 0) throw new Error(`device rejected the data (status ${f.payload[3]})`);
        off += n;
      }
    });
  }
  /** Coalesced live edit: rapid moves of one control send only the latest value. */
  setParam(scope: number, track: number, id: number, value: number) {
    if (this.closed || !this.has(CAP.PARAM)) return;
    this.pendingParams.set(`${scope}:${track}:${id}`, [scope, track, id, value]);
    if (this.pendingParams.size > 64) this.flush();
    else if (!this.flushTimer) this.flushTimer = setTimeout(() => this.flush(), 12);
  }
  flush() {
    if (this.flushTimer) { clearTimeout(this.flushTimer); this.flushTimer = null; }
    const batch = [...this.pendingParams.values()];
    this.pendingParams.clear();
    if (this.closed) return;
    for (const p of batch.slice(0, 24)) this.tx(CMD.SET_PARAM, p);
    for (const p of batch.slice(24)) this.pendingParams.set(`${p[0]}:${p[1]}:${p[2]}`, p); // rate limit
    if (this.pendingParams.size) this.flushTimer = setTimeout(() => this.flush(), 12);
  }
  transportCmd(play: boolean) { if (this.has(CAP.TRANSPORT)) this.tx(CMD.TRANSPORT, [play ? 1 : 0]); }
  note(track: number, note: number, vel: number) { this.tx(CMD.NOTE, [track, note & 127, vel & 127]); }
  select(track: number, scene = schema.scenes) { this.tx(CMD.SELECT, [track, scene]); }
  /** Live chord: 0 = I .. 6 = VII. Moves every sequenced synth note in key. */
  conduct(degree: number) { if (this.has(CAP.LIVE)) this.tx(CMD.LIVE, [0, Math.min(6, Math.max(0, degree))]); }
  /** Write the phrase just played on a track into its pattern (volatile). */
  capture(track: number) { if (this.has(CAP.LIVE)) this.tx(CMD.LIVE, [1, track]); }
  async generate(track: number): Promise<void> { await this.request(CMD.GENERATE, [track], RSP.GENERATED); }
  async status(): Promise<Status> {
    const d = (await this.request(CMD.GET_STATUS, [], RSP.STATUS)).payload;
    return { playing: !!d[0], scene: d[1], step: [d[2], d[3], d[4], d[5]], voices: d[6], load: d[7], track: d[8], page: d[9], storage: d[10], recording: !!d[11], peak: d[12], degree: d[13] ?? 0, pureRoot: d[14] ?? 255, duet: !!d[15], captureSeq: d[16] ?? 0, captureCount: d[17] ?? 0 };
  }
  close() { this.fail(new Error('closed')); this.transport.onFrame = null; this.transport.close(); }
}
