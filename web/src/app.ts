// SPDX-License-Identifier: GPL-3.0-only
// Editor state and device synchronisation. The editor's copy of the project is
// the source of truth for the UI; a device (simulator or FM-1) is a peer that
// is read on connect and written only through explicit, volatile commands.
import { BLOB, PATCH_BYTES, PATTERN_BYTES, blobPack, blobUnpack, clampParam, decodePatch, decodePattern, emptyPattern, encodePatch, encodePattern, globalId, patchDesc, sanitizeGlobals, schema, type Patch, type Pattern } from './core/schema';
import { Client, type Identity, type Status, type Transport } from './core/protocol';
import { Fm1Sim, SimTransport } from './transport/sim';
import { WebMidiTransport, type PortPair } from './transport/webmidi';

export interface Snapshot { patches: Patch[]; globals: number[]; patterns: Pattern[][] }
export interface LibraryEntry { id: string; patch: Patch; savedAt: number }
export interface ProjectFile { format: 'zarya-project'; schema: number; firmware: string; exportedAt: string; globals: number[]; patches: Patch[]; patterns: Pattern[][] }

const LIB_KEY = 'zarya.library.v1', DRAFT_KEY = 'zarya.draft.v1';
const clone = <T>(v: T): T => JSON.parse(JSON.stringify(v)) as T;

export class App {
  sim: Fm1Sim | null = null;
  client: Client | null = null;
  identity: Identity | null = null;
  mode: 'starting' | 'sim' | 'midi' | 'offline' = 'starting';
  deviceLabel = '';
  liveEdit = false;          // hardware only: user must switch it on
  unsynced = false;          // local edits not sent to the hardware
  track = 0;
  scene = 0;
  patches: Patch[] = [];
  globals: number[] = [];
  patterns: Pattern[][] = Array.from({ length: schema.scenes }, () => Array.from({ length: schema.tracks }, emptyPattern));
  loadedScenes = new Set<number>();
  status: Status | null = null;
  log: string[] = [];
  error = '';
  library: LibraryEntry[] = [];
  genUndo: { scene: number; track: number; pattern: Pattern } | null = null;
  private undoStack: Snapshot[] = [];
  private redoStack: Snapshot[] = [];
  private lastUndoKey = '';
  private lastUndoAt = 0;
  private patternTimers = new Map<string, ReturnType<typeof setTimeout>>();
  private listeners = new Set<(structural: boolean) => void>();
  private audio: { ctx: AudioContext; node: AudioWorkletNode; rendered: number; start: number } | null = null;
  private timers: ReturnType<typeof setInterval>[] = [];

  subscribe(fn: (structural: boolean) => void) { this.listeners.add(fn); }
  emit(structural = false) { for (const fn of this.listeners) fn(structural); }
  get canSend() { return !!this.client && !this.client.closed && (this.mode === 'sim' || this.liveEdit); }
  get playing() { return !!this.status?.playing; }

  // ---------- lifecycle ----------
  async start(wasmUrl: string) {
    try { this.library = JSON.parse(localStorage.getItem(LIB_KEY) ?? '[]') as LibraryEntry[]; } catch { this.library = []; }
    try {
      const bytes = await (await fetch(wasmUrl)).arrayBuffer();
      this.sim = await Fm1Sim.create(bytes);
      await this.attach(new SimTransport(this.sim), 'sim');
    } catch (e) {
      this.mode = 'offline';
      this.error = `Simulator failed to start: ${(e as Error).message}`;
      this.emit(true);
    }
    this.timers.push(setInterval(() => this.tick(), 10), setInterval(() => void this.pollStatus(), 120));
  }
  private async attach(transport: Transport, mode: 'sim' | 'midi') {
    const client = new Client(transport);
    client.log = (dir, f) => {
      this.log.push(`${dir} ${[...f].map((b) => b.toString(16).padStart(2, '0')).join(' ')}`);
      if (this.log.length > 300) this.log.splice(0, this.log.length - 300);
    };
    client.onEvent = (e) => {
      if (e.type === 'param') {
        if (e.scope === 0 && this.patches[e.track]) this.patches[e.track].p[e.id] = e.value;
        else if (e.scope === 1) this.globals[e.id] = e.value;
        this.emit();
      } else if (e.type === 'patch') void this.readPatch(e.track).then(() => this.emit(true));
      else { this.error = `Device reported error ${e.code} for command ${e.cmd}`; this.emit(); }
    };
    const identity = await client.hello(); // read-only identity check; throws on mismatch
    if (mode === 'midi' && !identity.hardware) throw new Error('The port answered as a simulator, not as an FM-1.');
    this.client?.close();
    this.client = client;
    this.identity = identity;
    this.mode = mode;
    this.deviceLabel = transport.label;
    this.liveEdit = false;
    this.unsynced = false;
    this.loadedScenes.clear();
    transport.onClose = () => { if (this.client === client) void this.handleDisconnect(); };
    await this.readAll();
    this.undoStack = []; this.redoStack = [];
    this.emit(true);
  }
  private async handleDisconnect() {
    // Keep local edits, stop every queued write, fall back to the simulator.
    for (const t of this.patternTimers.values()) clearTimeout(t);
    this.patternTimers.clear();
    const wasHardware = this.mode === 'midi';
    this.client = null; this.identity = null; this.status = null; this.liveEdit = false;
    this.mode = 'offline';
    if (wasHardware) { this.error = 'FM-1 disconnected. Your edits are kept in the editor.'; this.saveDraft(); }
    this.emit(true);
    if (wasHardware && this.sim) {
      const keep = this.snapshot();
      await this.attach(new SimTransport(this.sim), 'sim');
      this.restore(keep, false);
      await this.pushAll();
      this.emit(true);
    }
  }
  async connectMidi(pair: PortPair) {
    this.error = '';
    try { await this.attach(await WebMidiTransport.open(pair), 'midi'); }
    catch (e) { this.error = `Could not connect: ${(e as Error).message}`; this.emit(true); throw e; }
  }
  async useSimulator() { if (this.sim && this.mode !== 'sim') { this.client?.close(); } }

  // ---------- reading ----------
  private async readPatch(t: number) {
    if (!this.client) return;
    this.patches[t] = decodePatch(blobUnpack(await this.client.getBlob(BLOB.PATCH, t), BLOB.PATCH, PATCH_BYTES));
  }
  async readScene(scene: number) {
    if (!this.client || this.loadedScenes.has(scene)) return;
    for (let t = 0; t < schema.tracks; t++)
      this.patterns[scene][t] = decodePattern(blobUnpack(await this.client.getBlob(BLOB.PATTERN, scene, t), BLOB.PATTERN, PATTERN_BYTES));
    this.loadedScenes.add(scene);
  }
  async readAll() {
    if (!this.client) return;
    for (let t = 0; t < schema.tracks; t++) await this.readPatch(t);
    this.globals = [...blobUnpack(await this.client.getBlob(BLOB.GLOBALS), BLOB.GLOBALS, schema.globals)];
    this.scene = this.globals[globalId('SCENE')] ?? 0;
    await this.readScene(this.scene);
  }
  private async pollStatus() {
    if (!this.client || this.client.closed) return;
    try {
      const s = await this.client.status();
      const sceneChanged = s.scene !== this.status?.scene;
      this.status = s;
      if (sceneChanged && s.scene !== this.scene) { this.scene = s.scene; await this.readScene(s.scene); this.emit(true); } else this.emit();
    } catch { /* a missed poll is not an error worth showing */ }
  }

  // ---------- editing ----------
  snapshot(): Snapshot { return clone({ patches: this.patches, globals: this.globals, patterns: this.patterns }); }
  private pushUndo(key: string) {
    const now = Date.now();
    if (key === this.lastUndoKey && now - this.lastUndoAt < 700) { this.lastUndoAt = now; return; } // one drag = one step
    this.lastUndoKey = key; this.lastUndoAt = now;
    this.undoStack.push(this.snapshot());
    if (this.undoStack.length > 40) this.undoStack.shift();
    this.redoStack = [];
  }
  private restore(s: Snapshot, emit = true) {
    this.patches = clone(s.patches); this.globals = clone(s.globals); this.patterns = clone(s.patterns);
    if (emit) this.emit(true);
  }
  get canUndo() { return this.undoStack.length > 0; }
  get canRedo() { return this.redoStack.length > 0; }
  async undo() { const s = this.undoStack.pop(); if (!s) return; this.redoStack.push(this.snapshot()); this.lastUndoKey = ''; this.restore(s); await this.afterBulkChange(); }
  async redo() { const s = this.redoStack.pop(); if (!s) return; this.undoStack.push(this.snapshot()); this.lastUndoKey = ''; this.restore(s); await this.afterBulkChange(); }
  private async afterBulkChange() {
    this.saveDraft();
    if (this.canSend) await this.pushAll(); else if (this.mode === 'midi') { this.unsynced = true; this.emit(); }
  }
  private markLocal() { if (this.mode === 'midi' && !this.liveEdit) this.unsynced = true; this.saveDraft(); }

  setPatchParam(track: number, id: number, value: number) {
    const d = patchDesc(track, id), patch = this.patches[track];
    if (!d || !patch) return;
    const v = clampParam(d, value);
    if (patch.p[id] === v) return;
    this.pushUndo(`p${track}:${id}`);
    patch.p[id] = v;
    if (this.canSend) this.client!.setParam(0, track, id, v);
    this.markLocal();
    this.emit();
  }
  setGlobal(id: number, value: number) {
    const d = schema.global[id];
    if (!d) return;
    const v = clampParam(d, value);
    if (this.globals[id] === v) return;
    this.pushUndo(`g${id}`);
    this.globals[id] = v;
    if (this.canSend) this.client!.setParam(1, 0, id, v);
    this.markLocal();
    this.emit();
  }
  editPattern(scene: number, track: number, key: string, fn: (p: Pattern) => void) {
    this.pushUndo(`s${scene}:${track}:${key}`);
    fn(this.patterns[scene][track]);
    this.schedulePattern(scene, track);
    this.markLocal();
    this.emit();
  }
  private schedulePattern(scene: number, track: number) {
    if (!this.canSend) return;
    const k = `${scene}:${track}`;
    clearTimeout(this.patternTimers.get(k));
    this.patternTimers.set(k, setTimeout(() => { this.patternTimers.delete(k); void this.pushPattern(scene, track); }, 120));
  }
  private async pushPattern(scene: number, track: number) {
    if (!this.canSend) return;
    try { await this.client!.putBlob(BLOB.PATTERN, scene, track, blobPack(BLOB.PATTERN, encodePattern(this.patterns[scene][track]))); }
    catch (e) { this.error = `Pattern not sent: ${(e as Error).message}`; this.emit(); }
  }
  async setPatch(track: number, patch: Patch, undoKey = 'patch') {
    this.pushUndo(`${undoKey}${track}:${Date.now()}`);
    const p = clone(patch);
    p.type = track === 3 ? 1 : 0;
    this.patches[track] = decodePatch(encodePatch(p)); // clamp to the schema
    this.markLocal();
    if (this.canSend) {
      try { await this.client!.putBlob(BLOB.PATCH, track, 0, blobPack(BLOB.PATCH, encodePatch(this.patches[track]))); }
      catch (e) { this.error = `Sound not sent: ${(e as Error).message}`; }
    }
    this.emit(true);
  }
  renamePatch(track: number, name: string) { const p = clone(this.patches[track]); p.name = name; void this.setPatch(track, p, 'name'); }

  /** Send the whole editor project to the device's volatile edit buffers. */
  async pushAll() {
    if (!this.client || this.client.closed) return;
    const c = this.client;
    try {
      await c.putBlob(BLOB.GLOBALS, 0, 0, blobPack(BLOB.GLOBALS, Uint8Array.from(sanitizeGlobals(this.globals))));
      for (let t = 0; t < schema.tracks; t++) await c.putBlob(BLOB.PATCH, t, 0, blobPack(BLOB.PATCH, encodePatch(this.patches[t])));
      for (const s of this.loadedScenes)
        for (let t = 0; t < schema.tracks; t++) await c.putBlob(BLOB.PATTERN, s, t, blobPack(BLOB.PATTERN, encodePattern(this.patterns[s][t])));
      this.unsynced = false;
    } catch (e) { this.error = `Send failed: ${(e as Error).message}`; }
    this.emit();
  }
  setLiveEdit(on: boolean) { this.liveEdit = on; this.emit(true); }
  selectTrack(t: number) { this.track = t; if (this.canSend) this.client!.select(t); this.emit(true); }
  async selectScene(s: number) {
    this.scene = s;
    if (this.canSend) this.client!.select(this.track, s);
    await this.readScene(s);
    this.emit(true);
  }
  togglePlay() { if (this.client && (this.mode === 'sim' || this.liveEdit)) { void this.ensureAudio(); this.client.transportCmd(!this.playing); } }
  audition(note: number, on: boolean) { if (this.canSend) { void this.ensureAudio(); this.client!.note(this.track, note, on ? 100 : 0); } }

  // ---------- generator ----------
  async generate() {
    if (!this.canSend) { this.error = 'The generator runs inside the firmware: connect the simulator or enable Live edit.'; this.emit(); return; }
    const scene = this.scene, track = this.track;
    this.pushUndo(`gen${Date.now()}`);
    this.genUndo = { scene, track, pattern: clone(this.patterns[scene][track]) };
    this.client!.flush();
    try {
      await this.client!.generate(track);
      this.patterns[scene][track] = decodePattern(blobUnpack(await this.client!.getBlob(BLOB.PATTERN, scene, track), BLOB.PATTERN, PATTERN_BYTES));
    } catch (e) { this.error = `Generate failed: ${(e as Error).message}`; }
    this.saveDraft();
    this.emit(true);
  }
  undoGenerate() {
    const u = this.genUndo;
    if (!u) return;
    this.genUndo = null;
    this.editPattern(u.scene, u.track, `ungen${Date.now()}`, (p) => { p.steps = u.pattern.steps; p.length = u.pattern.length; });
    this.emit(true);
  }

  // ---------- library, drafts, project files ----------
  saveToLibrary(track: number) {
    this.library.unshift({ id: `${Date.now()}-${Math.random().toString(36).slice(2, 8)}`, patch: clone(this.patches[track]), savedAt: Date.now() });
    this.persistLibrary();
  }
  deleteFromLibrary(id: string) { this.library = this.library.filter((e) => e.id !== id); this.persistLibrary(); }
  private persistLibrary() { try { localStorage.setItem(LIB_KEY, JSON.stringify(this.library)); } catch { this.error = 'Browser storage is full or blocked.'; } this.emit(true); }
  private saveDraft() { try { localStorage.setItem(DRAFT_KEY, JSON.stringify(this.exportProject())); } catch { /* drafts are best effort */ } }
  hasDraft() { try { return !!localStorage.getItem(DRAFT_KEY); } catch { return false; } }
  async restoreDraft() { const raw = localStorage.getItem(DRAFT_KEY); if (raw) await this.importProject(raw); }
  exportProject(): ProjectFile {
    return { format: 'zarya-project', schema: schema.schema, firmware: schema.version, exportedAt: new Date().toISOString(), ...this.snapshot() };
  }
  /** Validate a project file completely before anything is replaced. */
  static parseProject(text: string): Snapshot {
    let f: Partial<ProjectFile>;
    try { f = JSON.parse(text) as Partial<ProjectFile>; } catch { throw new Error('not a JSON file'); }
    if (f.format !== 'zarya-project') throw new Error('not a ZARYA project file');
    if (f.schema !== schema.schema) throw new Error(`project schema ${String(f.schema)} is not supported (need ${schema.schema})`);
    if (!Array.isArray(f.patches) || f.patches.length !== schema.tracks || !Array.isArray(f.globals) || !Array.isArray(f.patterns) || f.patterns.length !== schema.scenes) throw new Error('project is incomplete');
    const patches = f.patches.map((p, t) => {
      if (!p || !Array.isArray(p.p) || typeof p.name !== 'string') throw new Error(`sound ${t + 1} is malformed`);
      return decodePatch(encodePatch({ ...p, type: t === 3 ? 1 : 0 }));
    });
    const patterns = f.patterns.map((row) => {
      if (!Array.isArray(row) || row.length !== schema.tracks) throw new Error('patterns are malformed');
      return row.map((p) => {
        if (!p || !Array.isArray(p.steps) || p.steps.length !== schema.steps) throw new Error('a pattern is malformed');
        return decodePattern(encodePattern(p));
      });
    });
    return { patches, globals: sanitizeGlobals(f.globals.map(Number)), patterns };
  }
  async importProject(text: string) {
    const s = App.parseProject(text);
    this.pushUndo(`import${Date.now()}`);
    this.restore(s, false);
    for (let i = 0; i < schema.scenes; i++) this.loadedScenes.add(i);
    this.scene = this.globals[globalId('SCENE')] ?? 0;
    if (this.mode === 'sim') await this.pushAll(); else if (this.mode === 'midi') this.unsynced = true; // hardware: explicit send only
    this.emit(true);
  }
  async loadAllScenes() { for (let s = 0; s < schema.scenes; s++) await this.readScene(s); }

  // ---------- simulator clock and audio ----------
  private tick() {
    if (!this.sim) return;
    this.sim.tick(10);
    const a = this.audio;
    if (a && a.ctx.state === 'running') {
      const want = Math.floor(((a.ctx.currentTime - a.start) * 44100) / 128) + 12; // ~35 ms ahead
      let n = Math.min(want - a.rendered, 48);
      if (n > 40) { a.rendered = want - 12; n = 12; } // resync after a stall
      for (; n > 0; n--, a.rendered++) {
        const src = this.sim.audio(), f = new Float32Array(256);
        const gain = this.mode === 'sim' ? 1 / 8388608 : 0; // the simulator is silent while hardware is connected
        for (let i = 0; i < 256; i++) f[i] = src[i] * gain;
        a.node.port.postMessage(f, [f.buffer]);
      }
    }
  }
  async ensureAudio() {
    if (this.audio) { if (this.audio.ctx.state !== 'running') await this.audio.ctx.resume(); return; }
    if (!this.sim || typeof AudioContext === 'undefined') return;
    const ctx = new AudioContext({ sampleRate: 44100, latencyHint: 'interactive' });
    await ctx.audioWorklet.addModule(new URL('./ring-worklet.js', document.baseURI).href);
    const node = new AudioWorkletNode(ctx, 'ring', { outputChannelCount: [2] });
    node.connect(ctx.destination);
    this.audio = { ctx, node, rendered: 0, start: ctx.currentTime };
    await ctx.resume();
    this.audio.start = ctx.currentTime;
    this.emit();
  }
  get audioOn() { return this.audio?.ctx.state === 'running'; }
}
