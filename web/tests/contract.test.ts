// Contract tests: the TypeScript editor code against the real firmware core
// (WebAssembly build). Simulation only — this is not a hardware test.
import { readFileSync } from 'node:fs';
import { describe, expect, it } from 'vitest';
import { BLOB, PATCH_BYTES, PATTERN_BYTES, blobPack, blobUnpack, crc16, decodePatch, decodePattern, encodePatch, encodePattern, globalId, patchId, schema, sanitizeGlobals } from '../src/core/schema';
import { CAP, Client, decodeFrame, encodeFrame, pack7, unpack7 } from '../src/core/protocol';
import { Fm1Sim, SimTransport } from '../src/transport/sim';

const wasm = readFileSync(new URL('../public/zarya.wasm', import.meta.url));
async function connect() {
  const sim = await Fm1Sim.create(wasm);
  const client = new Client(new SimTransport(sim), 500);
  const id = await client.hello();
  return { sim, client, id };
}

describe('codec', () => {
  it('pack7 round-trips every length and never sets bit 7', () => {
    for (let n = 0; n < 110; n++) {
      const d = Uint8Array.from({ length: n }, (_, i) => (i * 73 + n * 5) & 255);
      const p = pack7(d);
      expect(p.every((b) => b < 128)).toBe(true);
      expect([...unpack7(Uint8Array.from(p))]).toEqual([...d]);
    }
  });
  it('crc16 matches the CCITT check value', () => expect(crc16(new TextEncoder().encode('123456789'))).toBe(0x29b1));
  it('rejects malformed, truncated, oversized and foreign frames', () => {
    const ok = encodeFrame(4, 1, [0, 0, 1, 2]);
    expect(decodeFrame(ok)).not.toBeNull();
    expect(decodeFrame(ok.subarray(0, ok.length - 1))).toBeNull();
    expect(decodeFrame(Uint8Array.from([0xf0, 0x43, 0x5a, 0x52, 1, 1, 0xf7]))).toBeNull();
    expect(decodeFrame(Uint8Array.from([0xf0, 0x7d, 0x5a, 0x52, 1, 0x81, 0xf7]))).toBeNull();
    expect(decodeFrame(new Uint8Array(200).fill(1))).toBeNull();
    expect(() => encodeFrame(3, 1, new Uint8Array(200))).toThrow();
  });
  it('blob validation reports a precise reason', () => {
    const blob = blobPack(BLOB.PATCH, new Uint8Array(PATCH_BYTES));
    expect(blobUnpack(blob, BLOB.PATCH, PATCH_BYTES).length).toBe(PATCH_BYTES);
    expect(() => blobUnpack(blob, BLOB.PATTERN)).toThrow(/kind/);
    const bad = blob.slice(); bad[20] ^= 1;
    expect(() => blobUnpack(bad, BLOB.PATCH)).toThrow(/checksum/);
    expect(() => blobUnpack(blob.subarray(0, 50), BLOB.PATCH)).toThrow(/length/);
    const v = blob.slice(); v[3] = 99;
    expect(() => blobUnpack(v, BLOB.PATCH)).toThrow(/schema/);
  });
  it('schema ranges are coherent', () => {
    for (const d of [...schema.patch, ...schema.kit, ...schema.global]) {
      expect(d.min).toBeLessThanOrEqual(d.def); expect(d.def).toBeLessThanOrEqual(d.max);
      expect(d.labels.length).toBe(d.max - d.min + 1);
    }
  });
});

describe('editor <-> firmware simulator', () => {
  it('identifies as a simulator with volatile capabilities only', async () => {
    const { id, client } = await connect();
    expect(id.hardware).toBe(false);
    expect(id.firmware).toBe(schema.version);
    expect(client.has(CAP.PARAM) && client.has(CAP.BLOB_GET) && client.has(CAP.BLOB_PUT)).toBe(true);
    expect(client.has(CAP.PERSIST) || client.has(CAP.FLASH)).toBe(false);
  });
  it('reads patches, patterns and globals that decode and re-encode identically', async () => {
    const { client } = await connect();
    for (let t = 0; t < 4; t++) {
      const raw = blobUnpack(await client.getBlob(BLOB.PATCH, t), BLOB.PATCH, PATCH_BYTES);
      const patch = decodePatch(raw);
      expect(patch.type).toBe(t === 3 ? 1 : 0);
      expect([...encodePatch(patch)]).toEqual([...raw]);
      const praw = blobUnpack(await client.getBlob(BLOB.PATTERN, 0, t), BLOB.PATTERN, PATTERN_BYTES);
      expect([...encodePattern(decodePattern(praw))]).toEqual([...praw]);
    }
    const g = blobUnpack(await client.getBlob(BLOB.GLOBALS), BLOB.GLOBALS, schema.globals);
    expect(sanitizeGlobals([...g])).toEqual([...g]);
    expect(g[globalId('TEMPO')] + 40).toBe(118);
  });
  it('live parameter edits reach the firmware, clamped by the firmware', async () => {
    const { client } = await connect();
    client.setParam(0, 0, patchId('CUTOFF'), 33);
    client.setParam(0, 0, patchId('CUTOFF'), 44); // coalesced: only the last value is sent
    client.setParam(0, 0, patchId('ALGO'), 127);
    client.setParam(1, 0, globalId('TEMPO'), 90);
    client.flush();
    const p = decodePatch(blobUnpack(await client.getBlob(BLOB.PATCH, 0), BLOB.PATCH));
    expect(p.p[patchId('CUTOFF')]).toBe(44);
    expect(p.p[patchId('ALGO')]).toBe(7);
    const g = blobUnpack(await client.getBlob(BLOB.GLOBALS), BLOB.GLOBALS);
    expect(g[globalId('TEMPO')]).toBe(90);
  });
  it('writes a patch and a pattern and reads them back; rejects a corrupt blob', async () => {
    const { client } = await connect();
    const patch = decodePatch(blobUnpack(await client.getBlob(BLOB.PATCH, 1), BLOB.PATCH));
    patch.name = 'web test'; patch.p[patchId('RESO')] = 77;
    await client.putBlob(BLOB.PATCH, 0, 0, blobPack(BLOB.PATCH, encodePatch(patch)));
    const back = decodePatch(blobUnpack(await client.getBlob(BLOB.PATCH, 0), BLOB.PATCH));
    expect(back.name).toBe('WEB TEST'); expect(back.p).toEqual(patch.p);
    const pat = decodePattern(blobUnpack(await client.getBlob(BLOB.PATTERN, 0, 2), BLOB.PATTERN));
    pat.steps[5] = { note: 72, vel: 99, gate: 0x15, fx: 0x0a }; pat.length = 24;
    await client.putBlob(BLOB.PATTERN, 3, 2, blobPack(BLOB.PATTERN, encodePattern(pat)));
    expect(decodePattern(blobUnpack(await client.getBlob(BLOB.PATTERN, 3, 2), BLOB.PATTERN))).toEqual(pat);
    const bad = blobPack(BLOB.PATCH, encodePatch(patch)); bad[30] ^= 4;
    await expect(client.putBlob(BLOB.PATCH, 2, 0, bad)).rejects.toThrow(/rejected/);
  });
  it('generates deterministically from the seed and reports panel edits', async () => {
    const { client, sim } = await connect();
    const gen = async (seed: number) => {
      client.setParam(1, 0, globalId('SEED LO'), seed); client.flush();
      await client.generate(2);
      return decodePattern(blobUnpack(await client.getBlob(BLOB.PATTERN, 0, 2), BLOB.PATTERN));
    };
    const a = await gen(7), b = await gen(8), c = await gen(7);
    expect(a).toEqual(c); expect(a).not.toEqual(b);
    const events: unknown[] = [];
    client.onEvent = (e) => events.push(e);
    sim.turn(2, 2); // KNOB 1 on the HOME page = MACRO 1
    for (let i = 0; i < 5; i++) sim.tick();
    await new Promise((r) => setTimeout(r, 20));
    expect(events).toContainEqual({ type: 'param', scope: 0, track: 0, id: patchId('MACRO 1'), value: 2 });
  });
  it('plays: transport, status, audio in range, and never touches flash', async () => {
    const { client, sim } = await connect();
    client.transportCmd(true);
    let peak = 0;
    for (let b = 0; b < 700; b++) for (const s of sim.audio()) { expect(Math.abs(s)).toBeLessThanOrEqual(8388608); peak = Math.max(peak, Math.abs(s)); }
    expect(peak).toBeGreaterThan(100000);
    const st = await client.status();
    expect(st.playing).toBe(true);
    expect(sim.frame(true)!.length).toBe(57600);
    expect(sim.flash().every((b) => b === 0xff)).toBe(true);
  });
  it('times out cleanly when the device is silent and fails pending work on disconnect', async () => {
    const dead = { kind: 'midi' as const, label: 'x', send() {}, onFrame: null, onClose: null as null | (() => void), close() { this.onClose?.(); } };
    const c = new Client(dead, 60);
    await expect(c.hello()).rejects.toThrow(/timeout/);
    const p = c.hello();
    c.close();
    await expect(p).rejects.toThrow();
    expect(() => c.transportCmd(true)).not.toThrow();
    c.setParam(0, 0, 1, 1); // ignored: no identity, closed
  });
});
