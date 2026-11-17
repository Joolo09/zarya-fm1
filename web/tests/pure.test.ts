// Measures the claim behind the PURE demo on the site: with a harmonically
// rich sound, the harmonics shared by a chord's notes beat in equal temperament
// and stand still with PURE. Simulation only.
import { readFileSync } from 'node:fs';
import { expect, it } from 'vitest';
import { BLOB, blobPack, blobUnpack, decodePatch, encodePatch, globalId } from '../src/core/schema';
import { Client } from '../src/core/protocol';
import { Fm1Sim, SimTransport } from '../src/transport/sim';
import { PURE_CHORD, pureDemoPatch } from '../src/site/demos';

const wasm = readFileSync(new URL('../src/assets/zarya.wasm', import.meta.url));

async function wobble(pure: number): Promise<number> {
  const sim = await Fm1Sim.create(wasm);
  const client = new Client(new SimTransport(sim), 500);
  await client.hello();
  const base = decodePatch(blobUnpack(await client.getBlob(BLOB.PATCH, 1), BLOB.PATCH));
  const demo = pureDemoPatch(base);
  await client.putBlob(BLOB.PATCH, 1, 0, blobPack(BLOB.PATCH, encodePatch(demo)));
  for (const [name, v] of [['PURE', pure], ['REV LVL', 0], ['DLY LVL', 0]] as const) client.setParam(1, 0, globalId(name), v);
  client.flush();
  for (const n of PURE_CHORD) client.note(1, n, 100);
  const x: number[] = [];
  for (let b = 0; b < 800; b++) { const a = sim.audio(); for (let i = 0; i < 256; i += 2) x.push(a[i] / 8388608); } // ~2.3 s, left channel
  // The 5th harmonic of C4 and the 4th harmonic of E4 meet near 1313 Hz. In
  // equal temperament they are 10 Hz apart and beat; a pure third makes them
  // coincide. Follow the loudness of that narrow band (35 ms Hann windows).
  const f = 1313, N = 1536, env: number[] = [];
  for (let start = 44100 * 0.5; start + N < x.length; start += 128) {
    let re = 0, im = 0;
    for (let n = 0; n < N; n++) { const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * n) / (N - 1)), ph = (2 * Math.PI * f * (start + n)) / 44100; re += x[start + n] * w * Math.cos(ph); im += x[start + n] * w * Math.sin(ph); }
    env.push(Math.hypot(re, im));
  }
  const max = Math.max(...env), min = Math.min(...env);
  expect(max).toBeGreaterThan(1);
  return (max - min) / (max + min); // 0 = perfectly steady, 1 = beats down to silence
}

it('PURE makes a sawtooth dominant seventh chord beat less', async () => {
  const equal = await wobble(0), pure = await wobble(127);
  console.log(`beat depth of the major third's shared harmonic: equal temperament ${(equal * 100).toFixed(0)}%, PURE ${(pure * 100).toFixed(0)}%`);
  expect(equal).toBeGreaterThan(0.5);
  expect(pure).toBeLessThan(0.15);
});
