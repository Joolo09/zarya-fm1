// SPDX-License-Identifier: GPL-3.0-only
// Editor views. Controls are generated from the firmware schema. A full
// rebuild happens only on structural changes; value changes update in place
// so that a knob being dragged is never replaced under the pointer.
import { App } from '../app';
import { CAP } from '../core/protocol';
import { clampParam, globalId, isKit, label, noteName, opParam, patchDesc, patchId, schema, trackGlobal, type ParamDesc, type Pattern } from '../core/schema';
import { BUTTONS, ENCODERS } from '../transport/sim';
import { listPorts, midiSupport, type PortPair } from '../transport/webmidi';

type Child = Node | string | null | false | undefined;
function h<K extends keyof HTMLElementTagNameMap>(tag: K, attrs: Record<string, unknown> = {}, ...kids: Child[]): HTMLElementTagNameMap[K] {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs)) {
    if (v === undefined || v === false || v === null) continue;
    if (k.startsWith('on') && typeof v === 'function') e.addEventListener(k.slice(2), v as EventListener);
    else if (k === 'class') e.className = String(v);
    else if (k === 'style') e.setAttribute('style', String(v));
    else if (k in e && k !== 'list') (e as unknown as Record<string, unknown>)[k] = v;
    else e.setAttribute(k, String(v));
  }
  for (const c of kids) if (c) e.append(c);
  return e;
}
const TRACK_COLORS = ['#cbf36b', '#5adbcb', '#ff846c', '#ffc857'];
const PAGES = ['Device', 'Sound Lab', 'Sequencer', 'Melody Lab', 'Mixer & FX', 'Library', 'Backup', 'Diagnostics'] as const;
type Page = (typeof PAGES)[number];

let updaters: (() => void)[] = [];
const onUpdate = (fn: () => void) => { updaters.push(fn); fn(); };

/** A rotary control: drag, wheel, arrow keys, double-click for the default. */
function knob(d: ParamDesc, name: string, get: () => number, set: (v: number) => void, disabled = false): HTMLElement {
  const NS = 'http://www.w3.org/2000/svg';
  const svg = document.createElementNS(NS, 'svg');
  svg.setAttribute('viewBox', '0 0 48 48');
  const arc = (color: string, width: number) => { const p = document.createElementNS(NS, 'path'); p.setAttribute('fill', 'none'); p.setAttribute('stroke', color); p.setAttribute('stroke-width', String(width)); p.setAttribute('stroke-linecap', 'round'); svg.append(p); return p; };
  const pt = (a: number) => `${24 + 18 * Math.cos(a)} ${24 + 18 * Math.sin(a)}`;
  const path = (t: number) => { const a0 = Math.PI * 0.75, a1 = a0 + Math.PI * 1.5 * t; return `M ${pt(a0)} A 18 18 0 ${t > 2 / 3 ? 1 : 0} 1 ${pt(a1)}`; };
  arc('#2a384a', 4).setAttribute('d', path(1));
  const fg = arc('var(--accent)', 4);
  const val = h('div', { class: 'val' });
  const el = h('div', { class: 'knob', role: 'slider', tabIndex: disabled ? -1 : 0, title: `${name} — drag, scroll or use arrow keys; double-click resets`, 'aria-label': name, 'aria-valuemin': d.min, 'aria-valuemax': d.max },
    h('div', { class: 'nm' }, name), svg as unknown as Node, val);
  if (disabled) el.style.opacity = '.4';
  let startY = 0, startV = 0, dragging = false;
  svg.addEventListener('pointerdown', (e) => { if (disabled) return; dragging = true; startY = e.clientY; startV = get(); svg.setPointerCapture(e.pointerId); });
  svg.addEventListener('pointermove', (e) => { if (dragging) set(clampParam(d, startV + ((startY - e.clientY) * Math.max(d.max - d.min, 24)) / 160)); });
  const end = () => { dragging = false; };
  svg.addEventListener('pointerup', end); svg.addEventListener('pointercancel', end);
  svg.addEventListener('dblclick', () => { if (!disabled) set(d.def); });
  el.addEventListener('wheel', (e) => { if (disabled) return; e.preventDefault(); set(clampParam(d, get() + (e.deltaY < 0 ? 1 : -1))); }, { passive: false });
  el.addEventListener('keydown', (e) => {
    if (disabled) return;
    const step = e.shiftKey ? 8 : 1;
    if (e.key === 'ArrowUp' || e.key === 'ArrowRight') set(clampParam(d, get() + step));
    else if (e.key === 'ArrowDown' || e.key === 'ArrowLeft') set(clampParam(d, get() - step));
    else if (e.key === 'Home') set(d.min); else if (e.key === 'End') set(d.max); else return;
    e.preventDefault();
  });
  onUpdate(() => {
    const v = get();
    fg.setAttribute('d', path(d.max > d.min ? Math.max(0.001, (v - d.min) / (d.max - d.min)) : 0.001));
    val.textContent = label(d, v);
    el.setAttribute('aria-valuenow', String(v)); el.setAttribute('aria-valuetext', label(d, v));
  });
  return el;
}
function choice(d: ParamDesc, name: string, get: () => number, set: (v: number) => void): HTMLElement {
  const sel = h('select', { 'aria-label': name, onchange: () => set(Number(sel.value)) }, ...d.labels.map((l, i) => h('option', { value: String(d.min + i) }, l)));
  onUpdate(() => { sel.value = String(get()); });
  return h('label', { class: 'field' }, name, sel);
}
const FMT_ENUM = 3, FMT_ONOFF = 4;
function control(d: ParamDesc, name: string, get: () => number, set: (v: number) => void): HTMLElement {
  return d.fmt === FMT_ENUM || d.fmt === FMT_ONOFF ? choice(d, name, get, set) : knob(d, name, get, set);
}

export function mount(app: App, root: HTMLElement) {
  let page: Page = 'Device';
  let selStep = 0, seqPage = 0, lastNote = 60, libFilter = '', libCat = -1;
  let ports: PortPair[] = [];
  const pp = (t: number, id: number) => { const d = patchDesc(t, id)!; return control(d, d.name, () => app.patches[t]?.p[id] ?? d.def, (v) => app.setPatchParam(t, id, v)); };
  const gp = (id: number, name?: string) => { const d = schema.global[id]; return control(d, name ?? d.name, () => app.globals[id] ?? d.def, (v) => app.setGlobal(id, v)); };
  const card = (title: string, ...kids: Child[]) => h('section', { class: 'card' }, h('h3', {}, title), ...kids);
  const download = (name: string, text: string) => { const a = h('a', { href: URL.createObjectURL(new Blob([text], { type: 'application/json' })), download: name }); a.click(); setTimeout(() => URL.revokeObjectURL(a.href), 1000); };
  const pickFile = (cb: (text: string) => void) => { const i = h('input', { type: 'file', accept: '.json,application/json', onchange: () => { const f = i.files?.[0]; if (f) void f.text().then(cb); } }); i.click(); };

  // ---------- header ----------
  function header(): HTMLElement {
    const hw = app.mode === 'midi';
    const pill = app.mode === 'sim' ? h('span', { class: 'pill demo', title: 'The firmware runs in your browser. No FM-1 is involved.' }, 'DEMO / NO HARDWARE')
      : hw ? h('span', { class: 'pill hw' }, `FM-1 · ${app.deviceLabel} · fw ${app.identity?.firmware ?? '?'}`)
      : h('span', { class: 'pill off' }, app.mode === 'starting' ? 'STARTING…' : 'OFFLINE');
    const play = h('button', { onclick: () => app.togglePlay(), disabled: hw && !app.liveEdit, 'aria-label': 'Play or stop' });
    const tempo = h('span', { class: 'pill', style: 'font-family:var(--mono)' });
    const undo = h('button', { onclick: () => void app.undo(), title: 'Undo (Ctrl+Z)' }, '↶');
    const redo = h('button', { onclick: () => void app.redo(), title: 'Redo (Ctrl+Shift+Z)' }, '↷');
    onUpdate(() => {
      play.textContent = app.playing ? '■ Stop' : '▶ Play'; play.className = app.playing ? 'on' : 'primary';
      tempo.textContent = `${(app.globals[globalId('TEMPO')] ?? 80) + 40} BPM`;
      undo.disabled = !app.canUndo; redo.disabled = !app.canRedo;
    });
    return h('header', { class: 'top' },
      h('div', { class: 'brand' }, 'ZARYA', h('small', {}, 'editor for M-VAVE FM-1')), pill,
      hw && h('button', { class: app.liveEdit ? 'on' : '', 'aria-pressed': String(app.liveEdit), title: 'When on, edits are sent to the FM-1 edit buffer (not saved to its memory).', onclick: () => app.setLiveEdit(!app.liveEdit) }, app.liveEdit ? 'Live edit: ON' : 'Live edit: OFF'),
      hw && app.unsynced && h('button', { onclick: () => { if (confirm('Send the editor project to the FM-1 edit buffer?\nThis replaces what is currently playing on the device (not its saved memory).')) void app.pushAll(); } }, 'Send to FM-1…'),
      h('span', { class: 'spacer' }), tempo, undo, redo, play,
      h('button', { onclick: () => void openConnect() }, hw ? 'Connection…' : 'Connect FM-1…'));
  }
  async function openConnect() {
    const sup = midiSupport();
    const body = h('div', {});
    const dlg = h('dialog', {}, h('h3', {}, 'Connect an FM-1'), body, h('div', { class: 'row', style: 'margin-top:12px' }, h('button', { onclick: () => dlg.close() }, 'Close')));
    dlg.addEventListener('close', () => dlg.remove());
    document.body.append(dlg); dlg.showModal();
    const fill = () => {
      const kids: Child[] = [
        h('p', { class: 'muted' }, 'Connecting only identifies the device and reads its current state. Nothing is written until you turn on Live edit, and the editor can never write the FM-1 memory or install firmware.'),
        !sup.ok ? h('p', { style: 'color:var(--red)' }, sup.reason) : null,
        app.mode === 'midi' ? h('button', { onclick: () => { void app.useSimulator(); dlg.close(); } }, 'Disconnect and return to the simulator') : null,
        ...ports.map((p) => h('p', {}, h('button', { class: 'primary', onclick: () => { void app.connectMidi(p).then(() => dlg.close(), () => fill()); } }, `Connect: ${p.name}`))),
        sup.ok && !ports.length ? h('p', { class: 'muted' }, 'No MIDI ports with both input and output were found. Connect the FM-1 by USB (it must run ZARYA firmware) and allow MIDI + SysEx access.') : null,
        sup.ok ? h('button', { onclick: () => { void listPorts().then((p) => { ports = p; fill(); }, (e: Error) => { app.error = `MIDI access refused: ${e.message}`; fill(); }); } }, 'Scan MIDI ports') : null,
        app.error ? h('p', { style: 'color:var(--red)' }, app.error) : null];
      body.replaceChildren(...kids.filter((k): k is Node => !!k));
    };
    fill();
  }

  // ---------- pages ----------
  function devicePage(): HTMLElement {
    const sim = app.sim;
    if (app.mode !== 'sim' || !sim) return card('Device', h('p', {}, app.mode === 'midi' ? 'The panel and screen are on your FM-1. Use the other tabs to edit.' : 'The simulator is not running.'));
    const canvas = h('canvas', { class: 'lcd', width: 240, height: 240, 'aria-label': 'Simulated FM-1 screen' });
    const ctx = canvas.getContext('2d')!, img = ctx.createImageData(240, 240);
    let buttons = 0, keys = 0;
    const btns = BUTTONS.map((name, i) => {
      const b = h('button', { 'aria-label': name }, name);
      const set = (on: boolean) => { buttons = on ? buttons | (1 << i) : buttons & ~(1 << i); sim.setButtons(buttons); void app.ensureAudio(); };
      b.addEventListener('pointerdown', () => set(true)); b.addEventListener('pointerup', () => set(false)); b.addEventListener('pointerleave', () => set(false));
      b.addEventListener('keydown', (e) => { if (e.key === ' ' || e.key === 'Enter') set(true); }); b.addEventListener('keyup', () => set(false));
      return b;
    });
    const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
    const keyEls: HTMLElement[] = [];
    const kb = h('div', { class: 'keys', 'aria-label': 'Keyboard F3 to G5' });
    const setKey = (k: number, on: boolean) => { keys = on ? keys | (1 << k) : keys & ~(1 << k); sim.setKeys(keys); keyEls[k].classList.toggle('down', on); if (on) void app.ensureAudio(); };
    for (let k = 0; k < 27; k++) {
      const w = WHITE.indexOf(k);
      const e = h('div', { class: w >= 0 ? 'w' : 'b' });
      if (w < 0) { const left = WHITE.filter((x) => x < k).length; e.style.left = `${(left / 16) * 100 - 2.1}%`; }
      e.addEventListener('pointerdown', (ev) => { e.setPointerCapture(ev.pointerId); setKey(k, true); });
      e.addEventListener('pointerup', () => setKey(k, false)); e.addEventListener('pointercancel', () => setKey(k, false));
      keyEls.push(e); kb.append(e);
    }
    onUpdate(() => {
      const fb = sim.frame(!canvas.dataset.drawn);
      if (fb) {
        canvas.dataset.drawn = '1';
        for (let i = 0; i < 57600; i++) { const v = fb[i]; img.data[4 * i] = (v >> 8) & 0xf8; img.data[4 * i + 1] = (v >> 3) & 0xfc; img.data[4 * i + 2] = (v << 3) & 0xf8; img.data[4 * i + 3] = 255; }
        ctx.putImageData(img, 0, 0);
      }
      const [lo, hi] = sim.leds();
      btns.forEach((b, i) => b.classList.toggle('led', !!(lo & (1 << i))));
      keyEls.forEach((e, k) => { const bit = 14 + k; e.classList.toggle('lit', bit < 32 ? !!(lo & (1 << bit)) : !!(hi & (1 << (bit - 32)))); });
    });
    return h('div', { class: 'device' },
      h('div', {}, canvas, h('p', { class: 'muted' }, 'This is the real firmware interface, rendered by the firmware core compiled to WebAssembly. Sound is produced by the same DSP code.')),
      h('div', {},
        card('Panel', h('div', { class: 'panel-btns' }, ...btns),
          h('div', { class: 'encs' }, ...ENCODERS.map((name, i) => h('div', { class: 'enc' }, h('button', { 'aria-label': `${name} left`, onclick: () => sim.turn(i, -1) }, '−'), h('span', {}, name), h('button', { 'aria-label': `${name} right`, onclick: () => sim.turn(i, 1) }, '+')))),
          kb),
        !app.audioOn && h('button', { class: 'primary', onclick: () => void app.ensureAudio() }, 'Enable sound')));
  }

  function envShape(get: () => [number, number, number, number]): Node {
    const NS = 'http://www.w3.org/2000/svg';
    const svg = document.createElementNS(NS, 'svg'); svg.setAttribute('viewBox', '0 0 200 50'); svg.setAttribute('style', 'width:100%;height:50px;margin-top:6px');
    const p = document.createElementNS(NS, 'polyline'); p.setAttribute('fill', 'none'); p.setAttribute('stroke', 'var(--accent)'); p.setAttribute('stroke-width', '2'); svg.append(p);
    onUpdate(() => { const [a, d, s, r] = get(); const xa = 2 + (a * 50) / 127, xd = xa + 3 + (d * 50) / 127, xs = xd + 36, xr = xs + 3 + (r * 50) / 127, ys = 48 - (s * 44) / 127; p.setAttribute('points', `2,48 ${xa},4 ${xd},${ys} ${xs},${ys} ${xr},48`); });
    return svg;
  }
  function soundPage(): HTMLElement {
    const t = app.track, patch = app.patches[t];
    if (!patch) return card('Sound Lab', 'No sound loaded yet.');
    const name = h('input', { type: 'text', maxLength: 12, value: patch.name, 'aria-label': 'Sound name', onchange: () => app.renamePatch(t, name.value) });
    const top = card('Sound', h('div', { class: 'row' }, h('label', { class: 'field' }, 'NAME', name),
      h('button', { onclick: () => { app.saveToLibrary(t); } }, 'Save to library'),
      h('button', { onpointerdown: () => app.audition(isKit(t) ? 0 : lastNote, true), onpointerup: () => app.audition(isKit(t) ? 0 : lastNote, false), disabled: !app.canSend }, 'Audition')));
    if (isKit(t)) {
      return h('div', { class: 'grid' }, top,
        ...schema.drums.map((dn, d) => card(dn, h('div', { class: 'knobs' }, ...[0, 1, 2, 3].map((f) => pp(t, d * 4 + f))),
          h('button', { style: 'margin-top:8px', disabled: !app.canSend, onpointerdown: () => app.audition(d, true) }, 'Hit'))),
        card('Kit', h('div', { class: 'knobs' }, ...[32, 33, 34, 35].map((id) => pp(t, id)))));
    }
    const P = patchId;
    const ops = [0, 1, 2, 3].map((op) => card(`Operator ${op + 1}`,
      h('div', { class: 'knobs' }, ...[0, 1, 2, 3, 5, 6, 7, 8, 9, 10].map((f) => pp(t, opParam(op, f)))),
      h('div', { class: 'row', style: 'margin-top:8px' }, pp(t, opParam(op, 4))),
      envShape(() => [5, 6, 7, 8].map((f) => app.patches[t]?.p[opParam(op, f)] ?? 0) as [number, number, number, number])));
    return h('div', { class: 'grid' }, top,
      card('Voice', h('div', { class: 'knobs' }, ...['ALGO', 'FEEDBACK', 'LEVEL', 'PAN', 'TRANSP', 'GLIDE', 'BEND', 'SPREAD'].map((n) => pp(t, P(n)))), h('div', { class: 'row' }, pp(t, P('MODE')))),
      ...ops,
      card('Filter', h('div', { class: 'row' }, pp(t, P('FILTER'))), h('div', { class: 'knobs', style: 'margin-top:8px' }, ...['CUTOFF', 'RESO', 'FLT ENV', 'KEYTRACK', 'FLT VEL', 'F.ATTACK', 'F.DECAY', 'F.SUST', 'F.REL'].map((n) => pp(t, P(n)))),
        envShape(() => ['F.ATTACK', 'F.DECAY', 'F.SUST', 'F.REL'].map((n) => app.patches[t]?.p[P(n)] ?? 0) as [number, number, number, number])),
      card('LFO', h('div', { class: 'row' }, pp(t, P('LFO WAVE')), pp(t, P('LFO DEST')), pp(t, P('LFO SYNC'))), h('div', { class: 'knobs', style: 'margin-top:8px' }, ...['LFO RATE', 'LFO DPTH', 'LFO FADE'].map((n) => pp(t, P(n))))),
      card('Macros (KNOB 1–4 on the HOME page)', ...[1, 2, 3, 4].map((m) => h('div', { class: 'row', style: 'margin-bottom:6px' }, pp(t, P(`MACRO ${m}`)), pp(t, P(`M${m} DEST`)), pp(t, P(`M${m} AMT`))))),
      card('Sends', h('div', { class: 'knobs' }, pp(t, P('DLY SEND')), pp(t, P('REV SEND')))));
  }

  function seqPage_(): HTMLElement {
    const t = app.track, sc = app.scene, kit = isKit(t);
    const pat = (): Pattern => app.patterns[sc][t];
    const edit = (key: string, fn: (p: Pattern) => void) => app.editPattern(sc, t, key, fn);
    const lenD: ParamDesc = { id: 0, name: 'LENGTH', min: 1, max: 64, def: 16, fmt: 0, labels: Array.from({ length: 64 }, (_, i) => String(i + 1)) };
    const head = card('Pattern', h('div', { class: 'row' },
      h('label', { class: 'field' }, 'SCENE', h('div', {}, ...'ABCDEFGH'.split('').map((l, i) => h('button', { class: i === sc ? 'on' : '', 'aria-pressed': String(i === sc), onclick: () => void app.selectScene(i) }, l)))),
      knob(lenD, 'LENGTH', () => pat().length, (v) => edit('len', (p) => { p.length = v; })),
      gp(globalId('SWING')), gp(globalId('TEMPO')),
      h('button', { class: 'danger', onclick: () => edit(`clr${Date.now()}`, (p) => { for (const s of p.steps) { s.note = 0; s.vel = 0; s.gate = 0; s.fx = 0; } }) }, 'Clear')));
    if (kit) {
      const lanes = h('div', { class: 'lanes' });
      const n = pat().length;
      const cells: HTMLElement[][] = [];
      schema.drums.forEach((dn, d) => {
        const row = h('div', { class: 'lane', style: `--n:${n}` }, dn);
        cells[d] = [];
        for (let i = 0; i < n; i++) {
          const c = h('i', { role: 'button', tabIndex: 0, 'aria-label': `${dn} step ${i + 1}`, onclick: () => edit(`d${d}:${i}:${Date.now()}`, (p) => { const s = p.steps[i]; s.note ^= 1 << d; if (s.note && !s.vel) s.vel = 100; if (!s.note) s.vel = 0; }) });
          c.addEventListener('keydown', (e) => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); c.click(); } });
          cells[d].push(c); row.append(c);
        }
        lanes.append(row);
      });
      onUpdate(() => { const p = pat(), step = app.status?.step[t] ?? -1; cells.forEach((row, d) => row.forEach((c, i) => { c.classList.toggle('on', !!(p.steps[i].note & (1 << d))); c.classList.toggle('play', app.playing && step === i); })); });
      return h('div', { class: 'grid', style: 'grid-template-columns:1fr' }, head, card('Drum lanes', lanes));
    }
    const grid = h('div', { class: 'steps' });
    const cells: HTMLElement[] = [];
    for (let i = 0; i < 16; i++) {
      const idx = seqPage * 16 + i;
      const c = h('div', { class: 'step', role: 'button', tabIndex: 0, 'aria-label': `Step ${idx + 1}`,
        onclick: () => { if (selStep === idx || !pat().steps[idx].note) edit(`tg${idx}:${Date.now()}`, (p) => { const s = p.steps[idx]; if (s.note) s.note = 0; else { s.note = lastNote; s.vel = s.vel || 100; s.gate = s.gate || 3; } }); selStep = idx; app.emit(true); } });
      c.addEventListener('keydown', (e) => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); c.click(); } });
      cells.push(c); grid.append(c);
    }
    onUpdate(() => {
      const p = pat(), step = app.status?.step[t] ?? -1;
      cells.forEach((c, i) => { const idx = seqPage * 16 + i, s = p.steps[idx]; c.className = `step${s.note ? ' on' : ''}${idx >= p.length ? ' out' : ''}${idx === selStep ? ' sel' : ''}${app.playing && step === idx ? ' play' : ''}`; c.textContent = s.note ? noteName(s.note) : String(idx + 1); });
    });
    const sd = (name: string, min: number, max: number, labels: (v: number) => string, get: (p: Pattern) => number, set: (p: Pattern, v: number) => void) => {
      const d: ParamDesc = { id: 0, name, min, max, def: min, fmt: 0, labels: Array.from({ length: max - min + 1 }, (_, i) => labels(min + i)) };
      return knob(d, name, () => get(pat()), (v) => { edit(`${name}${selStep}`, (p) => set(p, v)); if (name === 'NOTE') lastNote = v; });
    };
    const st = (p: Pattern) => p.steps[selStep];
    return h('div', { class: 'grid', style: 'grid-template-columns:1fr' }, head,
      card(`Steps ${seqPage * 16 + 1}–${seqPage * 16 + 16}`, h('div', { class: 'row', style: 'margin-bottom:8px' }, ...[0, 1, 2, 3].map((pg) => h('button', { class: pg === seqPage ? 'on' : '', onclick: () => { seqPage = pg; selStep = pg * 16; app.emit(true); } }, `${pg * 16 + 1}–${pg * 16 + 16}`))), grid,
        h('p', { class: 'muted' }, 'Click an empty step to add a note; click a selected note to remove it.')),
      card(`Step ${selStep + 1}`, h('div', { class: 'knobs' },
        sd('NOTE', 1, 127, noteName, (p) => st(p).note || lastNote, (p, v) => { st(p).note = v; st(p).vel = st(p).vel || 100; }),
        sd('VELOCITY', 1, 127, String, (p) => st(p).vel || 100, (p, v) => { st(p).vel = v; }),
        sd('GATE', 0, 15, (v) => `${schema.gateEighths[v] / 8}`, (p) => st(p).gate & 15, (p, v) => { st(p).gate = (st(p).gate & 0x30) | v; }),
        sd('CHANCE', 0, 7, (v) => `${((v + 1) * 100) / 8}%`, (p) => 7 - (st(p).fx & 7), (p, v) => { st(p).fx = (st(p).fx & 0x38) | (7 - v); }),
        sd('RATCHET', 0, 3, (v) => `x${v + 1}`, (p) => (st(p).gate >> 4) & 3, (p, v) => { st(p).gate = (st(p).gate & 15) | (v << 4); }),
        sd('CHORD', 0, 7, (v) => schema.chords[v], (p) => (st(p).fx >> 3) & 7, (p, v) => { st(p).fx = (st(p).fx & 7) | (v << 3); }))));
  }

  function roll(get: () => Pattern, kit: boolean): Node {
    const c = h('canvas', { width: 640, height: 160, style: 'width:100%;border-radius:8px;background:#05080c', 'aria-label': 'Pattern preview' });
    onUpdate(() => {
      const g = c.getContext('2d')!, p = get(), n = p.length;
      g.clearRect(0, 0, 640, 160);
      const notes = p.steps.slice(0, n).filter((s) => s.note).map((s) => s.note);
      const lo = kit ? 0 : Math.min(48, ...notes), hi = kit ? 7 : Math.max(lo + 12, ...notes);
      g.fillStyle = '#192432';
      for (let i = 0; i < n; i += 4) g.fillRect((i * 640) / n, 0, 1, 160);
      g.fillStyle = TRACK_COLORS[app.track];
      p.steps.slice(0, n).forEach((s, i) => {
        if (!s.note) return;
        const w = 640 / n;
        if (kit) { for (let d = 0; d < 8; d++) if (s.note & (1 << d)) g.fillRect(i * w, 150 - d * 19, w - 2, 12); }
        else g.fillRect(i * w, 150 - ((s.note - lo) * 140) / (hi - lo + 1), Math.max(3, (w * schema.gateEighths[s.gate & 15]) / 8 - 1), 6);
      });
      if (app.playing) { g.fillStyle = '#eaf2fb'; g.fillRect(((app.status?.step[app.track] ?? 0) * 640) / n, 0, 2, 160); }
    });
    return c;
  }
  function melodyPage(): HTMLElement {
    const G = globalId, lo = G('SEED LO'), hi = G('SEED HI');
    const seedOf = () => (app.globals[hi] ?? 0) * 128 + (app.globals[lo] ?? 0);
    const setSeed = (v: number) => { const s = Math.min(16383, Math.max(0, Math.round(v) || 0)); app.setGlobal(hi, s >> 7); app.setGlobal(lo, s & 127); };
    const seed = h('input', { type: 'number', min: 0, max: 16383, 'aria-label': 'Seed', onchange: () => setSeed(Number(seed.value)) });
    onUpdate(() => { if (document.activeElement !== seed) seed.value = String(seedOf()); });
    return h('div', { class: 'grid', style: 'grid-template-columns:1fr' },
      card(`Generate for track ${app.track + 1}`,
        h('p', { class: 'muted' }, 'The melody is produced by the firmware’s own generator, so the FM-1 creates the same pattern from the same seed. One seed gives one chord progression: generate BASS, CHORDS, ARP and MELODY on different tracks with the same seed and they fit together.'),
        h('div', { class: 'row' }, h('label', { class: 'field' }, 'SEED', seed), h('button', { onclick: () => setSeed(Math.floor(Math.random() * 16384)) }, '🎲 New seed'),
          gp(G('KEY')), gp(G('SCALE')), !isKit(app.track) && gp(G('STYLE')), gp(G('LENGTH'))),
        h('div', { class: 'knobs', style: 'margin:10px 0' }, gp(G('DENSITY')), gp(G('COMPLEX')), gp(G('VARIATN')), gp(G('RANGE'))),
        h('div', { class: 'row' }, h('button', { class: 'primary', disabled: !app.canSend, onclick: () => void app.generate() }, 'Generate'),
          h('button', { disabled: !app.genUndo, onclick: () => app.undoGenerate() }, 'Undo generate'),
          h('button', { onclick: () => app.togglePlay(), disabled: !app.canSend }, app.playing ? 'Stop' : 'Audition'))),
      card('Pattern', roll(() => app.patterns[app.scene][app.track], isKit(app.track))));
  }
  function mixerPage(): HTMLElement {
    const G = globalId;
    const strips = [0, 1, 2, 3].map((t) => {
      const m = h('i', {});
      onUpdate(() => { m.style.width = `${Math.min(100, ((app.status && t === app.status.track ? app.status.peak : 0) * 100) / 127)}%`; });
      const send = (rev: boolean) => (isKit(t) ? (rev ? 34 : 33) : patchId(rev ? 'REV SEND' : 'DLY SEND'));
      return h('section', { class: 'card strip', style: `--accent:${TRACK_COLORS[t]}` }, h('h3', {}, `${t + 1} · ${app.patches[t]?.name ?? ''}`),
        h('div', { class: 'knobs', style: 'justify-content:center' }, gp(trackGlobal(t, 0), 'VOLUME'), pp(t, send(false)), pp(t, send(true)), !isKit(t) && pp(t, patchId('PAN'))),
        h('div', { class: 'row', style: 'justify-content:center;margin-top:8px' }, gp(trackGlobal(t, 1), 'MUTE'), gp(trackGlobal(t, 2), 'ARP'), gp(trackGlobal(t, 3), 'ARP MODE'), gp(trackGlobal(t, 4), 'ARP RATE')),
        h('div', { class: 'knobs', style: 'justify-content:center;margin-top:8px' }, gp(trackGlobal(t, 5), 'ARP OCT'), gp(trackGlobal(t, 6), 'ARP GATE')));
    });
    return h('div', { class: 'grid' }, ...strips,
      card('Delay', h('div', { class: 'row' }, gp(G('DLY TIME'))), h('div', { class: 'knobs', style: 'margin-top:8px' }, gp(G('DLY FDBK')), gp(G('DLY TONE')), gp(G('DLY LVL')))),
      card('Reverb', h('div', { class: 'knobs' }, gp(G('REV SIZE')), gp(G('REV DAMP')), gp(G('REV LVL')))),
      card('Master', h('div', { class: 'knobs' }, gp(G('DRIVE')), gp(G('DJ FILT')), gp(G('PUMP')), gp(G('PUMP REL')), gp(G('TEMPO')), gp(G('SWING')))),
      card('Keyboard', h('div', { class: 'row' }, gp(G('KEY')), gp(G('SCALE')), gp(G('SCL LOCK')), gp(G('MIDI OUT'))), h('div', { class: 'knobs', style: 'margin-top:8px' }, gp(G('OCTAVE')))));
  }
  function libraryPage(): HTMLElement {
    const q = libFilter.toLowerCase();
    const items = app.library.filter((e) => (libCat < 0 || e.patch.category === libCat) && e.patch.name.toLowerCase().includes(q));
    const search = h('input', { type: 'search', placeholder: 'Search sounds', value: libFilter, 'aria-label': 'Search sounds', oninput: () => { libFilter = search.value; app.emit(true); queueMicrotask(() => { const s = document.querySelector<HTMLInputElement>('input[type=search]'); s?.focus(); s?.setSelectionRange(s.value.length, s.value.length); }); } });
    const cat = h('select', { 'aria-label': 'Category', onchange: () => { libCat = Number(cat.value); app.emit(true); } }, h('option', { value: '-1' }, 'All categories'), ...schema.categories.map((c, i) => h('option', { value: String(i), selected: i === libCat }, c)));
    return h('div', { class: 'grid', style: 'grid-template-columns:1fr' },
      card('Your library (stored in this browser)', h('div', { class: 'row' }, search, cat,
        h('button', { onclick: () => app.saveToLibrary(app.track) }, `Save track ${app.track + 1} sound`),
        h('button', { onclick: () => download('zarya-library.json', JSON.stringify({ format: 'zarya-library', schema: schema.schema, sounds: app.library.map((e) => e.patch) }, null, 1)) }, 'Export library'),
        h('button', { onclick: () => pickFile((text) => { try { const f = JSON.parse(text) as { format?: string; schema?: number; sounds?: unknown[] }; if (f.format !== 'zarya-library' || f.schema !== schema.schema || !Array.isArray(f.sounds)) throw new Error('not a ZARYA library for this schema'); for (const s of f.sounds) { const p = s as { p?: unknown; name?: unknown; type?: unknown; category?: unknown }; if (!Array.isArray(p.p) || typeof p.name !== 'string') throw new Error('a sound is malformed'); app.library.push({ id: `${Date.now()}-${Math.random()}`, patch: { type: Number(p.type) ? 1 : 0, category: Number(p.category) & 7, name: p.name, p: p.p.map(Number) }, savedAt: Date.now() }); } app.saveToLibrary(app.track); app.library.shift(); app.emit(true); } catch (e) { app.error = `Import failed: ${(e as Error).message}`; app.emit(true); } }) }, 'Import library')),
        items.length ? h('table', {}, h('tr', {}, h('th', {}, 'Name'), h('th', {}, 'Category'), h('th', {}, 'Type'), h('th', {}, '')),
          ...items.map((e) => h('tr', {}, h('td', {}, e.patch.name), h('td', {}, schema.categories[e.patch.category] ?? ''), h('td', {}, e.patch.type ? 'Kit' : 'Synth'),
            h('td', {}, h('button', { disabled: (e.patch.type === 1) !== isKit(app.track), title: 'Loads into the editor (and the device edit buffer when live)', onclick: () => void app.setPatch(app.track, e.patch) }, `Load to track ${app.track + 1}`), ' ',
              h('button', { class: 'danger', onclick: () => { if (confirm(`Delete "${e.patch.name}" from the library?`)) app.deleteFromLibrary(e.id); } }, 'Delete')))))
          : h('p', { class: 'muted' }, 'No sounds yet. The 33 factory sounds and 4 kits live in the firmware: browse them with the PRESETS encoder (Device tab), then save the ones you tweak here.')));
  }
  function backupPage(): HTMLElement {
    return h('div', { class: 'grid' },
      card('Export project file', h('p', { class: 'muted' }, 'Saves the four sounds, all eight scenes and the song settings as a JSON file on your computer. It is an editor project file — not a backup of the FM-1 flash memory, firmware or user slots.'),
        h('button', { class: 'primary', onclick: () => void app.loadAllScenes().then(() => download(`zarya-project-${new Date().toISOString().slice(0, 10)}.json`, JSON.stringify(app.exportProject()))) }, 'Download project')),
      card('Import project file', h('p', { class: 'muted' }, 'The file is fully validated before anything changes. It replaces the project in the editor (Undo restores it). With an FM-1 connected nothing is sent until you press “Send to FM-1”.'),
        h('button', { onclick: () => pickFile((text) => { void app.importProject(text).catch((e: Error) => { app.error = `Import failed: ${e.message}`; app.emit(true); }); }) }, 'Choose file…')),
      card('Draft', h('p', { class: 'muted' }, 'The editor keeps a draft of your last edits in this browser.'), h('button', { disabled: !app.hasDraft(), onclick: () => { if (confirm('Replace the current editor project with the saved draft?')) void app.restoreDraft().catch((e: Error) => { app.error = e.message; app.emit(true); }); } }, 'Restore draft')),
      card('Saving on the FM-1', h('p', { class: 'muted' }, 'User sounds and projects are saved on the instrument itself (SAVE button), where every write is confirmed on its screen. The editor has no command that writes the FM-1 memory.')),
      card('Firmware', h('p', { class: 'muted' }, 'This editor does not install firmware. See docs/SAFETY_STATUS.md in the project for the current flash decision.')));
  }
  function diagPage(): HTMLElement {
    const sup = midiSupport(), id = app.identity;
    const caps = id ? Object.entries(CAP).map(([k, bit]) => `${k}: ${id.caps & bit ? 'yes' : 'no'}`).join(' · ') : '—';
    const log = h('pre', { class: 'log', 'aria-label': 'Message log' });
    onUpdate(() => { if (page === 'Diagnostics') log.textContent = app.log.slice(-80).join('\n'); });
    const row = (k: string, v: string) => h('tr', {}, h('th', {}, k), h('td', {}, v));
    return h('div', { class: 'grid', style: 'grid-template-columns:1fr' },
      card('Connection', h('table', {},
        row('Mode', app.mode === 'sim' ? 'Firmware simulator (DEMO / NO HARDWARE)' : app.mode === 'midi' ? `Hardware via Web MIDI: ${app.deviceLabel}` : app.mode),
        row('Web MIDI in this browser', sup.ok ? 'available' : `unavailable — ${sup.reason}`),
        row('Editor', `schema ${schema.schema}, protocol ${schema.protocol}, built for firmware ${schema.version}`),
        row('Device', id ? `firmware ${id.firmware}, schema ${id.schema}, protocol ${id.protocol}, ${id.hardware ? 'hardware' : 'simulator'}` : 'not identified'),
        row('Capabilities', caps),
        row('Device storage', id ? ['none', 'not formatted', 'ready', 'error'][id.storage] ?? '?' : '—'),
        row('Status', app.status ? `voices ${app.status.voices}, load ${app.status.load}%, scene ${'ABCDEFGH'[app.status.scene]}` : '—'))),
      card('Protocol log (last 80 frames)', log, h('button', { onclick: () => { app.log = []; app.emit(); } }, 'Clear')));
  }

  // ---------- shell ----------
  function build() {
    updaters = [];
    const active = document.activeElement?.getAttribute('aria-label');
    const tc = TRACK_COLORS[app.track];
    const body = page === 'Device' ? devicePage() : page === 'Sound Lab' ? soundPage() : page === 'Sequencer' ? seqPage_() : page === 'Melody Lab' ? melodyPage()
      : page === 'Mixer & FX' ? mixerPage() : page === 'Library' ? libraryPage() : page === 'Backup' ? backupPage() : diagPage();
    const parts: Child[] = [header(),
      app.mode === 'sim' ? h('div', { class: 'banner' }, 'Demo mode: you are editing a firmware simulator in the browser, not an FM-1.') : null,
      app.error ? h('div', { class: 'banner err', role: 'alert' }, app.error, h('button', { onclick: () => { app.error = ''; app.emit(true); } }, 'Dismiss')) : null,
      h('div', { class: 'tracks', role: 'group', 'aria-label': 'Tracks' }, ...[0, 1, 2, 3].map((t) => h('button', { style: `--tc:${TRACK_COLORS[t]};border-left-color:${TRACK_COLORS[t]}`, 'aria-pressed': String(t === app.track), onclick: () => app.selectTrack(t) }, `Track ${t + 1}`, h('small', {}, app.patches[t]?.name ?? '—')))),
      h('nav', { class: 'tabs', role: 'tablist' }, ...PAGES.map((p) => h('button', { role: 'tab', 'aria-selected': String(p === page), onclick: () => { page = p; build(); } }, p))),
      h('main', { style: `--accent:${tc};--tc:${tc}` }, body)];
    root.replaceChildren(...parts.filter((k): k is Node => !!k));
    if (active) document.querySelector<HTMLElement>(`[aria-label="${CSS.escape(active)}"]`)?.focus();
  }
  let pending = false, structuralPending = false;
  app.subscribe((structural) => {
    structuralPending ||= structural;
    if (pending) return;
    pending = true;
    requestAnimationFrame(() => { pending = false; if (structuralPending) { structuralPending = false; build(); } else for (const u of updaters) u(); });
  });
  document.addEventListener('keydown', (e) => {
    if (!(e.ctrlKey || e.metaKey) || e.key.toLowerCase() !== 'z') return;
    if ((e.target as HTMLElement).tagName === 'INPUT') return;
    e.preventDefault();
    void (e.shiftKey ? app.redo() : app.undo());
  });
  setInterval(() => { if (page === 'Device') for (const u of updaters) u(); }, 50);
  build();
}
