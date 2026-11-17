// SPDX-License-Identifier: GPL-3.0-only
// The project site. Everything that moves or sounds here is the firmware core
// itself (WebAssembly), driven through the same protocol client as the editor.
import { App } from '../app';
import { globalId } from '../core/schema';
import { Fm1Sim } from '../transport/sim';
import wasmUrl from '../assets/zarya.wasm?url';
import { DUET_PHRASE, PURE_CHORD, pureDemoPatch } from './demos';

const $ = <T extends HTMLElement>(id: string) => document.getElementById(id) as T;

// ---------- language ----------
const EN: Record<string, string> = {
  'skip': 'Skip to content', 'nav.live': 'Features', 'nav.engine': 'Engine', 'nav.editor': 'Editor', 'nav.install': 'Install',
  'hero.eyebrow': 'Firmware for the M-VAVE FM-1 · v0.2',
  'hero.lead': 'A pocket FM synth that listens to what you play: it pulls chords into pure tuning, answers your phrases and remembers what you did not manage to record.',
  'hero.status': 'Runs in the simulator. Not built for the device yet — <a href="#install">what that means</a>.',
  'hero.editor': 'Open the editor',
  'hero.caption': 'This is not a video. The screen is driven by the real firmware code, compiled to WebAssembly. Start the demo and browse the sounds.',
  'live.label': 'Live features', 'live.title': 'Five things we found nowhere else',
  'live.sub': 'The FM-1 community catalogue lists more than 80 firmwares. None of them has adaptive pure tuning, an answer to the phrase you played, after-the-fact recording or sound breeding. Moving the whole song to another chord does exist — here it follows the scale.',
  'pure.title': 'Chords without beating',
  'pure.text': 'In equal temperament a major third is 14 cents sharp. On a bright FM sound you hear it as a shimmer inside the chord. PURE finds the root of whatever is sounding and bends every voice to pure intervals on the fly — while the chord as a whole stays in tune with other instruments.',
  'pure.proof': 'Measured in the simulator: the harmonic shared by the third beats 83% deep in equal temperament and 3% with PURE.',
  'pure.scope': 'Loudness of the harmonic shared by the third: the waves are the beating.',
  'pure.et': 'Equal temperament', 'pure.on': 'PURE',
  'duet.title': 'It answers your phrase',
  'duet.text': 'Play a bar — one bar later the instrument replies. ECHO repeats, ANSWER keeps your opening and brings the phrase home to the tonic, MIRROR inverts the melody around its first note, WILD improvises in key.',
  'duet.you': 'you', 'duet.it': 'instrument', 'duet.mode': 'Mode', 'duet.play': 'Play a phrase',
  'chord.title': 'The whole song under one finger',
  'chord.text': 'Hold SCL and press a white key: the bass, the arpeggio and the chords of every track move to that degree of the scale. Chords are rebuilt from the notes of the scale, so IV in a minor key stays minor.',
  'chord.label': 'Scale degree · the demo plays', 'chord.hint': 'Press a degree: the demo changes harmony on the next step.',
  'capture.title': 'Recording after the fact',
  'capture.text': 'The instrument always remembers the last phrase. It was good and recording was off? GLO + REC — and it is in the pattern, on its own steps. With undo.',
  'capture.hint': 'The firmware screen after CAPTURE: three notes played without recording landed on steps 1, 3 and 5.',
  'evolve.title': 'Breeding sounds',
  'evolve.text': 'Four children of the current sound, with an optional second parent from the factory bank. Listen, pick the best, breed the next generation. You can return to the original at any time.',
  'evolve.hint': 'The EVOLVE screen: the bars are the sound’s “fingerprint”; yellow marks where a child differs from its parent.',
  'engine.label': 'Engine', 'engine.title': 'What is inside', 'engine.sound': 'Sound', 'engine.seq': 'Sequencer', 'engine.safe': 'Reliability',
  'engine.s1': '4 tracks: three synths sharing 8 voices and a drum machine with 8 synthesized instruments',
  'engine.s2': '4 FM operators, 8 algorithms, 8 waveforms per operator',
  'engine.s3': 'Resonant filter with envelope, LFO, 4 macros per sound',
  'engine.s4': 'Tempo delay, reverb, drive, DJ filter, kick-driven pump',
  'engine.s5': '33 presets and 4 drum kits',
  'engine.q1': '64 steps, 8 scenes, each track with its own length',
  'engine.q2': 'Swing, probability, ratchets, a chord per step',
  'engine.q3': 'Live and step recording, arpeggiator, scale lock',
  'engine.q4': 'Generator: one seed gives a matching bass, chords, arpeggio and melody',
  'engine.r1': 'Integer-only maths: the sound is bit-identical in the tests and in the browser',
  'engine.r2': '90,578 automated checks of the core',
  'engine.r3': 'Saves are kept in two copies: a test cuts the power at every write step',
  'engine.r4': 'Foreign data in flash is never touched without an explicit FORMAT on the device',
  'editor.label': 'Editor', 'editor.title': 'The whole synth in a browser',
  'editor.text': 'Sound Lab, sequencer, Melody Lab, Live, mixer, sound library and project backup. It needs no installation and no FM-1 — the firmware simulator is inside.',
  'editor.midi': 'It connects to the device over Web MIDI (Chrome, Edge) and makes temporary edits only. It cannot write to the FM-1 memory or flash it — it has no such commands.',
  'install.label': 'Install', 'install.title': 'What you can install today',
  'install.now.label': 'Ready', 'install.now.title': 'Editor and simulator',
  'install.now.text': 'There is nothing to install: open the editor in Chrome or Edge. To run it yourself and change the code, three commands:',
  'install.fw.label': 'NO-GO · nothing to flash', 'install.fw.title': 'Firmware for the FM-1',
  'install.fw.text': 'A firmware file does not exist yet, so there is no installer here. What is missing:',
  'install.fw.1': 'the image for the FM-1 processor is not built — it needs the JieLi toolchain and the official M-VAVE SDK;',
  'install.fw.2': 'the SDK adapter is written but has never been compiled;',
  'install.fw.3': 'the code has never run on a real device.',
  'install.fw.button': 'Install on the FM-1',
  'install.fw.why': 'The button will work once there is a built image with a checksum and a verified way back to the factory firmware.',
  'install.pre.label': 'Before any flashing', 'install.pre.title': 'Checklist',
  'install.pre.1': 'Save your projects, samples and voices in the <a href="https://open.m-vave.com/fm1-workbench">official workbench</a>: ZARYA stores data in the same partition in a different format.',
  'install.pre.2': 'Make sure your hardware revision has a working return to factory V15 and an emergency recovery path.',
  'install.pre.3': 'Check the SHA-256 of the image and keep the power on while writing.',
  'install.pre.4': 'For newer FM-1 units with UC25HQ80 flash the community reports problems with some custom firmwares — check compatibility separately.',
  'install.help.label': 'You can help', 'install.help.title': 'Build it for hardware',
  'install.help.text': 'You need Windows, the JieLi 2.5.3 toolchain and the fm1_open kit. The steps and the list of what is still unverified are in <a href="https://github.com/Joolo09/zarya-fm1/blob/main/device/README.md">device/README.md</a>. Send link errors to Issues.',
  'install.help.status': 'Current safety status',
  'foot.text': 'an independent project. “M-VAVE” and “FM-1” are trademarks of their owners. Code: GPL-3.0 with the FM1 SDK exception.',
};
const DYN = {
  ru: { play: '▶ Включить демо', stop: '■ Остановить', listening: 'слушает', answering: 'отвечает', root: 'тоника', title: 'ZARYA — прошивка для M-VAVE FM-1', failed: 'Симулятор не запустился в этом браузере.' },
  en: { play: '▶ Start the demo', stop: '■ Stop', listening: 'listening', answering: 'answering', root: 'root', title: 'ZARYA — firmware for the M-VAVE FM-1', failed: 'The simulator did not start in this browser.' },
};
type Lang = keyof typeof DYN;
const nodes = [...document.querySelectorAll<HTMLElement>('[data-i18n]')];
const RU = new Map(nodes.map((n) => [n, n.innerHTML]));
let lang: Lang = 'ru';
function setLang(l: Lang) {
  lang = l;
  for (const n of nodes) n.innerHTML = l === 'en' ? EN[n.dataset.i18n!] ?? RU.get(n)! : RU.get(n)!;
  document.documentElement.lang = l;
  document.title = DYN[l].title;
  $('lang').textContent = l === 'en' ? 'RU' : 'EN';
  try { localStorage.setItem('zarya.lang', l); } catch { /* preference only */ }
  refresh();
}

// ---------- the firmware ----------
const app = new App();
app.persist = false; // the site's demos must not overwrite the editor's draft
const NOTES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
const ROMAN = ['I', 'II', 'III', 'IV', 'V', 'VI', 'VII'];

function drawLcd(canvas: HTMLCanvasElement, fb: Uint16Array) {
  const ctx = canvas.getContext('2d')!, img = ctx.createImageData(240, 240);
  for (let i = 0; i < 57600; i++) { const v = fb[i]; img.data[4 * i] = (v >> 8) & 0xf8; img.data[4 * i + 1] = (v >> 3) & 0xfc; img.data[4 * i + 2] = (v << 3) & 0xf8; img.data[4 * i + 3] = 255; }
  ctx.putImageData(img, 0, 0);
}
const css = (name: string) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

// hero panel
for (const b of document.querySelectorAll<HTMLButtonElement>('[data-btn]'))
  b.addEventListener('click', () => { const sim = app.sim; if (!sim) return; sim.setButtons(1 << Number(b.dataset.btn)); setTimeout(() => sim.setButtons(0), 90); });
for (const b of document.querySelectorAll<HTMLButtonElement>('[data-enc]'))
  b.addEventListener('click', () => app.sim?.turn(Number(b.dataset.enc), Number(b.dataset.dir)));
$('play').addEventListener('click', () => { void app.ensureAudio().then(() => app.togglePlay()); });
$('lang').addEventListener('click', () => setLang(lang === 'ru' ? 'en' : 'ru'));

// ---------- PURE ----------
const scope = $<HTMLCanvasElement>('pure-scope');
let scopeOn = false, scopePure = false, phase = 0, saved: typeof app.patches[number] | null = null, pureRun = 0;
const blocks: [number, number][] = [], hist: number[] = [];
app.onBlock = (f) => {
  if (!scopeOn) return;
  // Follow the band around 1313 Hz, where the 5th harmonic of C and the 4th of E meet.
  let re = 0, im = 0;
  for (let i = 0; i < 128; i++, phase++) { const ph = (2 * Math.PI * 1313 * phase) / 44100; re += f[2 * i] * Math.cos(ph); im += f[2 * i] * Math.sin(ph); }
  blocks.push([re, im]);
  if (blocks.length > 14) blocks.shift();
  // Hann weights across the last 14 blocks (41 ms) keep neighbouring harmonics out of the band.
  let sr = 0, si = 0;
  blocks.forEach((b, k) => { const w = 0.5 - 0.5 * Math.cos((2 * Math.PI * (k + 0.5)) / blocks.length); sr += b[0] * w; si += b[1] * w; });
  hist.push(Math.hypot(sr, si));
  if (hist.length > 520) hist.shift();
};
function drawScope() {
  const g = scope.getContext('2d')!, w = scope.width, h = scope.height;
  g.clearRect(0, 0, w, h);
  g.strokeStyle = css('--line'); g.lineWidth = 1;
  g.beginPath(); g.moveTo(0, h - 12.5); g.lineTo(w, h - 12.5); g.stroke();
  if (hist.length < 2) return;
  const max = Math.max(...hist, 1e-6);
  g.strokeStyle = scopePure ? css('--cyan') : css('--dim'); g.lineWidth = 2; g.beginPath();
  hist.forEach((v, i) => { const x = (i / 520) * w, y = h - 12 - (v / max) * (h - 28); if (i) g.lineTo(x, y); else g.moveTo(x, y); });
  g.stroke();
}
async function pureDemo(on: boolean) {
  const c = app.client;
  if (!c || !app.patches[1]) return;
  const run = ++pureRun;
  await app.ensureAudio();
  if (app.playing) app.togglePlay();
  saved ??= JSON.parse(JSON.stringify(app.patches[1])) as typeof app.patches[number];
  await app.setPatch(1, pureDemoPatch(saved));
  app.setGlobal(globalId('PURE'), on ? 127 : 0);
  c.flush();
  hist.length = 0; blocks.length = 0; scopePure = on; scopeOn = true;
  $('pure-et').classList.toggle('on', !on); $('pure-on').classList.toggle('on', on);
  for (const n of PURE_CHORD) c.note(1, n, 100);
  setTimeout(() => { for (const n of PURE_CHORD) c.note(1, n, 0); }, 1500);
  setTimeout(() => {
    if (run !== pureRun) return; // another demo started meanwhile
    scopeOn = false;
    app.setGlobal(globalId('PURE'), 0);
    if (saved) void app.setPatch(1, saved);
  }, 2400);
}
$('pure-et').addEventListener('click', () => void pureDemo(false));
$('pure-on').addEventListener('click', () => void pureDemo(true));

// ---------- DUET ----------
const roll = $<HTMLCanvasElement>('duet-roll');
let duetT0 = 0, duetSpan = 4000, duetUntil = 0;
const heard: { t: number; note: number; answer: boolean }[] = [];
function drawRoll() {
  const g = roll.getContext('2d')!, w = roll.width, h = roll.height;
  g.clearRect(0, 0, w, h);
  g.fillStyle = css('--line');
  g.fillRect(w / 2, 8, 1, h - 16);
  for (const e of heard) {
    g.fillStyle = e.answer ? css('--coral') : css('--lime');
    g.fillRect(8 + (e.t / duetSpan) * (w - 40), h - 14 - ((e.note - 60) / 26) * (h - 30), 26, 7);
  }
}
async function duetDemo() {
  const c = app.client, sim = app.sim;
  if (!c || !sim) return;
  await app.ensureAudio();
  if (app.playing) app.togglePlay();
  app.selectTrack(2);
  app.setGlobal(globalId('DUET'), Number($<HTMLSelectElement>('duet-mode').value));
  app.setGlobal(globalId('DUET VAR'), 60);
  c.flush();
  const step = 60000 / ((app.globals[globalId('TEMPO')] ?? 80) + 40) / 4;
  duetSpan = step * 32; duetT0 = performance.now() + 120; duetUntil = duetT0 + duetSpan + 600;
  heard.length = 0;
  sim.onMidi = (m) => {
    if (m[0] !== 0x92 || !m[2] || performance.now() > duetUntil) return;
    heard.push({ t: performance.now() - duetT0, note: m[1], answer: heard.length >= DUET_PHRASE.length });
  };
  DUET_PHRASE.forEach((n, i) => setTimeout(() => { c.note(2, n, 100); setTimeout(() => c.note(2, n, 0), step * 1.2); }, 120 + i * step * 2));
}
$('duet-play').addEventListener('click', () => void duetDemo());

// ---------- CHORD ----------
const degrees = ROMAN.map((r, i) => {
  const b = document.createElement('button');
  b.type = 'button'; b.textContent = r;
  b.addEventListener('click', () => { void app.ensureAudio().then(() => { if (!app.playing) app.togglePlay(); app.conduct(i); }); });
  $('degrees').append(b);
  return b;
});

// ---------- screens that need a few gestures: a private simulator each ----------
async function gallery() {
  const bytes = await (await fetch(wasmUrl)).arrayBuffer();
  const press = (g: Fm1Sim, mask: number) => { g.setButtons(mask); g.tick(10); g.setButtons(0); g.tick(10); };
  const run = (g: Fm1Sim, n: number) => { for (let i = 0; i < n; i++) g.audio(); };
  const evo = await Fm1Sim.create(bytes);
  press(evo, 1 << 6);           // EDIT
  evo.turn(1, 7); evo.tick(10); // ALGORITHM -> EVOLVE
  evo.turn(4, 2); evo.tick(10); // KNOB 3: pick child 2
  drawLcd($<HTMLCanvasElement>('shot-evolve'), evo.frame(true)!);
  const cap = await Fm1Sim.create(bytes);
  for (const n of [60, 62, 64]) { cap.midi(0x90, n, 96); run(cap, 12); cap.midi(0x80, n, 0); run(cap, 74); }
  press(cap, (1 << 7) | (1 << 13)); // GLO + REC
  run(cap, 4); cap.tick(10); cap.tick(2000);
  press(cap, 1 << 11);              // SEQ
  drawLcd($<HTMLCanvasElement>('shot-capture'), cap.frame(true)!);
}

// ---------- refresh ----------
function refresh() {
  const d = DYN[lang], s = app.status;
  $('play').textContent = app.playing ? d.stop : d.play;
  $('pure-root').textContent = `${d.root}: ${s && s.pureRoot < 12 ? NOTES[s.pureRoot] : '—'}`;
  $('duet-state').textContent = s?.duet ? d.answering : performance.now() < duetUntil ? d.listening : '—';
  degrees.forEach((b, i) => b.classList.toggle('on', app.playing && (s?.degree ?? 0) === i));
}
app.subscribe(refresh);
let drawn = false;
setInterval(() => {
  const fb = app.sim?.frame(!drawn);
  if (fb) { drawLcd($<HTMLCanvasElement>('lcd'), fb); drawn = true; }
  drawScope(); drawRoll();
}, 50);

let preferred: Lang = navigator.language.toLowerCase().startsWith('ru') ? 'ru' : 'en';
try { const s = localStorage.getItem('zarya.lang'); if (s === 'ru' || s === 'en') preferred = s; } catch { /* default */ }
setLang(preferred);
void app.start(wasmUrl).then(() => {
  if (!app.sim) { $('play').setAttribute('disabled', ''); document.querySelector('.caption')!.textContent = DYN[lang].failed; return; }
  return gallery();
}).catch(() => undefined);
