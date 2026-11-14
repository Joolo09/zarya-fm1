// SPDX-License-Identifier: GPL-3.0-only
// Plays 128-frame stereo blocks rendered by the firmware simulator.
class Ring extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = [];
    this.port.onmessage = (e) => { if (this.q.length < 64) this.q.push(e.data); };
  }
  process(_inputs, outputs) {
    const out = outputs[0], b = this.q.shift();
    if (!b) return true; // underrun: silence
    for (let i = 0; i < 128; i++) { out[0][i] = b[2 * i]; if (out[1]) out[1][i] = b[2 * i + 1]; }
    return true;
  }
}
registerProcessor('ring', Ring);
