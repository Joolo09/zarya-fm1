// SPDX-License-Identifier: GPL-3.0-only
// The firmware simulator: the real ZARYA core compiled to WebAssembly.
// It is NOT hardware. Everything built on it is labelled DEMO / NO HARDWARE.
import type { Transport } from '../core/protocol';

interface Exports {
  memory: WebAssembly.Memory;
  zw_init(blank: number): void; zw_audio(): number; zw_controls(): number; zw_tick(now: number): void;
  zw_fb(): number; zw_draw(force: number): number; zw_inbuf(): number; zw_sysex(n: number): void;
  zw_midi(s: number, a: number, b: number): void; zw_out(): number; zw_out_len(): number; zw_out_clear(): void;
  zw_flash(): number; zw_flash_size(): number; zw_leds(hi: number): number; zw_load(used: number, period: number): void;
}
export const BUTTONS = ['OCT-', 'OCT+', 'FX', 'SCL', 'ENV', 'LFO', 'EDIT', 'GLO', 'HOME', 'SAVE', 'ARP', 'SEQ', 'PLAY', 'REC'] as const;
export const ENCODERS = ['SELECT', 'ALGORITHM', 'KNOB 1', 'KNOB 2', 'KNOB 3', 'KNOB 4', 'PRESETS'] as const;

export class Fm1Sim {
  private x: Exports;
  private now = 0;
  onSysex: ((frame: Uint8Array) => void) | null = null;
  onMidi: ((message: Uint8Array) => void) | null = null;
  private constructor(instance: WebAssembly.Instance) {
    this.x = instance.exports as unknown as Exports;
    this.x.zw_init(1);
  }
  static async create(wasm: BufferSource): Promise<Fm1Sim> {
    const { instance } = await WebAssembly.instantiate(wasm, {});
    return new Fm1Sim(instance);
  }
  private view() { return new DataView(this.x.memory.buffer); }
  setKeys(mask: number) { this.view().setUint32(this.x.zw_controls(), mask >>> 0, true); }
  setButtons(mask: number) { this.view().setUint32(this.x.zw_controls() + 4, mask >>> 0, true); }
  turn(encoder: number, detents: number) {
    const at = this.x.zw_controls() + 8 + encoder * 2, v = this.view();
    v.setInt16(at, v.getInt16(at, true) + detents, true);
  }
  /** Advance the control task by ms and collect what the firmware sent. */
  tick(ms = 10) {
    this.now += ms;
    this.x.zw_tick(this.now);
    const len = this.x.zw_out_len();
    if (!len) return;
    const out = new Uint8Array(this.x.memory.buffer, this.x.zw_out(), len).slice();
    this.x.zw_out_clear();
    for (let i = 0; i < out.length;) {
      const n = out[i++], m = out.subarray(i, i + n);
      i += n;
      if (m[0] === 0xf0) this.onSysex?.(m); else this.onMidi?.(m);
    }
  }
  sysex(frame: Uint8Array) {
    if (frame.length > 256) return;
    new Uint8Array(this.x.memory.buffer, this.x.zw_inbuf(), 256).set(frame);
    this.x.zw_sysex(frame.length);
  }
  midi(status: number, a: number, b: number) { this.x.zw_midi(status, a, b); }
  /** 128 stereo frames, signed Q23, interleaved. The view is valid until the next call. */
  audio(): Int32Array { return new Int32Array(this.x.memory.buffer, this.x.zw_audio(), 256); }
  /** RGB565 240x240 frame, or null when nothing changed. */
  frame(force = false): Uint16Array | null {
    return this.x.zw_draw(force ? 1 : 0) ? new Uint16Array(this.x.memory.buffer, this.x.zw_fb(), 240 * 240) : null;
  }
  leds(): [number, number] { return [this.x.zw_leds(0) >>> 0, this.x.zw_leds(1) >>> 0]; }
  flash(): Uint8Array { return new Uint8Array(this.x.memory.buffer, this.x.zw_flash(), this.x.zw_flash_size()); }
}

export class SimTransport implements Transport {
  readonly kind = 'sim' as const;
  readonly label = 'DEMO / NO HARDWARE — firmware simulator';
  onFrame: ((frame: Uint8Array) => void) | null = null;
  onClose: (() => void) | null = null;
  private queue: Uint8Array[] = [];
  private draining = false;
  constructor(readonly sim: Fm1Sim) {
    sim.onSysex = (f) => { this.queue.push(f.slice()); this.drain(); };
  }
  private drain() {
    if (this.draining) return;
    this.draining = true;
    const step = () => {
      const f = this.queue.shift();
      if (!f) { this.draining = false; return; }
      this.onFrame?.(f);
      setTimeout(step, 0); // one frame per task, like real MIDI events
    };
    setTimeout(step, 0);
  }
  send(frame: Uint8Array) {
    this.sim.sysex(frame);
    for (let i = 0; i < 6; i++) this.sim.tick(1);
  }
  close() { this.sim.onSysex = null; this.onClose?.(); }
}
