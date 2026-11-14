// SPDX-License-Identifier: GPL-3.0-only
// Web MIDI transport. Connecting only opens ports; it sends nothing until the
// editor's client asks, and the client can only send the documented volatile
// commands. Requires a secure context (https or localhost) and SysEx permission.
import type { Transport } from '../core/protocol';

export interface MidiSupport { ok: boolean; reason?: string }
export function midiSupport(): MidiSupport {
  if (typeof navigator === 'undefined' || !('requestMIDIAccess' in navigator)) return { ok: false, reason: 'This browser has no Web MIDI. Use Chrome or Edge on a computer.' };
  if (typeof window !== 'undefined' && !window.isSecureContext) return { ok: false, reason: 'Web MIDI needs https or localhost.' };
  return { ok: true };
}
export interface PortPair { id: string; name: string; input: MIDIInput; output: MIDIOutput }

export async function listPorts(): Promise<PortPair[]> {
  const access = await navigator.requestMIDIAccess({ sysex: true });
  const pairs: PortPair[] = [];
  for (const output of access.outputs.values()) {
    const input = [...access.inputs.values()].find((i) => i.name === output.name) ?? [...access.inputs.values()].find((i) => i.manufacturer === output.manufacturer);
    if (input) pairs.push({ id: output.id, name: output.name ?? 'MIDI device', input, output });
  }
  return pairs;
}

export class WebMidiTransport implements Transport {
  readonly kind = 'midi' as const;
  readonly label: string;
  onFrame: ((frame: Uint8Array) => void) | null = null;
  onClose: (() => void) | null = null;
  private closed = false;
  private readonly onState = () => { if (this.pair.input.state === 'disconnected' || this.pair.output.state === 'disconnected') this.close(); };
  constructor(private pair: PortPair) {
    this.label = pair.name;
    pair.input.onmidimessage = (e) => {
      const d = e.data;
      if (d && d.length <= 128 && d[0] === 0xf0 && d[d.length - 1] === 0xf7) this.onFrame?.(new Uint8Array(d));
    };
    pair.input.addEventListener('statechange', this.onState);
    pair.output.addEventListener('statechange', this.onState);
  }
  static async open(pair: PortPair): Promise<WebMidiTransport> {
    await Promise.all([pair.input.open(), pair.output.open()]);
    return new WebMidiTransport(pair);
  }
  send(frame: Uint8Array) { if (!this.closed) this.pair.output.send(frame); }
  close() {
    if (this.closed) return;
    this.closed = true;
    this.pair.input.onmidimessage = null;
    this.pair.input.removeEventListener('statechange', this.onState);
    this.pair.output.removeEventListener('statechange', this.onState);
    this.onClose?.();
  }
}
